VERILATOR ?= verilator
TOP = tb_top
SV = sim/harness/tb_top.sv
CPP = sim/harness/rv32_dpi_tb.cpp
C = sim/iss/rv32_dpi.c
INCDIR = -I sim/iss

all:
	$(VERILATOR) --cc --exe $(SV) $(CPP) $(C) $(INCDIR) --build

clean:
	rm -rf obj_dir *.o *.d *.dvi tb_top.vcd
