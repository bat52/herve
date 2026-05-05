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

# IRQ testcase
FIRMWARE_IRQ_S = firmware_irq.S
FIRMWARE_IRQ_BIN = firmware_irq.bin
FIRMWARE_IRQ_O = firmware_irq.o
FIRMWARE_IRQ_ELF = firmware_irq.elf
CPP_IRQ      = sim/harness/rv32_dpi_irq_tb.cpp

# AHB-Lite GPIO testcase
FIRMWARE_AHB_S = firmware_ahb.S
FIRMWARE_AHB_BIN = firmware_ahb.bin
FIRMWARE_AHB_O = firmware_ahb.o
FIRMWARE_AHB_ELF = firmware_ahb.elf
CPP_AHB      = sim/harness/rv32_dpi_ahb_tb.cpp
SV_AHB       = sim/harness/tb_top_ahb.sv
BUS_SV       = sim/bus/ahb_lite_bfm.sv
DUT_SV       = sim/dut/ahb_gpio.sv

C = sim/iss/rv32_dpi.c

.PHONY: all clean run run_fast run_debug run_mmio_regs run_muldiv run_irq run_ahb firmware firmware_muldiv firmware_irq firmware_ahb run_benchmark run_spike_benchmark compare_benchmark build_riscv_tests run_spike_benchmark_mm run_spike_benchmark_mm_csv compare_benchmark_mm run_spike_benchmark_memcpy run_spike_benchmark_memcpy_csv compare_benchmark_memcpy run_benchmark_dhrystone run_benchmark_dhrystone_csv run_spike_benchmark_dhrystone run_spike_benchmark_dhrystone_csv compare_benchmark_dhrystone

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

# ---- IRQ firmware ----
$(FIRMWARE_IRQ_O): $(FIRMWARE_IRQ_S)
	$(RISCV_PREFIX)-as -march=rv32im_zicsr -o $@ $<

$(FIRMWARE_IRQ_ELF): $(FIRMWARE_IRQ_O)
	$(RISCV_PREFIX)-ld -m elf32lriscv -Ttext=0x00000000 -o $@ $<

$(FIRMWARE_IRQ_BIN): $(FIRMWARE_IRQ_ELF)
	$(RISCV_PREFIX)-objcopy -O binary $< $@

firmware_irq: $(FIRMWARE_IRQ_BIN)

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
	$(VERILATOR) --cc --exe --build -j $(shell nproc 2>/dev/null || echo 4) --trace \
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

# ---- IRQ testcase (Verilator) ----
obj_dir/V$(TOP)_irq: $(SV) $(CPP_IRQ) $(C) $(FIRMWARE_IRQ_BIN)
	mkdir -p obj_dir
	$(CC) $(CFLAGS) -I$(CURDIR)/sim/iss -I$(CURDIR) -c $(C) -o obj_dir/rv32_dpi.o
	$(VERILATOR) --cc --exe --build -j $(shell nproc 2>/dev/null || echo 4) --trace \
	  -CFLAGS "-I$(CURDIR)/sim/iss -I$(CURDIR) -DVL_DPIDECL_dpi_mmio_read_ -DVL_DPIDECL_dpi_mmio_write_ -DVL_DPIDECL_rv_set_irq_" \
	  -LDFLAGS "$(shell pwd)/obj_dir/rv32_dpi.o" \
	  -o V$(TOP)_irq \
	  $(SV) $(CPP_IRQ)

run_irq: firmware_irq obj_dir/V$(TOP)_irq
	./obj_dir/V$(TOP)_irq

# ---- MMIO regs firmware ----
FIRMWARE_MMIO_REGS_S = firmware_mmio_regs.S
FIRMWARE_MMIO_REGS_BIN = firmware_mmio_regs.bin
FIRMWARE_MMIO_REGS_O = firmware_mmio_regs.o
FIRMWARE_MMIO_REGS_ELF = firmware_mmio_regs.elf

$(FIRMWARE_MMIO_REGS_O): $(FIRMWARE_MMIO_REGS_S)
	$(RISCV_PREFIX)-as -march=rv32im -o $@ $<

$(FIRMWARE_MMIO_REGS_ELF): $(FIRMWARE_MMIO_REGS_O)
	$(RISCV_PREFIX)-ld -m elf32lriscv -Ttext=0x00000000 -o $@ $<

$(FIRMWARE_MMIO_REGS_BIN): $(FIRMWARE_MMIO_REGS_ELF)
	$(RISCV_PREFIX)-objcopy -O binary $< $@

firmware_mmio_regs: $(FIRMWARE_MMIO_REGS_BIN)

# ---- AHB firmware ----
$(FIRMWARE_AHB_O): $(FIRMWARE_AHB_S)

	$(RISCV_PREFIX)-as -march=rv32im_zicsr -o $@ $<

$(FIRMWARE_AHB_ELF): $(FIRMWARE_AHB_O)
	$(RISCV_PREFIX)-ld -m elf32lriscv -Ttext=0x00000000 -o $@ $<

$(FIRMWARE_AHB_BIN): $(FIRMWARE_AHB_ELF)
	$(RISCV_PREFIX)-objcopy -O binary $< $@

firmware_ahb: $(FIRMWARE_AHB_BIN)

# ---- AHB-Lite GPIO testcase (Verilator) ----
obj_dir/V$(TOP)_ahb: $(SV_AHB) $(BUS_SV) $(DUT_SV) $(CPP_AHB) $(C) $(FIRMWARE_AHB_BIN)
	mkdir -p obj_dir
	$(CC) $(CFLAGS) -I$(CURDIR)/sim/iss -I$(CURDIR) -c $(C) -o obj_dir/rv32_dpi.o
	$(VERILATOR) --timing --cc --exe --build -j $(shell nproc 2>/dev/null || echo 4) --trace \
	  -CFLAGS "-I$(CURDIR)/sim/iss -I$(CURDIR) -DVL_DPIDECL_dpi_mmio_read_ -DVL_DPIDECL_dpi_mmio_write_" \
	  -LDFLAGS "$(shell pwd)/obj_dir/rv32_dpi.o" \
	  -o V$(TOP)_ahb \
	  $(SV_AHB) $(BUS_SV) $(DUT_SV) $(CPP_AHB)


run_ahb: firmware_ahb obj_dir/V$(TOP)_ahb
	./obj_dir/V$(TOP)_ahb

# ---- Icarus Verilog VPI smoke test (no Verilator) ----
ICARUS = iverilog
VVP = vvp
VPI_SRC = sim/vpi/rv32_dpi_vpi.c
VPI_VPI = obj_dir/rv32_dpi_vpi.vpi
TB_ICARUS_SV = sim/icarus/tb_icarus_vpi.sv

TB_ICARUS_VVP = obj_dir/tb_icarus.vvp

$(VPI_VPI): $(VPI_SRC) $(C)
	mkdir -p obj_dir
	$(CC) $(CFLAGS) -shared -fPIC -I$(CURDIR)/sim/iss -I/usr/include/iverilog \
	  -o $@ $(VPI_SRC) $(C)

$(TB_ICARUS_VVP): $(TB_ICARUS_SV)
	mkdir -p obj_dir
	$(ICARUS) -g2012 -o $@ $(TB_ICARUS_SV)

run_icarus: firmware $(VPI_VPI) $(TB_ICARUS_VVP)
	$(VVP) -Mobj_dir -mrv32_dpi_vpi $(TB_ICARUS_VVP)

.PHONY: run_icarus

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

# ---- Standalone IRQ test (no Verilator, no RISC-V toolchain) ----
rv32_dpi_irq_test: sim/iss/rv32_dpi_irq_test.cpp sim/iss/rv32_dpi.c
	g++ -I./sim/iss -I. -o $@ sim/iss/rv32_dpi_irq_test.cpp sim/iss/rv32_dpi.c

run_irq_standalone: rv32_dpi_irq_test
	./rv32_dpi_irq_test

# ---- Build riscv-tests ISA ELF binaries (needs RISC-V toolchain) ----
# We build only the "p" (physical) test variants: rv32ui-p-*, rv32um-p-*, rv32uc-p-*.
# The "v" (virtual-memory) variants require string.h from newlib and are
# not needed by our test runner.  We build the individual p-test binaries
# directly rather than using the rv32ui/rv32um/rv32uc targets (which would
# also try to build the failing v-variants).
RISCV_TESTS_ISA_DIR = ../riscv-tests/isa

# Collect all p-test targets from the riscv-tests Makefrags
RV32UI_P_TESTS = $(addprefix rv32ui-p-, simple add addi and andi auipc beq bge bgeu blt bltu bne fence_i jal jalr lb lbu lh lhu lw ld_st lui ma_data or ori sb sh sw st_ld sll slli slt slti sltiu sltu sra srai srl srli sub xor xori)
RV32UM_P_TESTS = $(addprefix rv32um-p-, mul mulh mulhsu mulhu div divu rem remu)
RV32UC_P_TESTS = $(addprefix rv32uc-p-, rvc)

build_riscv_tests:
	$(MAKE) -C $(RISCV_TESTS_ISA_DIR) XLEN=32 $(RV32UI_P_TESTS) $(RV32UM_P_TESTS) $(RV32UC_P_TESTS)

# ---- riscv-tests validation (no Verilator, no RISC-V toolchain) ----
rv32_dpi_riscv_tests: sim/iss/rv32_dpi_riscv_tests.cpp sim/iss/rv32_dpi.c
	g++ -I./sim/iss -I. -o $@ sim/iss/rv32_dpi_riscv_tests.cpp sim/iss/rv32_dpi.c

run_riscv_tests: build_riscv_tests rv32_dpi_riscv_tests
	./rv32_dpi_riscv_tests

# ---- Benchmark: Herve ISS vs Spike comparison ----
rv32_dpi_benchmark: sim/iss/rv32_dpi_benchmark.cpp sim/iss/rv32_dpi.c
	g++ -std=c++11 -I./sim/iss -I. -O2 -o $@ sim/iss/rv32_dpi_benchmark.cpp sim/iss/rv32_dpi.c

run_benchmark: rv32_dpi_benchmark
	./rv32_dpi_benchmark

run_benchmark_csv: rv32_dpi_benchmark
	./rv32_dpi_benchmark --csv

run_spike_benchmark:
	bash tests/run_spike_benchmark.sh

run_spike_benchmark_csv:
	bash tests/run_spike_benchmark.sh --csv

compare_benchmark: rv32_dpi_benchmark
	python3 tests/analyze_benchmark.py

# ---- HTIF-based benchmark (riscv-tests benchmarks) ----
RISCV_TESTS_DIR ?= ../riscv-tests
BMARKS_DIR = $(RISCV_TESTS_DIR)/benchmarks
COMMON_DIR = $(BMARKS_DIR)/common
ENV_DIR = $(RISCV_TESTS_DIR)/env

median.riscv: $(BMARKS_DIR)/median/median_main.c $(BMARKS_DIR)/median/median.c $(BMARKS_DIR)/median/median.h $(BMARKS_DIR)/median/dataset1.h $(COMMON_DIR)/crt.S $(COMMON_DIR)/syscalls.c $(COMMON_DIR)/util.h $(COMMON_DIR)/test.ld $(ENV_DIR)/encoding.h
	$(RISCV_PREFIX)-gcc -march=rv32im_zicsr -mabi=ilp32 -DPREALLOCATE=1 -static -std=gnu99 -O2 -ffast-math -fno-common -fno-builtin-printf -nostdlib -nostartfiles \
	  -I$(BMARKS_DIR) -I$(BMARKS_DIR)/median -I$(COMMON_DIR) -I$(ENV_DIR) -Inewlib-stubs \
	  -T $(COMMON_DIR)/test.ld \
	  -o $@ $(BMARKS_DIR)/median/median_main.c $(BMARKS_DIR)/median/median.c $(COMMON_DIR)/crt.S $(COMMON_DIR)/syscalls.c -lgcc

softfloat.o: softfloat.c
	$(RISCV_PREFIX)-gcc -march=rv32im_zicsr -mabi=ilp32 -static -O2 -nostdlib -nostartfiles -ffreestanding -c -o $@ $<

sync_stubs.o: sync_stubs.c
	$(RISCV_PREFIX)-gcc -march=rv32im_zicsr -mabi=ilp32 -static -O2 -nostdlib -nostartfiles -c -o $@ $<

alloca.o: newlib-stubs/alloca.c
	$(RISCV_PREFIX)-gcc -march=rv32im_zicsr -mabi=ilp32 -static -O2 -nostdlib -nostartfiles -ffreestanding -Inewlib-stubs -c -o $@ $<

mm.riscv: $(BMARKS_DIR)/mm/mm_main.c $(BMARKS_DIR)/mm/mm.c $(BMARKS_DIR)/mm/common.h $(BMARKS_DIR)/mm/rb.h $(COMMON_DIR)/crt.S $(COMMON_DIR)/syscalls.c $(COMMON_DIR)/util.h $(COMMON_DIR)/test.ld $(ENV_DIR)/encoding.h softfloat.o sync_stubs.o
	$(RISCV_PREFIX)-gcc -march=rv32im_zicsr -mabi=ilp32 -DPREALLOCATE=1 -static -std=gnu99 -O2 -ffast-math -fno-common -fno-builtin-printf -nostdlib -nostartfiles \
	  -I$(BMARKS_DIR) -I$(BMARKS_DIR)/mm -I$(COMMON_DIR) -I$(ENV_DIR) -Inewlib-stubs \
	  -T $(COMMON_DIR)/test.ld \
	  -o $@ $(BMARKS_DIR)/mm/mm_main.c $(BMARKS_DIR)/mm/mm.c $(COMMON_DIR)/crt.S $(COMMON_DIR)/syscalls.c softfloat.o sync_stubs.o -lgcc

memcpy.riscv: $(BMARKS_DIR)/memcpy/memcpy_main.c $(BMARKS_DIR)/memcpy/dataset1.h $(COMMON_DIR)/crt.S $(COMMON_DIR)/syscalls.c $(COMMON_DIR)/util.h $(COMMON_DIR)/test.ld $(ENV_DIR)/encoding.h
	$(RISCV_PREFIX)-gcc -march=rv32im_zicsr -mabi=ilp32 -DPREALLOCATE=1 -static -std=gnu99 -O2 -ffast-math -fno-common -fno-builtin-printf -nostdlib -nostartfiles \
	  -I$(BMARKS_DIR) -I$(BMARKS_DIR)/memcpy -I$(COMMON_DIR) -I$(ENV_DIR) -Inewlib-stubs \
	  -T $(COMMON_DIR)/test.ld \
	  -o $@ $(BMARKS_DIR)/memcpy/memcpy_main.c $(COMMON_DIR)/crt.S $(COMMON_DIR)/syscalls.c -lgcc

rv32_dpi_benchmark_htif: sim/iss/rv32_dpi_benchmark_htif.cpp sim/iss/rv32_dpi.c
	g++ -std=c++11 -I./sim/iss -I. -O2 -o $@ sim/iss/rv32_dpi_benchmark_htif.cpp sim/iss/rv32_dpi.c

run_benchmark_htif: median.riscv mm.riscv memcpy.riscv rv32_dpi_benchmark_htif
	./rv32_dpi_benchmark_htif

run_benchmark_htif_csv: median.riscv mm.riscv memcpy.riscv rv32_dpi_benchmark_htif
	./rv32_dpi_benchmark_htif --csv

run_spike_benchmark_median: median.riscv
	bash tests/run_spike_benchmark.sh --benchmark median.riscv

run_spike_benchmark_median_csv: median.riscv
	bash tests/run_spike_benchmark.sh --benchmark median.riscv --csv

run_spike_benchmark_mm: mm.riscv
	bash tests/run_spike_benchmark.sh --benchmark mm.riscv

run_spike_benchmark_mm_csv: mm.riscv
	bash tests/run_spike_benchmark.sh --benchmark mm.riscv --csv

compare_benchmark_htif: rv32_dpi_benchmark_htif median.riscv
	./rv32_dpi_benchmark_htif --csv > herve_benchmark_htif.csv
	bash tests/run_spike_benchmark.sh --benchmark median.riscv --csv > spike_benchmark_htif.csv
	python3 tests/analyze_benchmark.py herve_benchmark_htif.csv spike_benchmark_htif.csv

compare_benchmark_mm: rv32_dpi_benchmark_htif mm.riscv
	./rv32_dpi_benchmark_htif --csv > herve_benchmark_mm.csv
	bash tests/run_spike_benchmark.sh --benchmark mm.riscv --csv > spike_benchmark_mm.csv
	python3 tests/analyze_benchmark.py herve_benchmark_mm.csv spike_benchmark_mm.csv

run_spike_benchmark_memcpy: memcpy.riscv
	bash tests/run_spike_benchmark.sh --benchmark memcpy.riscv

run_spike_benchmark_memcpy_csv: memcpy.riscv
	bash tests/run_spike_benchmark.sh --benchmark memcpy.riscv --csv

compare_benchmark_memcpy: rv32_dpi_benchmark_htif memcpy.riscv
	./rv32_dpi_benchmark_htif --csv > herve_benchmark_memcpy.csv
	bash tests/run_spike_benchmark.sh --benchmark memcpy.riscv --csv > spike_benchmark_memcpy.csv
	python3 tests/analyze_benchmark.py herve_benchmark_memcpy.csv spike_benchmark_memcpy.csv

# ---- Dhrystone benchmark ----
dhrystone.riscv: $(BMARKS_DIR)/dhrystone/dhrystone_main.c $(BMARKS_DIR)/dhrystone/dhrystone.c $(BMARKS_DIR)/dhrystone/dhrystone.h $(COMMON_DIR)/crt.S $(COMMON_DIR)/syscalls.c $(COMMON_DIR)/util.h $(COMMON_DIR)/test.ld $(ENV_DIR)/encoding.h alloca.o
	$(RISCV_PREFIX)-gcc -march=rv32im_zicsr -mabi=ilp32 -DPREALLOCATE=1 -static -std=gnu99 -O2 -ffast-math -fno-common -fno-builtin-printf -nostdlib -nostartfiles \
	  -I$(BMARKS_DIR) -I$(BMARKS_DIR)/dhrystone -I$(COMMON_DIR) -I$(ENV_DIR) -Inewlib-stubs \
	  -T $(COMMON_DIR)/test.ld \
	  -o $@ $(BMARKS_DIR)/dhrystone/dhrystone_main.c $(BMARKS_DIR)/dhrystone/dhrystone.c $(COMMON_DIR)/crt.S $(COMMON_DIR)/syscalls.c alloca.o -lgcc

run_benchmark_dhrystone: dhrystone.riscv rv32_dpi_benchmark_htif
	./rv32_dpi_benchmark_htif

run_benchmark_dhrystone_csv: dhrystone.riscv rv32_dpi_benchmark_htif
	./rv32_dpi_benchmark_htif --csv

run_spike_benchmark_dhrystone: dhrystone.riscv
	bash tests/run_spike_benchmark.sh --benchmark dhrystone.riscv

run_spike_benchmark_dhrystone_csv: dhrystone.riscv
	bash tests/run_spike_benchmark.sh --benchmark dhrystone.riscv --csv

compare_benchmark_dhrystone: rv32_dpi_benchmark_htif dhrystone.riscv
	./rv32_dpi_benchmark_htif --csv > herve_benchmark_dhrystone.csv
	bash tests/run_spike_benchmark.sh --benchmark dhrystone.riscv --csv > spike_benchmark_dhrystone.csv
	python3 tests/analyze_benchmark.py herve_benchmark_dhrystone.csv spike_benchmark_dhrystone.csv

clean:
	rm -rf obj_dir *.o *.d *.dvi *.vcd *.vcd~ firmware.elf firmware.bin firmware.o firmware_muldiv.elf firmware_muldiv.bin firmware_muldiv.o firmware_irq.elf firmware_irq.bin firmware_irq.o firmware_ahb.elf firmware_ahb.bin firmware_ahb.o firmware_mmio_regs.elf firmware_mmio_regs.bin firmware_mmio_regs.o rv32_dpi_test rv32_dpi_irq_test rv32_dpi_riscv_tests rv32_dpi_benchmark herve_benchmark.csv spike_benchmark.csv median.riscv median.riscv.dump herve_benchmark_htif.csv spike_benchmark_htif.csv rv32_dpi_benchmark_htif mm.riscv mm.riscv.dump herve_benchmark_mm.csv spike_benchmark_mm.csv memcpy.riscv memcpy.riscv.dump herve_benchmark_memcpy.csv spike_benchmark_memcpy.csv softfloat.o sync_stubs.o alloca.o dhrystone.riscv dhrystone.riscv.dump herve_benchmark_dhrystone.csv spike_benchmark_dhrystone.csv



