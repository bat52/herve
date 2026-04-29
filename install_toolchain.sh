#!/usr/bin/env bash
sudo apt update
sudo apt install gcc-riscv64-linux-gnu g++-riscv64-linux-gnu \
                 binutils-riscv64-linux-gnu \
                 gcc-riscv64-unknown-elf
sudo apt install verilator