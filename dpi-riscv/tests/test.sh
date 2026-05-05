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

HAS_ICARUS=0
if command -v iverilog &>/dev/null && command -v vvp &>/dev/null; then
    HAS_ICARUS=1
    echo "  iverilog/vvp  : found"
else
    echo "  iverilog/vvp  : NOT found (Icarus VPI tests will be skipped)"
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
# 2. Standalone ISS tests (no Verilator)
# ---------------------------------------------------------------------------
echo "============================================"
echo " 2. Standalone ISS tests (no Verilator)"
echo "============================================"

SKIP_ISS=
[ "$HAS_GXX" -eq 0 ] && SKIP_ISS="no g++"

# Check if riscv-tests ISA ELF binaries exist; if not, try to build them
SKIP_RISCV_TESTS=
if [ "$HAS_GXX" -eq 0 ]; then
    SKIP_RISCV_TESTS="no g++"
elif [ "$HAS_TOOLCHAIN" -eq 0 ]; then
    SKIP_RISCV_TESTS="no RISC-V toolchain (needed to build riscv-tests ELFs)"
else
    # Check if at least one RV32 test ELF exists in ../riscv-tests/isa/
    if ! ls ../riscv-tests/isa/rv32ui-p-* &>/dev/null; then
        echo "  riscv-tests ELFs not found — attempting to build..."
        if make build_riscv_tests 2>&1; then
            echo "  riscv-tests built successfully."
        else
            echo "  riscv-tests build FAILED — skipping run_riscv_tests."
            SKIP_RISCV_TESTS="riscv-tests build failed"
        fi
    fi
fi

run_test "run_standalone"      "run_standalone"      "$SKIP_ISS"
run_test "run_c_test"          "run_c_test"          "$SKIP_ISS"
run_test "run_irq_standalone"  "run_irq_standalone"  "$SKIP_ISS"
run_test "run_riscv_tests"     "run_riscv_tests"     "$SKIP_RISCV_TESTS"
echo ""

# ---------------------------------------------------------------------------
# 3. Icarus Verilog VPI tests  (need toolchain + iverilog)
# ---------------------------------------------------------------------------
echo "============================================"
echo " 3. Icarus Verilog VPI tests"
echo "============================================"

SKIP_ICARUS=
[ "$HAS_TOOLCHAIN" -eq 0 ]  && SKIP_ICARUS="no RISC-V toolchain"
[ "$HAS_ICARUS" -eq 0 ]     && SKIP_ICARUS="no iverilog/vvp"

run_test "run_icarus"         "run_icarus"          "$SKIP_ICARUS"
echo ""

# ---------------------------------------------------------------------------
# 4. Verilator-based tests  (need g++ + toolchain + Verilator)
# ---------------------------------------------------------------------------
echo "============================================"
echo " 4. Verilator-based tests"
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
