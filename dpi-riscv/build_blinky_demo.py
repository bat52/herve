#!/usr/bin/env python3
"""
Build and run the Zephyros Blinky Demo.

Uses the timer firmware (firmware_timer.S) with tb_top_zephyr.sv Verilator model.
Produces tb_top_blinky.vcd showing GPIO toggling (mem_write bit 0).

Usage:
    python3 build_blinky_demo.py          # Build and run
    python3 build_blinky_demo.py --run    # Run only (skip build)
"""
import os, subprocess, sys, glob

RD = os.path.dirname(os.path.abspath(__file__))
OD = os.path.join(RD, "obj_dir")

def run(cmd, **kw):
    print(f"  {cmd[:100]}...")
    return subprocess.run(cmd, shell=True, **kw)

def build():
    os.makedirs(OD, exist_ok=True)

    # 1. Firmware (firmware_timer.S already toggles GPIO)
    print("== Building firmware ==")
    run(f"riscv64-unknown-elf-as -march=rv32im_zicsr -o {OD}/fw.o {RD}/firmware_timer.S")
    run(f"riscv64-unknown-elf-ld -m elf32lriscv -Ttext=0x00000000 -o {OD}/fw.elf {OD}/fw.o")
    run(f"riscv64-unknown-elf-objcopy -O binary {OD}/fw.elf {RD}/firmware_timer.bin")

    # 2. Verilator
    print("== Running Verilator ==")
    run(f"verilator --cc --trace --top-module tb_top_zephyr -Mdir {OD} "
        f"{RD}/sim/harness/tb_top_zephyr.sv {RD}/sim/harness/rv32_dpi_zephyr_tb.cpp")

    # 3. Write standalone testbench
    print("== Writing testbench ==")
    tb_code = """#include "Vtb_top_zephyr.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "rv32_dpi.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static Vtb_top_zephyr *tb = nullptr;
static uint64_t sim_time = 0;
static VerilatedVcdC *g_tfp = 0;
static const uint64_t CLK_HALF_PERIOD = 5000;
static void tick() {
    tb->clk = !tb->clk; tb->eval(); sim_time += CLK_HALF_PERIOD;
    if (g_tfp) g_tfp->dump(sim_time);
}
extern "C" int dpi_mmio_read(int addr) {
    switch (addr) {
        case 0x10000000: return tb->mem_write;  // GPIO: return last written value
        case 0x10000010: return tb->mtime_lo;
        case 0x10000014: return tb->mtime_hi;
        case 0x10000018: return tb->mtimecmp_lo;
        case 0x1000001C: return tb->mtimecmp_hi;
        default: return 0;
    }
}
extern "C" void dpi_mmio_write(int addr, int data) {
    switch (addr) {
        case 0x10000000: tb->mem_write = data; break;
        case 0x10000018: tb->mtimecmp_lo = data; break;
        case 0x1000001C: tb->mtimecmp_hi = data; break;
        default: break;
    }
}
int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv); Verilated::traceEverOn(true);
    const char *fw = "firmware_timer.bin";
    int step = 1000, ticks = 250000;
    printf("=== ZEPHYROS BLINKY DEMO ===\\n");
    printf("Firmware: %s\\n", fw);
    printf("Clock ticks: %d\\n", ticks);
    tb = new Vtb_top_zephyr;
    VerilatedVcdC *tfp = new VerilatedVcdC;
    tb->trace(tfp, 5); tfp->open("tb_top_blinky.vcd"); g_tfp = tfp;
    rv_init(fw, 1 << 20); rv_reset(0);
    tb->clk = 0; tb->rstn = 0; tb->irq = 0;
    tb->mem_read = 0; tb->mem_write = 0;
    tb->mtimecmp_lo = 0xFFFFFFFF; tb->mtimecmp_hi = 0xFFFFFFFF;
    for (int i = 0; i < 8; i++) tick();
    tb->rstn = 1;
    int ex = rv_step(step);
    printf("Boot: %d instr, PC=0x%08x\\n", ex, rv_get_pc());
    printf("Timer period: %d ticks\\n", tb->mtimecmp_lo);
    int toggles = 0, last = 0;
    for (int tc = 1; tc <= ticks; tc++) {
        tick(); tick();
        for (int s = 0; s < 5; s++) { ex = rv_step(step); if (ex == 0) break; }
        int curr = tb->mem_write & 1;
        if (curr != last) {
            printf("  GPIO toggle %d at tick %d: mem_write[0] = %d\\n", ++toggles, tc, curr);
            last = curr;
        }
    }
    bool pass = (toggles >= 4);
    printf("\\n=== RESULTS ===\\n");
    printf("GPIO toggles: %d\\n", toggles);
    printf("VCD: tb_top_blinky.vcd (%s)\\n", 
           pass ? "open with: gtkwave tb_top_blinky.vcd" : "see above");
    printf("STATUS: %s\\n", pass ? "PASS" : "FAIL");
    tfp->close(); tb->final(); delete tb; delete tfp;
    return pass ? 0 : 1;
}
"""
    with open(f"{OD}/tb.cpp", "w") as f:
        f.write(tb_code)

    # 4. Compile
    print("== Compiling ==")
    VINC = f"-I{OD} -I/usr/share/verilator/include -I/usr/share/verilator/include/vltstd"
    VFLAGS = f"{VINC} -DVM_COVERAGE=0 -DVM_SC=0 -DVM_TRACE=1 -DVM_TRACE_VCD=1 -Os -faligned-new -fcf-protection=none"
    ISSF = f"-I{RD}/sim/iss -I{RD}"
    DPIF = "-DVL_DPIDECL_dpi_mmio_read_ -DVL_DPIDECL_dpi_mmio_write_ -DVL_DPIDECL_rv_set_irq_"

    def comp(src, out, extra=""):
        run(f"g++ {VFLAGS} {extra} -c {src} -o {out}")

    # Verilator support
    comp("/usr/share/verilator/include/verilated.cpp", f"{OD}/verilated.o")
    comp("/usr/share/verilator/include/verilated_dpi.cpp", f"{OD}/verilated_dpi.o")
    comp("/usr/share/verilator/include/verilated_vcd_c.cpp", f"{OD}/verilated_vcd_c.o")
    comp("/usr/share/verilator/include/verilated_threads.cpp", f"{OD}/verilated_threads.o")

    # ISS
    run(f"cc {ISSF} -c {RD}/sim/iss/rv32_dpi.c -o {OD}/rv32_dpi.o")
    run(f"cc {ISSF} -c {RD}/sim/iss/herve_profiler.c -o {OD}/herve_profiler.o")

    # Vtb files
    for f in sorted(glob.glob(f"{OD}/Vtb_top_zephyr*.cpp")):
        base = os.path.basename(f).replace(".cpp", "")
        if "ALL" in base: continue
        comp(f, f"{OD}/{base}.o")

    # Testbench
    comp(f"{OD}/tb.cpp", f"{OD}/tb.o", f"{ISSF} {DPIF}")

    # 5. Archive & link
    all_os = " ".join(sorted(glob.glob(f"{OD}/Vtb_top_zephyr*.o")))
    run(f"ar rcs {OD}/Vtb_top_zephyr__ALL.a {all_os}")
    link = (f"g++ -Os {OD}/tb.o {OD}/verilated.o {OD}/verilated_dpi.o {OD}/verilated_vcd_c.o "
            f"{OD}/verilated_threads.o {OD}/Vtb_top_zephyr__ALL.a "
            f"{OD}/rv32_dpi.o {OD}/herve_profiler.o -pthread -latomic -o {OD}/blinky_demo")
    run(link)
    print(f"== Binary: {OD}/blinky_demo ==")

if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] != "--run":
        build()
    print("== Running Blinky Demo ==")
    os.chdir(RD)
    r = subprocess.run([f"{OD}/blinky_demo"])
    sys.exit(r.returncode)
