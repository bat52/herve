VERILATOR ?= verilator
TOP = tb_top
SV = sim/harness/tb_top.sv

# Firmware build
FIRMWARE_S = firmware.S
FIRMWARE_BIN = firmware.bin
FIRMWARE_O = firmware.o
FIRMWARE_ELF = firmware.elf

# RISC-V toolchain for firmware compilation
RISCV_PREFIX ?= riscv64-unknown-elf

# Original test
CPP_ORIG   = sim/harness/rv32_dpi_tb.cpp

# MMIO register configuration testcase
CPP_MMIO   = sim/harness/rv32_dpi_mmio_regs_tb.cpp

# MUL/DIV extension testcase
FIRMWARE_MULDIV_S = firmware_muldiv.S
FIRMWARE_MULDIV_BIN = firmware_muldiv.bin
FIRMWARE_MULDIV_O = firmware_muldiv.o
FIRMWARE_MULDIV_ELF = firmware_muldiv.elf
CPP_MULDIV  = sim/harness/rv32_dpi_muldiv_tb.cpp

C = sim/iss/rv32_dpi.c

.PHONY: all clean run run_fast run_debug run_mmio_regs run_muldiv firmware firmware_muldiv

all: run

# ---- Firmware ----
$(FIRMWARE_O): $(FIRMWARE_S)
	$(RISCV_PREFIX)-as -march=rv32im -o $@ $<

$(FIRMWARE_ELF): $(FIRMWARE_O)
	$(RISCV_PREFIX)-ld -m elf32lriscv -Ttext=0x00000000 -o $@ $<

$(FIRMWARE_BIN): $(FIRMWARE_ELF)
	$(RISCV_PREFIX)-objcopy -O binary $< $@

firmware: $(FIRMWARE_BIN)

# ---- MUL/DIV firmware ----
$(FIRMWARE_MULDIV_O): $(FIRMWARE_MULDIV_S)
	$(RISCV_PREFIX)-as -march=rv32im -o $@ $<

$(FIRMWARE_MULDIV_ELF): $(FIRMWARE_MULDIV_O)
	$(RISCV_PREFIX)-ld -m elf32lriscv -Ttext=0x00000000 -o $@ $<

$(FIRMWARE_MULDIV_BIN): $(FIRMWARE_MULDIV_ELF)
	$(RISCV_PREFIX)-objcopy -O binary $< $@

firmware_muldiv: $(FIRMWARE_MULDIV_BIN)

# ---- Original test ----
obj_dir/V$(TOP): $(SV) $(CPP_ORIG) $(C) $(FIRMWARE_BIN)
	mkdir -p obj_dir
	# Compile ISS separately - do NOT pass it via Verilator's --exe --build
	# as that would pull its DPI extern declarations into Vtb_top__ALL.cpp,
	# causing duplicate symbol errors with the wrappers in rv32_dpi_tb.cpp.
	$(CC) $(CFLAGS) -I$(CURDIR)/sim/iss -I$(CURDIR) -c $(C) -o obj_dir/rv32_dpi.o
	# Use -LDFLAGS with absolute path (Verilator's sub-make runs inside obj_dir/)
	$(VERILATOR) --cc --exe --build -j $(shell nproc 2>/dev/null || echo 4) --trace \
	  -CFLAGS "-I$(CURDIR)/sim/iss -I$(CURDIR) -DVL_DPIDECL_dpi_mmio_read_ -DVL_DPIDECL_dpi_mmio_write_" \
	  -LDFLAGS "$(shell pwd)/obj_dir/rv32_dpi.o" \
	  $(SV) $(CPP_ORIG)

run: obj_dir/V$(TOP) $(FIRMWARE_BIN)
	./obj_dir/V$(TOP)

# ---- Performance / debug variants ----
# Build without trace for faster simulation
obj_dir/V$(TOP)_fast: $(SV) $(CPP_ORIG) $(C) $(FIRMWARE_BIN)
	mkdir -p obj_dir
	$(CC) $(CFLAGS) -I$(CURDIR)/sim/iss -I$(CURDIR) -c $(C) -o obj_dir/rv32_dpi.o
	$(VERILATOR) --cc --exe --build -j $(shell nproc 2>/dev/null || echo 4) \
	  -CFLAGS "-I$(CURDIR)/sim/iss -I$(CURDIR) -DVL_DPIDECL_dpi_mmio_read_ -DVL_DPIDECL_dpi_mmio_write_" \
	  -LDFLAGS "$(shell pwd)/obj_dir/rv32_dpi.o" \
	  -o V$(TOP)_fast \
	  $(SV) $(CPP_ORIG)

run_fast: obj_dir/V$(TOP)_fast $(FIRMWARE_BIN)
	./obj_dir/V$(TOP)_fast 2>/dev/null

run_debug: obj_dir/V$(TOP) $(FIRMWARE_BIN)
	./obj_dir/V$(TOP) --trace

# ---- MMIO register config testcase ----
SV_MMIO = sim/harness/tb_top_mmio_regs.sv

obj_dir/V$(TOP)_mmio_regs: $(SV_MMIO) $(CPP_MMIO) $(C) $(FIRMWARE_BIN)
	mkdir -p obj_dir
	$(CC) $(CFLAGS) -I$(CURDIR)/sim/iss -I$(CURDIR) -c $(C) -o obj_dir/rv32_dpi.o
	$(VERILATOR) --cc --exe --build -j $(shell nproc 2>/dev/null || echo 4) --trace \
	  -CFLAGS "-I$(CURDIR)/sim/iss -I$(CURDIR) -DVL_DPIDECL_dpi_mmio_read_ -DVL_DPIDECL_dpi_mmio_write_" \
	  -LDFLAGS "$(shell pwd)/obj_dir/rv32_dpi.o" \
	  -o V$(TOP)_mmio_regs \
	  $(SV_MMIO) $(CPP_MMIO)

run_mmio_regs: obj_dir/V$(TOP)_mmio_regs
	./obj_dir/V$(TOP)_mmio_regs

# ---- MUL/DIV extension testcase ----
# Note: firmware is built programmatically in the C++ testbench,
# so no firmware binary dependency is needed.
obj_dir/V$(TOP)_muldiv: $(SV_MMIO) $(CPP_MULDIV) $(C)
	mkdir -p obj_dir
	$(CC) $(CFLAGS) -I$(CURDIR)/sim/iss -I$(CURDIR) -c $(C) -o obj_dir/rv32_dpi.o
	$(VERILATOR) --cc --exe --build -j $(shell nproc 2>/dev/null || echo 4) --trace \
	  -CFLAGS "-I$(CURDIR)/sim/iss -I$(CURDIR) -DVL_DPIDECL_dpi_mmio_read_ -DVL_DPIDECL_dpi_mmio_write_" \
	  -LDFLAGS "$(shell pwd)/obj_dir/rv32_dpi.o" \
	  -o V$(TOP)_muldiv \
	  $(SV_MMIO) $(CPP_MULDIV)

run_muldiv: obj_dir/V$(TOP)_muldiv
	./obj_dir/V$(TOP)_muldiv

# ---- Standalone ISS test (no Verilator, no RISC-V toolchain) ----
rv32_dpi_test: sim/iss/rv32_dpi_test.cpp sim/iss/rv32_dpi.c
	g++ -I./sim/iss -I. -o $@ sim/iss/rv32_dpi_test.cpp sim/iss/rv32_dpi.c

run_standalone: rv32_dpi_test
	./rv32_dpi_test

# ---- Standalone C-extension test (no Verilator, no RISC-V toolchain) ----
rv32_dpi_c_test: sim/iss/rv32_dpi_c_test.cpp sim/iss/rv32_dpi.c
	g++ -I./sim/iss -I. -o $@ sim/iss/rv32_dpi_c_test.cpp sim/iss/rv32_dpi.c

run_c_test: rv32_dpi_c_test
	./rv32_dpi_c_test

clean:
	rm -rf obj_dir *.o *.d *.dvi *.vcd *.vcd~ firmware.elf firmware.bin firmware.o firmware_muldiv.elf firmware_muldiv.bin firmware_muldiv.o rv32_dpi_test

