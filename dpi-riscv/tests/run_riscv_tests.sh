#!/bin/bash
#
# run_riscv_tests.sh — Build and run the riscv-tests validation suite.
#
# This script:
#   1. Clones riscv-tests (if not already present)
#   2. Builds the test binaries (if not already built)
#   3. Converts ELF to binary
#   4. Compiles the standalone test runner
#   5. Runs all tests against the ISS
#
# Usage:
#   ./tests/run_riscv_tests.sh              # run from dpi-riscv/
#   make run_riscv_tests                    # alternative via Makefile
#
# Prerequisites:
#   - RISC-V toolchain (riscv64-unknown-elf-*) in PATH
#   - g++ with C++11 support
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ISS_DIR="$PROJECT_DIR/sim/iss"
RISCV_TESTS_DIR="$PROJECT_DIR/../riscv-tests"
TEST_BIN_DIR="$RISCV_TESTS_DIR/isa/bin"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "============================================"
echo "  RISC-V Tests Validation Suite"
echo "============================================"
echo ""

# --------------------------------------------------
# Step 1: Check for riscv-tests
# --------------------------------------------------
if [ ! -d "$RISCV_TESTS_DIR" ]; then
    echo -e "${YELLOW}Cloning riscv-tests...${NC}"
    cd "$PROJECT_DIR/.."
    git clone https://github.com/riscv-software-src/riscv-tests.git
    cd "$RISCV_TESTS_DIR"
    git submodule update --init --recursive
else
    echo -e "${GREEN}riscv-tests directory found.${NC}"
fi

# --------------------------------------------------
# Step 2: Build test binaries if not already built
# --------------------------------------------------
if [ ! -f "$TEST_BIN_DIR/rv32ui-p-simple.bin" ]; then
    echo -e "${YELLOW}Building riscv-tests binaries...${NC}"
    mkdir -p "$TEST_BIN_DIR"
    cd "$RISCV_TESTS_DIR/isa"

    # Build RV32UI tests
    make XLEN=32 RISCV_PREFIX=riscv64-unknown-elf- -j$(nproc) 2>&1 | tail -5

    # Convert ELF to binary
    echo -e "${YELLOW}Converting ELF to binary...${NC}"
    for elf in "$RISCV_TESTS_DIR/isa/rv32ui-p-"*.elf \
               "$RISCV_TESTS_DIR/isa/rv32um-p-"*.elf \
               "$RISCV_TESTS_DIR/isa/rv32uc-p-"*.elf; do
        if [ -f "$elf" ]; then
            base="$(basename "$elf" .elf)"
            riscv64-unknown-elf-objcopy -O binary "$elf" "$TEST_BIN_DIR/$base.bin"
        fi
    done

    echo -e "${GREEN}Build complete.${NC}"
else
    echo -e "${GREEN}Test binaries already built.${NC}"
fi

# --------------------------------------------------
# Step 3: Count test binaries
# --------------------------------------------------
NUM_BINS=$(ls "$TEST_BIN_DIR"/*.bin 2>/dev/null | wc -l)
echo -e "Found ${YELLOW}$NUM_BINS${NC} test binaries."
echo ""

# --------------------------------------------------
# Step 4: Compile test runner
# --------------------------------------------------
echo -e "${YELLOW}Compiling test runner...${NC}"
cd "$PROJECT_DIR"
g++ -I"$ISS_DIR" -I"$PROJECT_DIR" -o rv32_dpi_riscv_tests \
    "$ISS_DIR/rv32_dpi_riscv_tests.cpp" \
    "$ISS_DIR/rv32_dpi.c"
echo -e "${GREEN}Compilation complete.${NC}"
echo ""

# --------------------------------------------------
# Step 5: Run tests
# --------------------------------------------------
echo -e "${YELLOW}Running tests...${NC}"
echo ""
./rv32_dpi_riscv_tests "$TEST_BIN_DIR"
EXIT_CODE=$?

echo ""
if [ $EXIT_CODE -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
else
    echo -e "${RED}Some tests failed.${NC}"
fi

exit $EXIT_CODE
