#!/usr/bin/env bash
# run_profile_all.sh — Run the profiling harness over all ISA tests and benchmarks,
# collect JSON results, then generate a visualisation report.
#
# Usage:
#   bash scripts/run_profile_all.sh
#
# Prerequisites:
#   - profile_run.out must already be built (make profile_build)
#   - fyp18-riscv-emulator/riscv-tests/images/ must contain .bin files
#   - benchmark/ must contain mt-*.bin files
#
# Outputs:
#   - profile_results/<name>.json  — per-benchmark JSON reports
#   - profile_results/profile_report.png — visualisation (requires matplotlib)

set -euo pipefail

PROFILE_BIN="./profile_run.out"
RESULTS_DIR="profile_results"
IMAGE_PATH="fyp18-riscv-emulator/src/Image"
ISA_IMAGES_DIR="fyp18-riscv-emulator/riscv-tests/images"
BENCHMARK_DIR="benchmark"
VISUALIZE_SCRIPT="scripts/profile_visualize.py"

# Default cycle timeouts
ISA_TIMEOUT=50000000       # 50M cycles for ISA tests
BENCH_TIMEOUT=500000000    # 500M cycles for benchmarks

# Colours for terminal output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Colour

log_info()  { echo -e "${GREEN}[run_profile_all]${NC} $*"; }
log_warn()  { echo -e "${YELLOW}[run_profile_all] WARNING:${NC} $*"; }
log_error() { echo -e "${RED}[run_profile_all] ERROR:${NC} $*"; }

# ── Prerequisite checks ───────────────────────────────────────────────────────

if [ ! -f "$PROFILE_BIN" ]; then
    log_error "profile_run.out not found. Run 'make profile_build' first."
    exit 1
fi

mkdir -p "$RESULTS_DIR"

run_profile() {
    local name="$1"
    local image="$2"
    local timeout="$3"
    local json_out="$RESULTS_DIR/${name}.json"

    log_info "Running: $name  (timeout=${timeout})"
    cp "$image" "$IMAGE_PATH"

    if "$PROFILE_BIN" \
            --image "$IMAGE_PATH" \
            --name  "$name" \
            --output "$json_out" \
            --timeout "$timeout" 2>&1; then
        log_info "  => PASS: $json_out"
    else
        local rc=$?
        if [ $rc -eq 1 ]; then
            log_warn "  => FAIL (test reported failure): $json_out"
        elif [ $rc -eq 2 ]; then
            log_warn "  => TIMEOUT after ${timeout} cycles: $json_out"
        else
            log_warn "  => exit code $rc: $json_out"
        fi
        # JSON is still written on failure/timeout — continue
    fi
}

# ── ISA regression tests ──────────────────────────────────────────────────────

if [ -d "$ISA_IMAGES_DIR" ]; then
    ISA_COUNT=$(find "$ISA_IMAGES_DIR" -name "*.bin" | wc -l)
    log_info "Found $ISA_COUNT ISA test images in $ISA_IMAGES_DIR"

    for img in "$ISA_IMAGES_DIR"/*.bin; do
        [ -f "$img" ] || continue
        name=$(basename "$img" .bin)
        run_profile "isa_${name}" "$img" "$ISA_TIMEOUT"
    done
else
    log_warn "ISA images directory not found: $ISA_IMAGES_DIR — skipping ISA tests."
fi

# ── Benchmark suite ───────────────────────────────────────────────────────────

run_benchmark_suite() {
    local suite_name="$1"
    local bin_prefix="$2"
    local scales="$3"

    for s in $scales; do
        local img="${BENCHMARK_DIR}/${bin_prefix}-s${s}.bin"
        if [ -f "$img" ]; then
            run_profile "${suite_name}-s${s}" "$img" "$BENCH_TIMEOUT"
        else
            log_warn "Benchmark image not found: $img"
        fi
    done
}

SCALES="1 2 3 4 5"

log_info "=== vvadd ==="
run_benchmark_suite "vvadd"  "mt-vvadd"       "$SCALES"

log_info "=== matmul ==="
run_benchmark_suite "matmul" "mt-matmul"      "$SCALES"

log_info "=== filter ==="
run_benchmark_suite "filter" "mt-mask-sfilter" "$SCALES"

log_info "=== csaxpy ==="
run_benchmark_suite "csaxpy" "mt-csaxpy"      "$SCALES"

log_info "=== histo ==="
run_benchmark_suite "histo"  "mt-histo"       "$SCALES"

# mt-image.bin (single binary)
if [ -f "${BENCHMARK_DIR}/mt-image.bin" ]; then
    log_info "=== image demo ==="
    run_profile "image" "${BENCHMARK_DIR}/mt-image.bin" "$BENCH_TIMEOUT"
fi

# ── Visualisation ─────────────────────────────────────────────────────────────

JSON_COUNT=$(find "$RESULTS_DIR" -name "*.json" | wc -l)
log_info "Collected $JSON_COUNT JSON result(s) in $RESULTS_DIR"

if [ "$JSON_COUNT" -gt 0 ]; then
    if command -v python3 &>/dev/null; then
        log_info "Generating visualisation..."
        python3 "$VISUALIZE_SCRIPT" "$RESULTS_DIR" \
            --out "${RESULTS_DIR}/profile_report.png" || \
            log_warn "Visualisation failed (is matplotlib installed?)"
    else
        log_warn "python3 not found — skipping visualisation."
    fi
fi

log_info "Done. Results in: $RESULTS_DIR/"
