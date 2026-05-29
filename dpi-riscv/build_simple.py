#!/usr/bin/env python3
"""Simple build script for blinky demo."""
import os, subprocess, sys

RD = os.path.dirname(os.path.abspath(__file__))
OD = os.path.join(RD, "obj_dir")

def run(cmd, **kw):
    print(f"+ {cmd[:120] if isinstance(cmd,str) else ' '.join(cmd[:15])}")
    return subprocess.run(cmd, shell=isinstance(cmd, str), **kw)

# Clean
subprocess.run(["rm", "-rf", OD])
os.makedirs(OD)

# 1. Firmware
run(f"riscv64-unknown-elf-as -march=rv32im_zicsr -o {OD}/fw.o {RD}/firmware_blinky.S")
run(f"riscv64-unknown-elf-ld -m elf32lriscv -Ttext=0x00000000 -o {OD}/fw.elf {OD}/fw.o")
run(f"riscv64-unknown-elf-objcopy -O binary {OD}/fw.elf {OD}/fw.bin")
subprocess.run(["cp", f"{OD}/fw.bin", f"{RD}/firmware_blinky.bin"])

# 2. Verilator – use tb_top_zephyr.sv (it works!)
verilator_cmd = [
    "verilator", "--cc", "--trace", "--top-module", "tb_top_zephyr",
    "-Mdir", OD,
    f"{RD}/sim/harness/tb_top_zephyr.sv",
    f"{RD}/sim/harness/rv32_dpi_zephyr_tb.cpp"
]
r = run(verilator_cmd)
if r.returncode != 0:
    print("Verilator failed"); sys.exit(1)

# 3. Write a minimal blinky testbench (overwrite the .cpp)
tb_src = '''#include "Vtb_top_zephyr.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "rv32_dpi.h"
#include <stdio.h>
static Vtb_top_zephyr*tb=0;
static uint64_t st=0;
static void tk(){tb->clk=!tb->clk;tb->eval();st+=5000;}
extern "C" int dpi_mmio_read(int a){
  switch(a){
    case 0x10000010:return tb->mtime_lo;case 0x10000014:return tb->mtime_hi;
    case 0x10000018:return tb->mtimecmp_lo;case 0x1000001C:return tb->mtimecmp_hi;
    case 0x10000000:return tb->mem_read;default:return 0;
  }
}
extern "C" void dpi_mmio_write(int a,int d){
  fprintf(stderr,"DPI_W: a=0x%x d=0x%x\\n",a,d);
  switch(a){case 0x10000000:tb->mem_write=d;break;
  case 0x10000018:tb->mtimecmp_lo=d;break;case 0x1000001C:tb->mtimecmp_hi=d;break;default:break;}
}
int main(int argc,char**argv){
  Verilated::commandArgs(argc,argv);Verilated::traceEverOn(true);
  tb=new Vtb_top_zephyr;VerilatedVcdC*tfp=new VerilatedVcdC;
  tb->trace(tfp,5);tfp->open("tb_top_blinky.vcd");
  rv_init("firmware_blinky.bin",1<<20);rv_reset(0);
  tb->clk=0;tb->rstn=0;tb->irq=0;tb->mem_read=0;tb->mem_write=0;
  tb->mtimecmp_lo=0xFFFFFFFF;tb->mtimecmp_hi=0xFFFFFFFF;
  for(int i=0;i<8;i++)tk();tb->rstn=1;
  rv_step(1000);
  for(int tc=1;tc<=100000;tc++){
    tk();tk();
    for(int s=0;s<5;s++){int ex=rv_step(1000);if(!ex)break;}
    int w=tb->mem_write&1;
    if(w)printf("GPIO=1 @%d\\n",tc);
  }
  printf("Final: mem_write=0x%x mtime=%u mcmp=0x%x\\n",tb->mem_write,tb->mtime_lo,tb->mtimecmp_lo);
  printf("VCD: tb_top_blinky.vcd\\n");
  tfp->close();tb->final();delete tb;delete tfp;return 0;
}
'''
with open(f"{OD}/tb.cpp", "w") as f:
    f.write(tb_src)

# 4. Compile everything manually
VINC = f"-I{OD} -I/usr/share/verilator/include -I/usr/share/verilator/include/vltstd"
VFLAGS = f"{VINC} -DVM_COVERAGE=0 -DVM_SC=0 -DVM_TRACE=1 -DVM_TRACE_VCD=1 -Os -faligned-new -fcf-protection=none"

# verilated support
for f in ["verilated.cpp", "verilated_dpi.cpp", "verilated_vcd_c.cpp", "verilated_threads.cpp"]:
    run(f"g++ {VFLAGS} -c /usr/share/verilator/include/{f} -o {OD}/{f.replace('.cpp','.o')}")

# ISS
run(f"cc -I{RD}/sim/iss -I{RD} -c {RD}/sim/iss/rv32_dpi.c -o {OD}/rv32_dpi.o")
run(f"cc -I{RD}/sim/iss -I{RD} -c {RD}/sim/iss/herve_profiler.c -o {OD}/herve_profiler.o")

# Vtb files
vtb_files = []
for f in os.listdir(OD):
    if f.startswith("Vtb_top_zephyr") and f.endswith(".cpp") and f != "Vtb_top_zephyr__ALL.cpp":
        vtb_files.append(f)
for f in sorted(vtb_files):
    run(f"g++ {VFLAGS} -c {OD}/{f} -o {OD}/{f.replace('.cpp','.o')}")

# Testbench
run(f"g++ {VFLAGS} -I{RD}/sim/iss -I{RD} -DVL_DPIDECL_dpi_mmio_read_ -DVL_DPIDECL_dpi_mmio_write_ -DVL_DPIDECL_rv_set_irq_ -c {OD}/tb.cpp -o {OD}/tb.o")

# Generate ALL.o
verilator_inc = f"/usr/share/verilator/bin/verilator_includer"
cpp_files_in = " ".join(f"{OD}/{f}" for f in sorted(vtb_files))
run(f"python3 {verilator_inc} -DVL_INCLUDE_OPT=include {cpp_files_in} > {OD}/Vtb_top_zephyr__ALL.cpp")
run(f"g++ {VFLAGS} -c {OD}/Vtb_top_zephyr__ALL.cpp -o {OD}/Vtb_top_zephyr__ALL.o")

# Archive & link
all_obj = " ".join(f"{OD}/{f.replace('.cpp','.o')}" for f in vtb_files)
run(f"ar rcs {OD}/Vtb_top_zephyr__ALL.a {all_obj}")
link = (f"g++ -Os {OD}/tb.o {OD}/verilated.o {OD}/verilated_dpi.o {OD}/verilated_vcd_c.o "
        f"{OD}/verilated_threads.o {OD}/Vtb_top_zephyr__ALL.a {OD}/rv32_dpi.o {OD}/herve_profiler.o "
        f"-pthread -latomic -o {OD}/blinky_demo")
run(link)

print("=== BUILD DONE ===")
