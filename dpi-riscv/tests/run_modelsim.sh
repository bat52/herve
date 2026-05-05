#!/bin/bash
#
# run_modelsim.sh — Run all ModelSim DPI-C tests and report results.
#
# This script:
#   1. Detects available tools (ModelSim, gcc, RISC-V toolchain)
#   2. Builds firmware binaries (needs RISC-V toolchain)
#   3. Compiles the DPI-C shared library (needs gcc)
#   4. Runs each ModelSim testbench in command-line mode
#   5. Prints a PASS/FAIL/SKIP summary
#
# Usage:
#   ./tests/run_modelsim.sh
#
# Environment variables:
#   INTELFPGA_DIR  — Path to Intel FPGA installation root (default: $HOME/intelFPGA)
#   MODELSIM_DIR   — Path to ModelSim installation (takes precedence over INTELFPGA_DIR)
#   RISCV_PREFIX   — RISC-V toolchain prefix (default: riscv64-unknown-elf)
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
# Helper: run a single test and record pass/fail/skip.
#   $1 : descriptive name
#   $2 : firmware binary (empty = none needed)
#   $3 : SV testbench file
#   $4 : reason to skip (empty = don't skip)
# ---------------------------------------------------------------------------
run_test() {
    local name="$1"
    local firmware="$2"
    local tb_file="$3"
    local skip_reason="$4"

    if [ -n "$skip_reason" ]; then
        echo "  [SKIP] $name  ($skip_reason)"
        SKIP=$((SKIP + 1))
        return
    fi

    echo "  [RUN ] $name"

    # Create a temporary working directory for this test
    local work_dir
    work_dir=$(mktemp -d)

    cd "$work_dir" || return 1

    # Copy firmware if needed
    if [ -n "$firmware" ] && [ -f "$ORIG_DIR/$firmware" ]; then
        cp "$ORIG_DIR/$firmware" .
    fi

    # Copy the shared library
    if [ -f "$ORIG_DIR/$SO_FILE" ]; then
        cp "$ORIG_DIR/$SO_FILE" .
    fi

    # Create work library
    if ! "$VLIB" work 2>&1; then
        echo "  [FAIL] $name  (vlib failed)"
        FAIL=$((FAIL + 1))
        cd "$ORIG_DIR"
        return
    fi

    # Compile the SV testbench
    if ! "$VLOG" "$ORIG_DIR/$tb_file" 2>&1; then
        echo "  [FAIL] $name  (vlog failed)"
        FAIL=$((FAIL + 1))
        cd "$ORIG_DIR"
        return
    fi

    # Extract module name from filename (strip .sv)
    local module_name
    module_name=$(basename "$tb_file" .sv)

    # Run simulation
    local sim_log="sim.log"
    if "$VSIM" -c -sv_lib "$(basename "$SO_FILE" .so)" -do "run -all; quit" "$module_name" > "$sim_log" 2>&1; then
        # Check for RESULT: PASS in the log
        if grep -q "RESULT: PASS" "$sim_log"; then
            echo "  [PASS] $name"
            PASS=$((PASS + 1))
        else
            echo "  [FAIL] $name  (simulation reported failure)"
            echo "  --- Last 20 lines of simulation log ---"
            tail -20 "$sim_log" | sed 's/^/  /'
            echo "  ----------------------------------------"
            FAIL=$((FAIL + 1))
        fi
    else
        echo "  [FAIL] $name  (vsim exited with error)"
        echo "  --- Last 20 lines of simulation log ---"
        tail -20 "$sim_log" | sed 's/^/  /'
        echo "  ----------------------------------------"
        FAIL=$((FAIL + 1))
    fi

    cd "$ORIG_DIR"
}

# ---------------------------------------------------------------------------
# Tool detection
# ---------------------------------------------------------------------------
echo "============================================"
echo " run_modelsim.sh — Tool detection"
echo "============================================"

ORIG_DIR=$(pwd)

# ModelSim detection
VLIB=""
VLOG=""
VSIM=""
HAS_MODELSIM=0

# Priority 1: MODELSIM_DIR explicitly set by user
if [ -n "${MODELSIM_DIR:-}" ]; then
    if [ -x "$MODELSIM_DIR/vsim" ]; then
        export PATH="$MODELSIM_DIR:$PATH"
        VLIB="vlib"
        VLOG="vlog"
        VSIM="vsim"
        HAS_MODELSIM=1
        echo "  ModelSim     : found ($MODELSIM_DIR)"
    else
        echo "  ModelSim     : NOT found at MODELSIM_DIR=$MODELSIM_DIR"
    fi
fi

# Priority 2: INTELFPGA_DIR — auto-discover latest version
if [ "$HAS_MODELSIM" -eq 0 ]; then
    INTELFPGA_DIR="${INTELFPGA_DIR:-$HOME/intelFPGA}"
    if [ -d "$INTELFPGA_DIR" ]; then
        # Find the latest version subdirectory (sorted numerically, descending)
        latest_ver=$(ls -1 "$INTELFPGA_DIR" 2>/dev/null | grep -E '^[0-9]+\.[0-9]+$' | sort -t. -k1,1nr -k2,2nr | head -1)
        if [ -n "$latest_ver" ]; then
            modelsim_bin="$INTELFPGA_DIR/$latest_ver/modelsim_ase/bin"
            if [ -x "$modelsim_bin/vsim" ]; then
                export PATH="$modelsim_bin:$PATH"
                VLIB="vlib"
                VLOG="vlog"
                VSIM="vsim"
                HAS_MODELSIM=1
                echo "  ModelSim     : found ($modelsim_bin)"
            fi
        fi
    fi
fi

# Priority 3: Try common hardcoded paths (fallback)
if [ "$HAS_MODELSIM" -eq 0 ]; then
    MODELSIM_CANDIDATES=(
        "/opt/intelFPGA/20.1/modelsim_ase/linux_x86_64"
        "/opt/intelFPGA/20.1/modelsim_ae/linux_x86_64"
        "/opt/intelFPGA/19.1/modelsim_ase/linux_x86_64"
        "/opt/intelFPGA/19.1/modelsim_ae/linux_x86_64"
        "/opt/modelsim/modeltech/linux_x86_64"
        "/usr/local/modelsim/modeltech/linux_x86_64"
    )
    for dir in "${MODELSIM_CANDIDATES[@]}"; do
        if [ -x "$dir/vsim" ]; then
            export PATH="$dir:$PATH"
            VLIB="vlib"
            VLOG="vlog"
            VSIM="vsim"
            HAS_MODELSIM=1
            echo "  ModelSim     : found ($dir)"
            break
        fi
    done
fi

# Priority 4: vsim in PATH
if [ "$HAS_MODELSIM" -eq 0 ]; then
    if command -v vsim &>/dev/null; then
        VSIM="vsim"
        VLOG="vlog"
        VLIB="vlib"
        HAS_MODELSIM=1
        echo "  ModelSim     : found (in PATH)"
    else
        echo "  ModelSim     : NOT found (all tests will be skipped)"
    fi
fi

# Verify ModelSim tools are actually executable (not just symlinks to vco)
if [ "$HAS_MODELSIM" -eq 1 ]; then
    # Run vlib with no args — exit code 126 or 127 means the binary
    # can't execute at all (e.g. missing 32-bit libraries).
    # Exit code 1 (usage) means it works fine.
    "$VLIB" 2>/dev/null
    rc=$?
    if [ "$rc" -eq 126 ] || [ "$rc" -eq 127 ]; then
        echo "  WARNING: ModelSim tools found but cannot execute (missing 32-bit libraries?)"
        echo "           Install 32-bit compatibility libraries:"
        echo "             sudo dpkg --add-architecture i386"
        echo "             sudo apt-get update"
        echo "             sudo apt-get install libc6:i386 lib32stdc++6"
        HAS_MODELSIM=0
    fi
fi

# gcc detection
HAS_GCC=0
if command -v gcc &>/dev/null; then
    HAS_GCC=1
    echo "  gcc          : found"
else
    echo "  gcc          : NOT found (shared library build will be skipped)"
fi

# RISC-V toolchain detection
HAS_TOOLCHAIN=0
RISCV_PREFIX="${RISCV_PREFIX:-riscv64-unknown-elf}"
if command -v "${RISCV_PREFIX}-as" &>/dev/null; then
    HAS_TOOLCHAIN=1
    echo "  RISC-V asm   : found ($RISCV_PREFIX)"
else
    echo "  RISC-V asm   : NOT found (firmware build will be skipped)"
fi

echo ""

# ---------------------------------------------------------------------------
# Determine skip reasons
# ---------------------------------------------------------------------------
SKIP_ALL=
[ "$HAS_MODELSIM" -eq 0 ] && SKIP_ALL="no ModelSim"
[ "$HAS_GCC" -eq 0 ]      && SKIP_ALL="no gcc"
[ "$HAS_TOOLCHAIN" -eq 0 ] && SKIP_ALL="no RISC-V toolchain"

# ---------------------------------------------------------------------------
# Build firmware binaries
# ---------------------------------------------------------------------------
if [ "$HAS_TOOLCHAIN" -eq 1 ]; then
    echo "============================================"
    echo " Building firmware binaries"
    echo "============================================"
    echo ""

    # Build all firmware targets
    for fw_target in firmware firmware_muldiv firmware_irq firmware_ahb firmware_mmio_regs; do
        echo "  Building $fw_target..."
        if make "$fw_target" 2>&1; then
            echo "    $fw_target: OK"
        else
            echo "    $fw_target: FAILED"
        fi
    done
    echo ""
else
    echo "============================================"
    echo " WARNING: Cannot build firmware binaries"
    echo " (no RISC-V toolchain). Tests will be skipped."
    echo "============================================"
    echo ""
fi

# ---------------------------------------------------------------------------
# Build DPI-C shared library
# ---------------------------------------------------------------------------
SO_FILE="sim/modelsim/rv32_dpi_mti.so"
if [ "$HAS_GCC" -eq 1 ]; then
    echo "============================================"
    echo " Building DPI-C shared library"
    echo "============================================"
    echo ""

    echo "  Compiling $SO_FILE..."
    if gcc -shared -fPIC -m32 -I./sim/iss -o "$SO_FILE" \
        sim/modelsim/rv32_dpi_mti.c sim/iss/rv32_dpi.c 2>&1; then
        echo "  Shared library: OK"
    else
        echo "  Shared library: FAILED"
        if [ -z "$SKIP_ALL" ]; then
            SKIP_ALL="shared library build failed"
        fi
    fi
    echo ""
fi

# ---------------------------------------------------------------------------
# Run tests
# ---------------------------------------------------------------------------
echo "============================================"
echo " Running ModelSim DPI-C tests"
echo "============================================"
echo ""

# Test 1: Basic MMIO smoke test
run_test "basic" \
    "firmware.bin" \
    "sim/modelsim/tb_modelsim_basic.sv" \
    "$SKIP_ALL"

# Test 2: MMIO register config test
run_test "mmio_regs" \
    "firmware_mmio_regs.bin" \
    "sim/modelsim/tb_modelsim_mmio_regs.sv" \
    "$SKIP_ALL"

# Test 3: MUL/DIV extension test
run_test "muldiv" \
    "firmware_muldiv.bin" \
    "sim/modelsim/tb_modelsim_muldiv.sv" \
    "$SKIP_ALL"

# Test 4: IRQ + WFI test
run_test "irq" \
    "firmware_irq.bin" \
    "sim/modelsim/tb_modelsim_irq.sv" \
    "$SKIP_ALL"

# Test 5: AHB GPIO self-test
run_test "ahb" \
    "firmware_ahb.bin" \
    "sim/modelsim/tb_modelsim_ahb.sv" \
    "$SKIP_ALL"

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
TOTAL=$((PASS + FAIL + SKIP))
echo ""
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
