VERILATOR ?= verilator
TOP = tb_top
SV = sim/harness/tb_top.sv
CPP = sim/harness/rv32_dpi_tb.cpp
C = sim/iss/rv32_dpi.c

.PHONY: all clean run

all:
	$(VERILATOR) --cc --exe --build -j $(shell nproc 2>/dev/null || echo 4) --trace -CFLAGS "-I$(CURDIR)/sim/iss -I$(CURDIR)" $(SV) $(CPP) $(C)

run: all
	./obj_dir/V$(TOP)

clean:
	rm -rf obj_dir *.o *.d *.dvi *.vcd *.vcd~

