#!/usr/bin/env python3
import argparse
import json
import math
import re
import statistics
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List

THRESHOLDS = {
    "strict": {
        "warn": {
            "throughput_drop_pct": 12.0,
            "sensing_phase_increase_pct": 15.0,
            "p95_runtime_increase_pct": 18.0,
        },
        "fail": {
            "throughput_drop_pct": 25.0,
            "sensing_phase_increase_pct": 28.0,
            "p95_runtime_increase_pct": 30.0,
        },
    },
    "noise_tolerant": {
        "warn": {
            "throughput_drop_pct": 18.0,
            "sensing_phase_increase_pct": 24.0,
            "p95_runtime_increase_pct": 26.0,
        },
        "fail": {
            "throughput_drop_pct": 35.0,
            "sensing_phase_increase_pct": 38.0,
            "p95_runtime_increase_pct": 40.0,
        },
    },
}

BENCHMARKS = [
    {"name": "default", "scenario": "scenarios/default.json", "type": "strict", "smoke_runs": 3, "full_runs": 10},
    {"name": "benchmark_dense", "scenario": "scenarios/benchmark_dense.json", "type": "noise_tolerant", "smoke_runs": 2, "full_runs": 8},
    {"name": "noisy_perception", "scenario": "scenarios/noisy_perception.json", "type": "noise_tolerant", "smoke_runs": 2, "full_runs": 8},
    {"name": "multi_agent", "scenario": "scenarios/multi_agent.json", "type": "strict", "smoke_runs": 2, "full_runs": 8},
]

THROUGHPUT_RE = re.compile(r"^throughput:\s+([0-9]+(?:\.[0-9]+)?)\s+ticks/sec$", re.MULTILINE)
PER_TICK_RE = re.compile(r"^per tick:\s+([0-9]+(?:\.[0-9]+)?)\s+ms$", re.MULTILINE)
SENSING_RE = re.compile(r"^sensing:\s+([0-9]+(?:\.[0-9]+)?)\s+ms", re.MULTILINE)


@dataclass
class RunSample:
    throughput_ticks_per_sec: float
    per_tick_ms: float
    sensing_ms: float


def parse_metrics(output: str) -> RunSample:
    throughput = THROUGHPUT_RE.search(output)
    per_tick = PER_TICK_RE.search(output)
    sensing = SENSING_RE.search(output)
    if not throughput or not per_tick or not sensing:
        raise RuntimeError("could not parse benchmark output")
    return RunSample(
        throughput_ticks_per_sec=float(throughput.group(1)),
        per_tick_ms=float(per_tick.group(1)),
        sensing_ms=float(sensing.group(1)),
    )


def run_once(binary: str, scenario: str) -> RunSample:
    cmd = [binary, "--bench", scenario]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        raise RuntimeError(
            f"benchmark command failed ({proc.returncode}): {' '.join(cmd)}\n"
            f"stdout:\n{proc.stdout}\n\nstderr:\n{proc.stderr}"
        )
    return parse_metrics(proc.stdout)


def pct_change(current: float, baseline: float) -> float:
    if baseline == 0:
        return 0.0
    return ((current - baseline) / baseline) * 100.0


def load_json(path: Path):
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def benchmark_current(binary: str, mode: str) -> Dict[str, dict]:
    results: Dict[str, dict] = {}
    for b in BENCHMARKS:
        runs = b["smoke_runs"] if mode == "smoke" else b["full_runs"]
        samples = [run_once(binary, b["scenario"]) for _ in range(runs)]
        per_tick_samples = [s.per_tick_ms for s in samples]
        sensing_samples = [s.sensing_ms for s in samples]
        throughput_samples = [s.throughput_ticks_per_sec for s in samples]
        p95_idx = max(0, min(len(per_tick_samples) - 1, math.ceil(0.95 * len(per_tick_samples)) - 1))
        p95_runtime_ms = sorted(per_tick_samples)[p95_idx]

        results[b["name"]] = {
            "name": b["name"],
            "scenario": b["scenario"],
            "benchmark_type": b["type"],
            "runs": runs,
            "samples": {
                "throughput_ticks_per_sec": throughput_samples,
                "per_tick_ms": per_tick_samples,
                "sensing_ms": sensing_samples,
            },
            "metrics": {
                "throughput_ticks_per_sec": statistics.fmean(throughput_samples),
                "sensing_phase_ms": statistics.fmean(sensing_samples),
                "p95_runtime_ms": p95_runtime_ms,
            },
        }
    return results


def compare(current: Dict[str, dict], baseline: Dict[str, dict], mode: str, warn_only: bool):
    report = []
    failures = []
    warnings = []

    for name, cur in current.items():
        base = baseline.get("benchmarks", {}).get(name)
        if not base:
            warnings.append(f"no baseline found for benchmark '{name}'")
            report.append({"name": name, "status": "⚠️", "note": "missing baseline", "regressions": {}})
            continue

        btype = cur["benchmark_type"]
        warn_t = THRESHOLDS[btype]["warn"]
        fail_t = THRESHOLDS[btype]["fail"]

        cur_m = cur["metrics"]
        base_m = base["metrics"]

        throughput_drop_pct = -pct_change(cur_m["throughput_ticks_per_sec"], base_m["throughput_ticks_per_sec"])
        sensing_increase_pct = pct_change(cur_m["sensing_phase_ms"], base_m["sensing_phase_ms"])
        p95_increase_pct = pct_change(cur_m["p95_runtime_ms"], base_m["p95_runtime_ms"])

        regressions = {
            "throughput_drop_pct": throughput_drop_pct,
            "sensing_phase_increase_pct": sensing_increase_pct,
            "p95_runtime_increase_pct": p95_increase_pct,
        }

        level = "pass"
        reasons: List[str] = []

        for key, value in regressions.items():
            if value > fail_t[key]:
                level = "fail"
                reasons.append(f"{key}={value:.2f}% > fail({fail_t[key]:.2f}%)")
            elif value > warn_t[key] and level != "fail":
                level = "warn"
                reasons.append(f"{key}={value:.2f}% > warn({warn_t[key]:.2f}%)")

        if level == "fail":
            if warn_only:
                warnings.append(f"{name}: " + "; ".join(reasons) + " (warn-only mode)")
                status = "⚠️"
            else:
                failures.append(f"{name}: " + "; ".join(reasons))
                status = "❌"
        elif level == "warn":
            warnings.append(f"{name}: " + "; ".join(reasons))
            status = "⚠️"
        else:
            status = "✅"

        report.append(
            {
                "name": name,
                "status": status,
                "benchmark_type": btype,
                "regressions": regressions,
                "baseline_metrics": base_m,
                "current_metrics": cur_m,
                "note": "; ".join(reasons) if reasons else "within thresholds",
            }
        )

    summary = {
        "mode": mode,
        "warn_only": warn_only,
        "warnings": warnings,
        "failures": failures,
        "report": report,
    }
    return summary


def render_markdown(comparison: dict, baseline_source: str) -> str:
    lines = []
    lines.append("# Benchmark Report")
    lines.append("")
    lines.append(f"- Mode: `{comparison['mode']}`")
    lines.append(f"- Baseline source: `{baseline_source}`")
    lines.append(f"- Warn-only: `{comparison['warn_only']}`")
    lines.append("")
    lines.append("## Threshold Profiles")
    lines.append("")
    for profile, cfg in THRESHOLDS.items():
        lines.append(f"- **{profile}**: warn={cfg['warn']} fail={cfg['fail']}")
    lines.append("")
    lines.append("## Comparison")
    lines.append("")
    lines.append("| Status | Benchmark | Type | Throughput Δ (drop %) | Sensing Δ (%) | p95 runtime Δ (%) | Note |")
    lines.append("|---|---|---:|---:|---:|---:|---|")
    for row in comparison["report"]:
        reg = row.get("regressions", {})
        lines.append(
            f"| {row['status']} | `{row['name']}` | `{row.get('benchmark_type', '-')}` | "
            f"{reg.get('throughput_drop_pct', float('nan')):.2f} | "
            f"{reg.get('sensing_phase_increase_pct', float('nan')):.2f} | "
            f"{reg.get('p95_runtime_increase_pct', float('nan')):.2f} | {row.get('note', '')} |"
        )

    if comparison["warnings"]:
        lines.append("")
        lines.append("## Warnings")
        for w in comparison["warnings"]:
            lines.append(f"- ⚠️ {w}")

    if comparison["failures"]:
        lines.append("")
        lines.append("## Failures")
        for f in comparison["failures"]:
            lines.append(f"- ❌ {f}")

    lines.append("")
    lines.append("## Raw artifacts")
    lines.append("- `bench-current.json`")
    lines.append("- `bench-comparison.json`")

    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=["smoke", "full"], required=True)
    parser.add_argument("--binary", default="./build/xushi")
    parser.add_argument("--baseline", default=".github/benchmarks/baseline.json")
    parser.add_argument("--artifact-baseline", default="bench-baseline/baseline.json")
    parser.add_argument("--warn-only", action="store_true")
    parser.add_argument("--outdir", default="bench-artifacts")
    args = parser.parse_args()

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    artifact_baseline = Path(args.artifact_baseline)
    committed_baseline = Path(args.baseline)

    baseline_path = artifact_baseline if artifact_baseline.exists() else committed_baseline
    baseline_source = "artifact" if artifact_baseline.exists() else "committed"
    if not baseline_path.exists():
        print(f"error: baseline not found at {artifact_baseline} or {committed_baseline}", file=sys.stderr)
        return 2

    baseline = load_json(baseline_path)
    current = benchmark_current(args.binary, args.mode)

    comparison = compare(current, baseline, args.mode, args.warn_only)

    current_payload = {
        "mode": args.mode,
        "benchmarks": current,
    }

    (outdir / "bench-current.json").write_text(json.dumps(current_payload, indent=2) + "\n", encoding="utf-8")
    (outdir / "bench-comparison.json").write_text(json.dumps(comparison, indent=2) + "\n", encoding="utf-8")
    (outdir / "bench-report.md").write_text(render_markdown(comparison, baseline_source), encoding="utf-8")
    (outdir / "baseline.json").write_text(json.dumps(current_payload, indent=2) + "\n", encoding="utf-8")

    print((outdir / "bench-report.md").read_text(encoding="utf-8"))

    if comparison["failures"] and not args.warn_only:
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
