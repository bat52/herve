#!/bin/bash
#
# test.sh — Run all valid make configurations and verify they pass.
#
# Usage:  ./test.sh
#         VERILATOR=verilator-4 ./test.sh     # custom verilator binary
#
# The script first checks which tools are available, groups tests by
# dependency, and runs each one.  A summary is printed at the end.
#

set -o pipefail

cd "$(dirname "$0")/.." || exit 1

# ---------------------------------------------------------------------------
# Counters
# ---------------------------------------------------------------------------
PASS=0
FAIL=0
SKIP=0

# ---------------------------------------------------------------------------
# Helper: run a single make target and record pass/fail/skip.
#   $1 : descriptive name
#   $2 : make target (and any extra flags)
#   $3 : reason to skip (empty = don't skip)
# ---------------------------------------------------------------------------
run_test() {
    local name="$1"
    local target="$2"
    local skip_reason="$3"

    if [ -n "$skip_reason" ]; then
        echo "  [SKIP] $name  ($skip_reason)"
        SKIP=$((SKIP + 1))
        return
    fi

    echo "  [RUN ] $name"
    if make "$target" 2>&1; then
        echo "  [PASS] $name"
        PASS=$((PASS + 1))
    else
        echo "  [FAIL] $name"
        FAIL=$((FAIL + 1))
    fi
}

# ---------------------------------------------------------------------------
# Tool detection
# ---------------------------------------------------------------------------
echo "============================================"
echo " test.sh — Tool detection"
echo "============================================"

HAS_GXX=0
if command -v g++ &>/dev/null; then
    HAS_GXX=1
    echo "  g++           : found"
else
    echo "  g++           : NOT found (standalone tests will be skipped)"
fi

HAS_TOOLCHAIN=0
if command -v "${RISCV_PREFIX:-riscv64-unknown-elf}-as" &>/dev/null; then
    HAS_TOOLCHAIN=1
    echo "  RISC-V asm    : found (${RISCV_PREFIX:-riscv64-unknown-elf})"
else
    echo "  RISC-V asm    : NOT found (firmware + Verilator tests will be skipped)"
fi

HAS_VERILATOR=0
VERILATOR_CMD="${VERILATOR:-verilator}"
if command -v "$VERILATOR_CMD" &>/dev/null; then
    HAS_VERILATOR=1
    echo "  verilator     : found ($VERILATOR_CMD)"
else
    echo "  verilator     : NOT found (Verilator-based tests will be skipped)"
fi

echo ""

# ---------------------------------------------------------------------------
# Pre-clean
# ---------------------------------------------------------------------------
echo "============================================"
echo " make clean"
echo "============================================"
make clean
echo ""

# ---------------------------------------------------------------------------
# 1. Firmware builds  (need RISC-V toolchain)
# ---------------------------------------------------------------------------
echo "============================================"
echo " 1. Firmware builds"
echo "============================================"

SKIP_FW=
[ "$HAS_TOOLCHAIN" -eq 0 ] && SKIP_FW="no RISC-V toolchain"

run_test "firmware"            "firmware"            "$SKIP_FW"
run_test "firmware_muldiv"     "firmware_muldiv"     "$SKIP_FW"
run_test "firmware_irq"        "firmware_irq"        "$SKIP_FW"
run_test "firmware_ahb"        "firmware_ahb"        "$SKIP_FW"
echo ""

# ---------------------------------------------------------------------------
# 2. Standalone ISS tests  (need g++ only)
# ---------------------------------------------------------------------------
echo "============================================"
echo " 2. Standalone ISS tests (no Verilator)"
echo "============================================"

SKIP_ISS=
[ "$HAS_GXX" -eq 0 ] && SKIP_ISS="no g++"

run_test "run_standalone"      "run_standalone"      "$SKIP_ISS"
run_test "run_c_test"          "run_c_test"          "$SKIP_ISS"
run_test "run_irq_standalone"  "run_irq_standalone"  "$SKIP_ISS"
run_test "run_riscv_tests"     "run_riscv_tests"     "$SKIP_ISS"
echo ""

# ---------------------------------------------------------------------------
# 3. Verilator-based tests  (need g++ + toolchain + Verilator)
# ---------------------------------------------------------------------------
echo "============================================"
echo " 3. Verilator-based tests"
echo "============================================"

SKIP_VLT=
[ "$HAS_GXX" -eq 0 ]        && SKIP_VLT="no g++"
[ "$HAS_TOOLCHAIN" -eq 0 ]  && SKIP_VLT="no RISC-V toolchain"
[ "$HAS_VERILATOR" -eq 0 ]  && SKIP_VLT="no Verilator"

run_test "run"                "run"                 "$SKIP_VLT"
run_test "run_fast"           "run_fast"            "$SKIP_VLT"
run_test "run_debug"          "run_debug"           "$SKIP_VLT"
run_test "run_mmio_regs"      "run_mmio_regs"       "$SKIP_VLT"
run_test "run_muldiv"         "run_muldiv"          "$SKIP_VLT"
run_test "run_irq"            "run_irq"             "$SKIP_VLT"
run_test "run_ahb"            "run_ahb"             "$SKIP_VLT"
echo ""

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
TOTAL=$((PASS + FAIL + SKIP))
echo "============================================"
echo " RESULTS"
echo "============================================"
echo "  Total : $TOTAL"
echo "  Pass  : $PASS"
echo "  Fail  : $FAIL"
echo "  Skip  : $SKIP"
echo ""

if [ "$FAIL" -gt 0 ]; then
    echo "ERROR: $FAIL test(s) failed."
    exit 1
fi

if [ "$PASS" -eq 0 ]; then
    echo "WARNING: No tests were able to run (all skipped)."
    exit 2
fi

echo "All tests passed."
exit 0
