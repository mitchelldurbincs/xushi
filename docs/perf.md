# Performance Profiling Workflow

This document defines a repeatable profiling workflow for `xushi --bench` and ties profile artifacts to benchmark outputs for traceability.

## Prerequisites

- Build with symbols for useful stack traces:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

- Use the profiling helper script:

```bash
tools/bench/profile.sh --help
```

## 1) Run benchmark scenario

Choose a deterministic scenario (typically `scenarios/mvp_contract_2v2.json`) and run benchmark mode:

```bash
./build/xushi --bench scenarios/mvp_contract_2v2.json
```

or via helper:

```bash
tools/bench/profile.sh scenarios/mvp_contract_2v2.json
```

The helper script emits a UTC `run_id` and stores benchmark logs under `profiles/`.

## 2) Capture profile

### Linux (`perf`)

```bash
tools/bench/profile.sh --flamegraph scenarios/mvp_contract_2v2.json
```

This captures:

- `profiles/perf_<run_id>.data` (sampling profile)
- `profiles/bench_<run_id>.log` (benchmark stdout)
- optional `profiles/flamegraph_<run_id>.svg`

Inspect samples:

```bash
perf report -i profiles/perf_<run_id>.data
```

### macOS (Instruments)

`tools/bench/profile.sh` prints exact `xctrace` instructions. Typical flow:

1. Record with Time Profiler template.
2. Launch `./build/xushi --bench ...` from Instruments.
3. Save `.trace` bundle as `profiles/xctrace_<run_id>.trace`.

### Windows (WPA / Visual Studio Profiler)

`tools/bench/profile.sh` prints WPA and VS Profiler steps. Save captures as:

- `profiles/wpa_<run_id>.etl` (WPA)
- `profiles/vsprof_<run_id>.*` (VS profiler output)

## 3) Attribute hot symbols to simulation modules

When reading call stacks or top tables, map hotspots to core files:

- `src/sensing.cpp` — LOS checks, observation generation
- `src/belief.cpp` — track update/decay/expiration
- `src/comm.cpp` — queueing, latency/loss behavior
- `src/sim_engine.cpp` — phase orchestration and per-tick integration

Recommended Linux triage commands:

```bash
perf report -i profiles/perf_<run_id>.data --stdio | head -n 120
perf annotate -i profiles/perf_<run_id>.data
```

If symbol names are mangled, demangle while reviewing (`c++filt`) or configure your profiling UI to demangle C++ names.

## 4) Link profile snapshots to benchmark result IDs

For traceability, use the same `<run_id>` across all artifacts produced from one benchmark execution.

Suggested convention:

- Benchmark log: `profiles/bench_<run_id>.log`
- Profile snapshot: `profiles/perf_<run_id>.data` or `profiles/xctrace_<run_id>.trace`
- Optional summary note: `profiles/summary_<run_id>.md`

In any performance note/PR, include:

- scenario path
- git commit SHA
- benchmark result identifier (`run_id`)
- profile artifact paths
- top hotspots and mapped module owner (`sensing.cpp`, `belief.cpp`, `comm.cpp`, `sim_engine.cpp`)

This makes it possible to trace a reported regression back to both benchmark output and raw profiler capture.
