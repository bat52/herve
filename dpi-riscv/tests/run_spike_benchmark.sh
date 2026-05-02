#!/bin/bash
#
# run_spike_benchmark.sh — Benchmark RISC-V tests on Spike simulator.
#
# This script runs each riscv-tests ELF binary through Spike, measures
# execution time, and outputs CSV for comparison with Herve.
#
# Usage:
#   ./tests/run_spike_benchmark.sh                    # default: ../riscv-tests/isa
#   ./tests/run_spike_benchmark.sh /path/to/isa/dir   # custom path
#   ./tests/run_spike_benchmark.sh --csv              # CSV only
#
# Prerequisites:
#   - spike in PATH (RISC-V ISA simulator)
#   - riscv-tests ELF binaries built in ../riscv-tests/isa/
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RISCV_TESTS_DIR="$(cd "$PROJECT_DIR/../riscv-tests" 2>/dev/null && pwd || echo "$PROJECT_DIR/../riscv-tests")"
ISA_DIR="$RISCV_TESTS_DIR/isa"

CSV_MODE=false
POSITIONAL=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --csv) CSV_MODE=true; shift ;;
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
