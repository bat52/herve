#include "Vtb_top_zephyr.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "rv32_dpi.h"
#include <stdio.h>
#include <string.h>
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
  switch(a){case 0x10000000:tb->mem_write=d;break;case 0x10000018:tb->mtimecmp_lo=d;break;case 0x1000001C:tb->mtimecmp_hi=d;break;default:break;}
}
int main(int argc,char**argv){
  Verilated::commandArgs(argc,argv);Verilated::traceEverOn(true);
  printf("=== Blinky Demo ===\n");
  tb=new Vtb_top_zephyr;VerilatedVcdC*tfp=new VerilatedVcdC;
  tb->trace(tfp,5);tfp->open("tb_top_blinky.vcd");
  rv_init("firmware_blinky.bin",1<<20);rv_reset(0);
  uint32_t*ram=(uint32_t*)rv_get_ram();printf("FW[0]=0x%08x\n",ram[0]);
  tb->clk=0;tb->rstn=0;tb->irq=0;tb->mem_read=0;tb->mem_write=0;
  tb->mtimecmp_lo=0xFFFFFFFF;tb->mtimecmp_hi=0xFFFFFFFF;
  for(int i=0;i<8;i++)tk();tb->rstn=1;
  int ex=rv_step(1000);printf("Boot: %d PC=0x%08x mtimecmp=0x%x\n",ex,rv_get_pc(),tb->mtimecmp_lo);
  int tog=0,last=0,ev[10]={0};
  for(int tc=1;tc<=300000;tc++){
    tk();tk();
    for(int s=0;s<5;s++){ex=rv_step(1000);if(!ex)break;}
    int c=tb->mem_write&1;
    if(c!=last&&tog<10){ev[tog]=tc;printf("Toggle %d @ %d = %d\n",tog+1,tc,c);tog++;last=c;}
    if(tc%100000==0)printf("[%d] mtime=%u mcmp=0x%x pc=0x%x gpio=0x%x\n",tc,tb->mtime_lo,tb->mtimecmp_lo,rv_get_pc(),tb->mem_write);
  }
  bool pass=(tog>=4);printf("Toggles: %d %s\n",tog,pass?"PASS":"FAIL");
  if(pass&&tog>=4)printf("Period: %d ticks\n",ev[2]-ev[0]);
  printf("mem_write=0x%x mtime=%u mcmp=0x%x\n",tb->mem_write,tb->mtime_lo,tb->mtimecmp_lo);
  printf("VCD: tb_top_blinky.vcd\n");
  tfp->close();tb->final();delete tb;delete tfp;return pass?0:1;
}
