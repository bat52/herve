# DPI Verilator Harness

This harness demonstrates the RV32 DPI emulator in a minimal Verilator simulation.

## Build

From the repository root:

    cd dpi-riscv
    make

## Run

    ./obj_dir/Vtb_top

The harness writes a small program into shared RAM, executes it with `rv_step()`, and captures the MMIO write result in the SystemVerilog DPI-exported register.
