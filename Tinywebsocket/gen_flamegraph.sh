#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
FLAMEGRAPH_DIR="${FLAMEGRAPH_DIR:-${SCRIPT_DIR}/../FlameGraph}"
SVG_DIR="${SCRIPT_DIR}/svg"
PERF_DIR="${SCRIPT_DIR}/perf_data"

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <test-binary> [binary-args...]"
    echo "Example: $0 Test_log"
    exit 1
fi

TARGET="$1"
shift || true

if [[ "$TARGET" != /* ]]; then
    REL_NAME="$(basename -- "${TARGET#./}")"
    if [[ -x "${SCRIPT_DIR}/exe/${REL_NAME}" ]]; then
        TARGET="${SCRIPT_DIR}/exe/${REL_NAME}"
    else
        TARGET="${SCRIPT_DIR}/${TARGET#./}"
    fi
fi

if [[ ! -x "$TARGET" ]]; then
    echo "Error: test binary not found or not executable: $TARGET"
    exit 1
fi

if [[ ! -x "${FLAMEGRAPH_DIR}/flamegraph.pl" ]] || [[ ! -x "${FLAMEGRAPH_DIR}/stackcollapse-perf.pl" ]]; then
    echo "Error: FlameGraph scripts not found under: ${FLAMEGRAPH_DIR}"
    echo "Hint: set FLAMEGRAPH_DIR=/absolute/path/to/FlameGraph"
    exit 1
fi

BIN_NAME="$(basename -- "$TARGET")"
PERF_DATA="${PERF_DIR}/${BIN_NAME}.perf.data"
FOLDED="${PERF_DIR}/${BIN_NAME}.folded"
SVG_FILE="${SVG_DIR}/${BIN_NAME}.svg"
PERF_FREQ="${PERF_FREQ:-99}"
PERF_EVENT="${PERF_EVENT:-cpu-clock}"
PERF_CALLGRAPH="${PERF_CALLGRAPH:-dwarf,16384}"

mkdir -p "$SVG_DIR" "$PERF_DIR"

echo "[1/3] perf record -> ${PERF_DATA}"
perf record -e "$PERF_EVENT" -F "$PERF_FREQ" -g --call-graph "$PERF_CALLGRAPH" -o "$PERF_DATA" -- "$TARGET" "$@"

echo "[2/3] stackcollapse -> ${FOLDED}"
perf script -i "$PERF_DATA" | "${FLAMEGRAPH_DIR}/stackcollapse-perf.pl" > "$FOLDED"

if [[ ! -s "$FOLDED" ]]; then
    echo "Error: no folded stacks generated: $FOLDED"
    echo "Hint: test binary may be too short-lived; run a longer workload or loop the case."
    exit 1
fi

echo "[3/3] flamegraph -> ${SVG_FILE}"
"${FLAMEGRAPH_DIR}/flamegraph.pl" "$FOLDED" > "$SVG_FILE"

echo "Done: ${SVG_FILE}"
