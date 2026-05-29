#include "Vtb_top_zephyr.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "rv32_dpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static Vtb_top_zephyr *tb = 0;
static uint64_t st = 0;
extern "C" int dpi_mmio_read(int a) {
    switch(a){case 0x10000000:return tb->mem_read;case 0x10000010:return tb->mtime_lo;case 0x10000014:return tb->mtime_hi;case 0x10000018:return tb->mtimecmp_lo;case 0x1000001C:return tb->mtimecmp_hi;default:return 0;}
}
extern "C" void dpi_mmio_write(int a,int d) {
    switch(a){case 0x10000000:tb->mem_write=d;break;case 0x10000018:tb->mtimecmp_lo=d;break;case 0x1000001C:tb->mtimecmp_hi=d;break;default:break;}
}
int main(int argc,char**argv){
    Verilated::commandArgs(argc,argv);Verilated::traceEverOn(true);
    const char*fp="firmware_blinky.bin";int mt=200000;
    for(int i=1;i<argc;i++){if(!strcmp(argv[i],"-c")&&i+1<argc)mt=atoi(argv[++i]);else if(argv[i][0]!='-')fp=argv[i];}
    printf("=== Blinky Demo ===\nFirmware: %s\nTicks: %d\n\n",fp,mt);
    tb=new Vtb_top_zephyr;VerilatedVcdC*tf=new VerilatedVcdC;tb->trace(tf,5);tf->open("tb_top_zephyr_blinky.vcd");
    rv_init(fp,4<<20);if(!rv_get_ram())return 1;rv_reset(0);
    tb->clk=0;tb->rstn=0;tb->irq=0;tb->mem_read=0;tb->mem_write=0;tb->mtimecmp_lo=0xFFFFFFFF;tb->mtimecmp_hi=0xFFFFFFFF;
    tf->dump(st);for(int i=0;i<8;i++){tb->clk=!tb->clk;tb->eval();st+=5000;tf->dump(st);}tb->rstn=1;
    printf("Running %d ticks...\n",mt);
    int ti=0,pg=0,tg=0,ft=0;
    for(int tc=1;tc<=mt;tc++){
        for(int e=0;e<2;e++){tb->clk=!tb->clk;tb->eval();st+=5000;tf->dump(st);}
        int ex=rv_step(10000);if(ex>0)ti+=ex;if(rv_is_halted())break;
        int g=tb->mem_write;if((g&1)!=(pg&1)){tg++;if(tg==1)ft=tc;printf("[%6d] GPIO=%d instr=%d\n",tc,g&1,ti);}pg=g;
        if(tc%50000==0)printf("[%6d] progress instr=%d mtime=%u\n",tc,ti,tb->mtime_lo);
    }
    printf("\n=== Results ===\nToggles: %d\nFirst: tick %d\nGPIO: 0x%08x\nVCD: tb_top_zephyr_blinky.vcd\n%s\n",tg,ft,tb->mem_write,(tg>=2)?"PASS":"FAIL");
    tf->close();delete tf;tb->final();delete tb;return(tg>=2)?0:1;
}
