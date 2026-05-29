#!/bin/bash
set -e

RD=/home/marco/programming/rv/herve/dpi-riscv
OD=$RD/obj_dir

rm -rf "$OD"
mkdir -p "$OD"

# Run everything from the RD (project root)
cd "$RD"

# Verilator
verilator --timing --cc --trace --top-module tb_top_blinky -Mdir "$OD" \
  sim/harness/tb_top_blinky.sv \
  sim/bus/ahb_lite_bfm.sv \
  sim/dut/ahb_gpio.sv

echo "=== Verilator done ==="
ls "$OD/Vtb_top_blinky.h"

# ISS C files
cc -Isim/iss -I. -c sim/iss/rv32_dpi.c -o "$OD/rv32_dpi.o"
cc -Isim/iss -I. -c sim/iss/herve_profiler.c -o "$OD/herve_profiler.o"
echo "=== ISS done ==="

# Verilator support
VINC="-I$OD -I/usr/share/verilator/include -I/usr/share/verilator/include/vltstd"
VFLAGS="$VINC -DVM_COVERAGE=0 -DVM_SC=0 -DVM_TRACE=1 -DVM_TRACE_VCD=1 -faligned-new -fcf-protection=none -Os"
g++ $VFLAGS -c /usr/share/verilator/include/verilated.cpp -o "$OD/verilated.o"
g++ $VFLAGS -c /usr/share/verilator/include/verilated_dpi.cpp -o "$OD/verilated_dpi.o"
g++ $VFLAGS -c /usr/share/verilator/include/verilated_vcd_c.cpp -o "$OD/verilated_vcd_c.o"
g++ $VFLAGS -c /usr/share/verilator/include/verilated_threads.cpp -o "$OD/verilated_threads.o"
echo "=== Support done ==="

# Testbench
g++ $VFLAGS -Isim/iss -I. -DVL_DPIDECL_dpi_mmio_read_ -DVL_DPIDECL_dpi_mmio_write_ -DVL_DPIDECL_rv_set_irq_ \
  -c sim/harness/rv32_dpi_blinky_tb.cpp -o "$OD/rv32_dpi_blinky_tb.o"
echo "=== Testbench done ==="

# Generate ALL.cpp
python3 /usr/share/verilator/bin/verilator_includer -DVL_INCLUDE_OPT=include \
  "$OD/Vtb_top_blinky.cpp" \
  "$OD/Vtb_top_blinky___024root__DepSet_h6d8195d0__0.cpp" \
  "$OD/Vtb_top_blinky___024root__DepSet_h05da3ee6__0.cpp" \
  "$OD/Vtb_top_blinky__Dpi.cpp" \
  "$OD/Vtb_top_blinky__Trace__0.cpp" \
  "$OD/Vtb_top_blinky___024root__Slow.cpp" \
  "$OD/Vtb_top_blinky___024root__DepSet_h6d8195d0__0__Slow.cpp" \
  "$OD/Vtb_top_blinky___024root__DepSet_h05da3ee6__0__Slow.cpp" \
  "$OD/Vtb_top_blinky__Syms.cpp" \
  "$OD/Vtb_top_blinky__Trace__0__Slow.cpp" \
  "$OD/Vtb_top_blinky__TraceDecls__0__Slow.cpp" > "$OD/Vtb_top_blinky__ALL.cpp"

g++ $VFLAGS -c "$OD/Vtb_top_blinky__ALL.cpp" -o "$OD/Vtb_top_blinky__ALL.o"
echo "=== ALL done ==="

# Link
ar rcs "$OD/Vtb_top_blinky__ALL.a" "$OD/Vtb_top_blinky__ALL.o"
g++ -Os "$OD/rv32_dpi_blinky_tb.o" "$OD/verilated.o" "$OD/verilated_dpi.o" \
  "$OD/verilated_vcd_c.o" "$OD/verilated_threads.o" "$OD/Vtb_top_blinky__ALL.a" \
  "$OD/rv32_dpi.o" "$OD/herve_profiler.o" -pthread -lpthread -latomic -o "$OD/Vtb_top_blinky"
echo "=== LINK SUCCESS ==="
ls -la "$OD/Vtb_top_blinky"
