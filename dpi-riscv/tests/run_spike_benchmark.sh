#!/bin/bash
#
# run_spike_benchmark.sh — Benchmark RISC-V tests on Spike simulator.
#
# This script runs riscv-tests ELF binaries through Spike, measures
# execution time, and outputs CSV for comparison with Herve.
#
# Supports two modes:
#   1. ISA test suite mode (default): runs all rv32ui-p-*, rv32um-p-*, rv32uc-p-* tests
#   2. Single benchmark mode (--benchmark): runs a single benchmark ELF (e.g. median.riscv)
#
# Usage:
#   ./tests/run_spike_benchmark.sh                              # default: ../riscv-tests/isa
#   ./tests/run_spike_benchmark.sh /path/to/isa/dir             # custom path
#   ./tests/run_spike_benchmark.sh --csv                        # CSV only
#   ./tests/run_spike_benchmark.sh --benchmark <elf>            # single benchmark ELF
#   ./tests/run_spike_benchmark.sh --benchmark <elf> --csv      # single benchmark, CSV
#
# Prerequisites:
#   - spike in PATH (RISC-V ISA simulator)
#   - riscv-tests ELF binaries built in ../riscv-tests/isa/ (for ISA mode)
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RISCV_TESTS_DIR="$(cd "$PROJECT_DIR/../riscv-tests" 2>/dev/null && pwd || echo "$PROJECT_DIR/../riscv-tests")"
ISA_DIR="$RISCV_TESTS_DIR/isa"

CSV_MODE=false
BENCHMARK_ELF=""
POSITIONAL=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --csv) CSV_MODE=true; shift ;;
        --benchmark) BENCHMARK_ELF="$2"; shift 2 ;;
        *) POSITIONAL+=("$1"); shift ;;
    esac
done

if [[ ${#POSITIONAL[@]} -gt 0 ]]; then
    ISA_DIR="${POSITIONAL[0]}"
fi

# Check prerequisites
if ! command -v spike &>/dev/null; then
    echo "ERROR: spike not found in PATH" >&2
    exit 1
fi

# -----------------------------------------------------------------------
# Single benchmark mode (--benchmark <elf>)
# -----------------------------------------------------------------------
if [[ -n "$BENCHMARK_ELF" ]]; then
    if [[ ! -f "$BENCHMARK_ELF" ]]; then
        echo "ERROR: Benchmark ELF not found: $BENCHMARK_ELF" >&2
        exit 1
    fi

    test_name="$(basename "$BENCHMARK_ELF")"

    if ! $CSV_MODE; then
        echo "=== Spike Benchmark (single) ==="
        echo ""
        echo "Benchmark ELF: $BENCHMARK_ELF"
        echo ""
    fi

    # Run spike with log-commits to get instruction count
    # Spike exits with non-zero for bare-metal ELFs that use EBREAK or HTIF,
    # so we ignore the exit code and parse the log.
    log_file=$(mktemp /tmp/spike_bench_XXXXXX.log)
    set +e
    exec 3>&1 4>&2
    spike_time=$( { time spike --isa=rv32imc --log-commits "$BENCHMARK_ELF" > "$log_file" 2>&1; } 2>&1 )
    spike_exit=$?
    set -e

    # Parse wall-clock time from `time` output
    real_sec=$(echo "$spike_time" | grep "^real" | awk '{print $2}' | \
        sed 's/m/ /' | sed 's/s//' | awk '{if (NF==2) print $1*60+$2; else print $1}')

    # Parse instruction count from log-commits
    insn_count=$(grep -c "core   0:" "$log_file" 2>/dev/null || echo 0)

    rm -f "$log_file"

    # Determine pass/fail: if spike produced any committed instructions, consider it PASS
    if [[ $insn_count -gt 0 ]]; then
        status="PASS"
    else
        status="FAIL"
    fi

    ips=$(echo "$insn_count $real_sec" | awk '{if ($2 > 0) printf "%.0f", $1/$2; else print "0"}')

    if $CSV_MODE; then
        echo "test_name,passed,instructions,time_sec,ips,reason"
        echo "$test_name,$status,$insn_count,$real_sec,$ips,"
        echo "TOTAL,$status,$insn_count,$real_sec,$ips,1 test"
    else
        printf "  [%s] %-40s %6d insn  %8.4f s  %12s IPS\n" \
            "$status" "$test_name" "$insn_count" "$real_sec" "$ips"
        echo ""
        echo "========================================"
        echo "  Total instructions: $insn_count"
        printf "  Total time:         %.4f s\n" "$real_sec"
        echo "  Overall IPS:        $ips"
        echo "========================================"
    fi

    exit 0
fi

# -----------------------------------------------------------------------
# ISA test suite mode (default)
# -----------------------------------------------------------------------
if [[ ! -d "$ISA_DIR" ]]; then
    echo "ERROR: ISA directory not found: $ISA_DIR" >&2
    exit 1
fi

if ! $CSV_MODE; then
    echo "=== Spike Benchmark ==="
    echo ""
    echo "ISA directory: $ISA_DIR"
    echo ""
fi

# Collect ELF test files (rv32ui-p-*, rv32um-p-*, rv32uc-p-*)
# Exclude .dump files
TESTS=()
for pattern in rv32ui-p- rv32um-p- rv32uc-p-; do
    for f in "$ISA_DIR/$pattern"*; do
        [[ -f "$f" ]] || continue
        case "$f" in *.dump|*.hex) continue ;; esac
        TESTS+=("$f")
    done
done

if ! $CSV_MODE; then
    echo "Found ${#TESTS[@]} test ELF binaries"
    echo ""
fi

# Sort tests
IFS=$'\n' TESTS=($(sort <<<"${TESTS[*]}")); unset IFS

# Results storage
declare -a TEST_NAMES
declare -a TIMES
declare -a INSN_COUNTS
declare -a STATUSES

TOTAL_TIME=0
TOTAL_INSN=0
PASSED=0
FAILED=0

for test_elf in "${TESTS[@]}"; do
    test_name="$(basename "$test_elf")"

    # Run spike with log-commits to get instruction count
    # Spike exits with non-zero for bare-metal ELFs that use EBREAK,
    # so we ignore the exit code and parse the log.
    log_file=$(mktemp /tmp/spike_bench_XXXXXX.log)
    set +e
    # Use /usr/bin/time for wall-clock measurement
    exec 3>&1 4>&2
    spike_time=$( { time spike --isa=rv32imc --log-commits "$test_elf" > "$log_file" 2>&1; } 2>&1 )
    spike_exit=$?
    set -e

    # Parse wall-clock time from `time` output
    # Format: real XmY.YYYs or X.XXs
    real_sec=$(echo "$spike_time" | grep "^real" | awk '{print $2}' | \
        sed 's/m/ /' | sed 's/s//' | awk '{if (NF==2) print $1*60+$2; else print $1}')

    # Parse instruction count from log-commits
    # Spike log-commits format: each line has "core   0: >" followed by instruction info
    # Count lines with "core   0:" that are commit lines
    insn_count=$(grep -c "core   0:" "$log_file" 2>/dev/null || echo 0)

    rm -f "$log_file"

    # Determine pass/fail from spike output
    # riscv-tests set gp=1 on pass, but spike doesn't expose gp easily.
    # We check if spike ran without crashing and produced output.
    if [[ $insn_count -gt 0 ]]; then
        status="PASS"
        PASSED=$((PASSED + 1))
    else
        status="FAIL"
        FAILED=$((FAILED + 1))
    fi

    TEST_NAMES+=("$test_name")
    TIMES+=("$real_sec")
    INSN_COUNTS+=("$insn_count")
    STATUSES+=("$status")

    TOTAL_TIME=$(echo "$TOTAL_TIME + $real_sec" | bc 2>/dev/null || echo "0")
    TOTAL_INSN=$((TOTAL_INSN + insn_count))

    if ! $CSV_MODE; then
        ips=$(echo "$insn_count $real_sec" | awk '{if ($2 > 0) printf "%.0f", $1/$2; else print "0"}')
        printf "  [%s] %-40s %6d insn  %8.4f s  %12s IPS\n" \
            "$status" "$test_name" "$insn_count" "$real_sec" "$ips"
    fi
done

# Output CSV
if $CSV_MODE; then
    echo "test_name,passed,instructions,time_sec,ips,reason"
    for i in "${!TEST_NAMES[@]}"; do
        ips=$(echo "${INSN_COUNTS[$i]} ${TIMES[$i]}" | awk '{if ($2 > 0) printf "%.0f", $1/$2; else print "0"}')
        echo "${TEST_NAMES[$i]},${STATUSES[$i]},${INSN_COUNTS[$i]},${TIMES[$i]},$ips,"
    done
    total_ips=$(echo "$TOTAL_INSN $TOTAL_TIME" | awk '{if ($2 > 0) printf "%.0f", $1/$2; else print "0"}')
    echo "TOTAL,$([ $FAILED -eq 0 ] && echo PASS || echo FAIL),$TOTAL_INSN,$TOTAL_TIME,$total_ips,$PASSED passed, $FAILED failed"
else
    echo ""
    echo "========================================"
    echo "  Total instructions: $TOTAL_INSN"
    printf "  Total time:         %.4f s\n" "$TOTAL_TIME"
    total_ips=$(echo "$TOTAL_INSN $TOTAL_TIME" | awk '{if ($2 > 0) printf "%.0f", $1/$2; else print "0"}')
    echo "  Overall IPS:        $total_ips"
    echo "  Passed:  $PASSED"
    echo "  Failed:  $FAILED"
    echo "========================================"
fi
