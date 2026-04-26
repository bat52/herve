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

C = sim/iss/rv32_dpi.c

.PHONY: all clean run run_mmio_regs firmware

all: run

# ---- Firmware ----
$(FIRMWARE_O): $(FIRMWARE_S)
	$(RISCV_PREFIX)-as -march=rv32im -o $@ $<

$(FIRMWARE_ELF): $(FIRMWARE_O)
	$(RISCV_PREFIX)-ld -m elf32lriscv -Ttext=0x00000000 -o $@ $<

$(FIRMWARE_BIN): $(FIRMWARE_ELF)
	$(RISCV_PREFIX)-objcopy -O binary $< $@

firmware: $(FIRMWARE_BIN)

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

clean:
	rm -rf obj_dir *.o *.d *.dvi *.vcd *.vcd~ firmware.elf firmware.bin firmware.o

