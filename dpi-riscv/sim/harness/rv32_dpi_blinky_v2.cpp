#include "Vtb_top_zephyr.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "rv32_dpi.h"
#include <stdio.h>
static Vtb_top_zephyr *tb; static uint64_t st; static VerilatedVcdC *tfp;
static void tk(){tb->clk=!tb->clk;tb->eval();st+=5000;if(tfp)tfp->dump(st);}
extern "C" int dpi_mmio_read(int a){
    switch(a){case 0x10000000:return tb->mem_read;case 0x10000010:return tb->mtime_lo;
    case 0x10000014:return tb->mtime_hi;case 0x10000018:return tb->mtimecmp_lo;
    case 0x1000001C:return tb->mtimecmp_hi;default:return 0;}
}
extern "C" void dpi_mmio_write(int a,int d){
    switch(a){case 0x10000000:tb->mem_write=d;break;case 0x10000018:tb->mtimecmp_lo=d;break;
    case 0x1000001C:tb->mtimecmp_hi=d;break;default:break;}
}
int main(int argc,char**argv){
    Verilated::commandArgs(argc,argv);Verilated::traceEverOn(true);
    tb=new Vtb_top_zephyr;tfp=new VerilatedVcdC;tb->trace(tfp,5);
    tfp->open("tb_top_zephyr_blinky.vcd");
    rv_init_elf("firmware_blinky.elf",16<<20);
    if(!rv_get_ram()){fprintf(stderr,"ELF fail\n");return 1;}
    rv_reset(rv_get_pc());
    tb->clk=0;tb->rstn=0;tb->irq=0;tb->mem_read=0;tb->mem_write=0;
    tb->mtimecmp_lo=0xFFFFFFFF;tb->mtimecmp_hi=0xFFFFFFFF;
    tfp->dump(st);for(int i=0;i<8;i++)tk();tb->rstn=1;
    int n=rv_step(5000);fprintf(stderr,"rv_step=%d pc=0x%08x\n",n,rv_get_pc());
    int tog=0,last=0;
    for(int tc=1;tc<=300000;tc++){
        tk();tk();
        if(tc%100==0){do{n=rv_step(5000);}while(n>0);}
        if(tb->mem_write!=(unsigned)last){last=tb->mem_write;tog++;
            printf("tick %d toggle#%d gpio=0x%08x\n",tc,tog,tb->mem_write);
            if(tog>=10)break;}
    }
    printf("Toggles:%d VCD:tb_top_zephyr_blinky.vcd\n%s\n",tog,tog>=2?"PASS":"FAIL");
    tfp->close();tb->final();delete tfp;delete tb;return(tog>=2)?0:1;
}
