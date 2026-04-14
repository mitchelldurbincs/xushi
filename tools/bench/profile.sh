#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN_DEFAULT="$ROOT_DIR/build/xushi"
SCENARIO_DEFAULT="$ROOT_DIR/scenarios/benchmark_dense.json"
OUT_DIR_DEFAULT="$ROOT_DIR/profiles"

BIN="$BIN_DEFAULT"
SCENARIO="$SCENARIO_DEFAULT"
OUT_DIR="$OUT_DIR_DEFAULT"
MAKE_FLAMEGRAPH=0

usage() {
  cat <<USAGE
Usage: $(basename "$0") [options] [scenario.json]

Run xushi benchmark mode with platform-specific profiling guidance.

Options:
  -b, --bin PATH         xushi binary path (default: $BIN_DEFAULT)
  -o, --out-dir DIR      output directory for profile artifacts (default: $OUT_DIR_DEFAULT)
  -f, --flamegraph       on Linux, also try generating SVG flamegraph
  -h, --help             show this help text

Examples:
  tools/bench/profile.sh
  tools/bench/profile.sh scenarios/default.json
  tools/bench/profile.sh --flamegraph --out-dir profiles/run_20260413
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -b|--bin)
      BIN="$2"
      shift 2
      ;;
    -o|--out-dir)
      OUT_DIR="$2"
      shift 2
      ;;
    -f|--flamegraph)
      MAKE_FLAMEGRAPH=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    -* )
      echo "Unknown option: $1" >&2
      usage
      exit 1
      ;;
    * )
      SCENARIO="$1"
      shift
      ;;
  esac
done

mkdir -p "$OUT_DIR"
RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)"
PERF_DATA="$OUT_DIR/perf_${RUN_ID}.data"
BENCH_LOG="$OUT_DIR/bench_${RUN_ID}.log"

OS="$(uname -s)"

echo "run_id=$RUN_ID"
echo "binary=$BIN"
echo "scenario=$SCENARIO"
echo "out_dir=$OUT_DIR"

case "$OS" in
  Linux)
    if [[ ! -x "$BIN" ]]; then
      echo "error: binary not executable: $BIN" >&2
      echo "hint: cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j" >&2
      exit 1
    fi

    echo "[1/3] Running benchmark and capturing perf data..."
    perf record -g --call-graph dwarf -o "$PERF_DATA" -- "$BIN" --bench "$SCENARIO" | tee "$BENCH_LOG"

    echo "[2/3] Open interactive report with:"
    echo "  perf report -i $PERF_DATA"

    echo "[3/3] Text report summary:"
    perf report -i "$PERF_DATA" --stdio | sed -n '1,80p'

    if [[ "$MAKE_FLAMEGRAPH" -eq 1 ]]; then
      PERF_SCRIPT_OUT="$OUT_DIR/perf_${RUN_ID}.folded"
      FLAMEGRAPH_SVG="$OUT_DIR/flamegraph_${RUN_ID}.svg"

      if command -v stackcollapse-perf.pl >/dev/null 2>&1 && command -v flamegraph.pl >/dev/null 2>&1; then
        echo "Generating flamegraph..."
        perf script -i "$PERF_DATA" | stackcollapse-perf.pl > "$PERF_SCRIPT_OUT"
        flamegraph.pl "$PERF_SCRIPT_OUT" > "$FLAMEGRAPH_SVG"
        echo "Flamegraph: $FLAMEGRAPH_SVG"
      else
        echo "warning: flamegraph tools not found (stackcollapse-perf.pl, flamegraph.pl)."
        echo "Install FlameGraph tools: https://github.com/brendangregg/FlameGraph"
      fi
    fi
    ;;

  Darwin)
    cat <<'MAC'
macOS profiling workflow (Instruments):
  1) Build with symbols:
       cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
       cmake --build build -j
  2) Launch Time Profiler:
       xcrun xctrace record --template 'Time Profiler' \
         --output profiles/xctrace_<RUN_ID>.trace \
         --launch -- ./build/xushi --bench scenarios/benchmark_dense.json
  3) Open trace:
       open profiles/xctrace_<RUN_ID>.trace
  4) In Instruments, filter hot frames by module files:
       sensing.cpp, belief.cpp, comm.cpp, sim_engine.cpp
MAC
    ;;

  MINGW*|MSYS*|CYGWIN*|Windows_NT)
    cat <<'WIN'
Windows profiling workflow:
  WPA (ETW):
    1) Record with Windows Performance Recorder (CPU sampling profile).
    2) Run: build\Release\xushi.exe --bench scenarios\benchmark_dense.json
    3) Stop capture and open .etl in WPA.

  Visual Studio Profiler:
    1) Build Release with debug info (/Zi).
    2) Debug > Performance Profiler > CPU Usage.
    3) Start profiling and run benchmark scenario.

  In both tools, attribute hot symbols to:
    sensing.cpp, belief.cpp, comm.cpp, sim_engine.cpp
WIN
    ;;

  *)
    echo "Unsupported OS: $OS" >&2
    exit 1
    ;;
esac

echo "\nArtifacts:"
echo "  benchmark log: $BENCH_LOG"
if [[ -f "$PERF_DATA" ]]; then
  echo "  perf data:     $PERF_DATA"
fi
