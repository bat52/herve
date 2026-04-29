/**
 * rv32_dpi_vpi.c — VPI wrapper for the RV32 DPI ISS.
 *
 * Since Icarus Verilog 12.0 does not support DPI-C import/export
 * declarations, this VPI module provides equivalent system functions
 * via the Verilog Procedural Interface (VPI).
 *
 * Registered system functions/tasks:
 *   $rv_init(fw_path, ram_size)   — System task, load firmware into ISS
 *   $rv_reset(pc)                 — System task, reset ISS to PC
 *   $rv_step(max_insn)            — System function, execute N instructions, returns count
 *   $rv_get_reg(reg)              — System function, return value of x[reg]
 *   $rv_get_pc()                  — System function, return current PC
 *   $vpi_read_mmio(idx)           — System function, read MMIO register at 0x1000_0000 + idx*4
 *   $vpi_print_mmio()             — System task, print MMIO register state
 *
 * Build:
 *   gcc -shared -fPIC -I./sim/iss -I/usr/include/iverilog \
 *       -o obj_dir/rv32_dpi_vpi.vpi \
 *       sim/vpi/rv32_dpi_vpi.c sim/iss/rv32_dpi.c
 *
 * Run:
 *   iverilog -g2012 -o obj_dir/tb_icarus.vvp sim/icarus/tb_icarus_vpi.sv
 *   vvp -Mobj_dir -mrv32_dpi_vpi obj_dir/tb_icarus.vvp
 *
 * Reference: IEEE 1800-2017 §35 (VPI), Icarus VPI docs.
 */

#include <vpi_user.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  MMIO register file (C-side storage, replaces DPI export)          */
/* ------------------------------------------------------------------ */
#define MMIO_BASE  0x10000000u
#define MMIO_REGS  64

static uint32_t mmio_regs[MMIO_REGS];

/* These functions are called by rv32_dpi.c when the ISS does
 * a load/store to the MMIO address range. They replace the
 * DPI extern declarations that normally go to the SV side. */
uint32_t dpi_mmio_read(uint32_t addr) {
    if (addr >= MMIO_BASE && addr < MMIO_BASE + 0x100u) {
        uint32_t idx = (addr - MMIO_BASE) / 4;
        if (idx < MMIO_REGS) {
            return mmio_regs[idx];
        }
    }
    return 0;
}

void dpi_mmio_write(uint32_t addr, uint32_t value) {
    if (addr >= MMIO_BASE && addr < MMIO_BASE + 0x100u) {
        uint32_t idx = (addr - MMIO_BASE) / 4;
        if (idx < MMIO_REGS) {
            mmio_regs[idx] = value;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  ISS function declarations (from rv32_dpi.c / rv32_dpi.h)          */
/* ------------------------------------------------------------------ */
void   rv_init(const char *firmware, size_t ram_size);
void   rv_reset(uint32_t pc);
int    rv_step(int max_instructions);
uint32_t rv_get_pc(void);
uint32_t rv_get_reg(unsigned reg);

/* ------------------------------------------------------------------ */
/*  VPI callbacks — System Tasks (void return)                        */
/* ------------------------------------------------------------------ */

/* VPI calltf functions receive a PLI_BYTE8* (char*) argument list,
 * not p_cb_data directly in Icarus. We use the standard Icarus VPI
 * convention where the argument is the instance handle. */

/* $rv_init(fw_path, ram_size) — system task */
static PLI_INT32 calltf_rv_init(PLI_BYTE8 *user_data) {
    (void)user_data;
    vpiHandle systf = vpi_handle(vpiSysTfCall, NULL);
    vpiHandle arg_iter = vpi_iterate(vpiArgument, systf);
    if (arg_iter == NULL) return 0;

    /* Get first argument: string path to firmware */
    vpiHandle arg1 = vpi_scan(arg_iter);
    s_vpi_value val1;
    val1.format = vpiStringVal;
    vpi_get_value(arg1, &val1);

    /* Get second argument: ram_size integer */
    vpiHandle arg2 = vpi_scan(arg_iter);
    s_vpi_value val2;
    val2.format = vpiIntVal;
    vpi_get_value(arg2, &val2);
    int ram_size = val2.value.integer;
    if (ram_size <= 0) ram_size = 1 << 20;

    if (val1.value.str != NULL && strlen(val1.value.str) > 0) {
        rv_init(val1.value.str, (size_t)ram_size);
    } else {
        rv_init(NULL, (size_t)ram_size);
    }

    vpi_free_object(arg1);
    vpi_free_object(arg2);
    vpi_free_object(arg_iter);
    return 0;
}

/* $rv_reset(pc) — system task */
static PLI_INT32 calltf_rv_reset(PLI_BYTE8 *user_data) {
    (void)user_data;
    vpiHandle systf = vpi_handle(vpiSysTfCall, NULL);
    vpiHandle arg_iter = vpi_iterate(vpiArgument, systf);
    if (arg_iter == NULL) return 0;

    vpiHandle arg = vpi_scan(arg_iter);
    s_vpi_value val;
    val.format = vpiIntVal;
    vpi_get_value(arg, &val);
    int pc = val.value.integer;

    rv_reset((uint32_t)pc);

    vpi_free_object(arg);
    vpi_free_object(arg_iter);
    return 0;
}

/* $vpi_print_mmio() — system task with no args */
static PLI_INT32 calltf_vpi_print_mmio(PLI_BYTE8 *user_data) {
    (void)user_data;
    int i;
    for (i = 0; i < 8; i++) {
        char line[256];
        snprintf(line, sizeof(line),
                 "  mmio_regs[%d] = 0x%08x\n", i, mmio_regs[i]);
        /* Use io_printf for cleaner output, but keep vpi_printf for compatibility */
        vpi_printf("%s", line);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  VPI callbacks — System Functions (return int)                     */
/* ------------------------------------------------------------------ */

/* Helper: set the return value of the current VPI system function call */
static void set_int_return(int value) {
    vpiHandle systf = vpi_handle(vpiSysTfCall, NULL);
    s_vpi_value ret_val;
    ret_val.format = vpiIntVal;
    ret_val.value.integer = value;
    vpi_put_value(systf, &ret_val, NULL, vpiNoDelay);
}

/* $rv_step(max_insn) -> int */
static PLI_INT32 calltf_rv_step(PLI_BYTE8 *user_data) {
    (void)user_data;
    vpiHandle systf = vpi_handle(vpiSysTfCall, NULL);
    vpiHandle arg_iter = vpi_iterate(vpiArgument, systf);
    if (arg_iter == NULL) {
        set_int_return(0);
        return 0;
    }

    vpiHandle arg = vpi_scan(arg_iter);
    s_vpi_value val;
    val.format = vpiIntVal;
    vpi_get_value(arg, &val);
    int max_insn = val.value.integer;

    int result = rv_step(max_insn);
    set_int_return(result);

    vpi_free_object(arg);
    vpi_free_object(arg_iter);
    return 0;
}

/* $rv_get_reg(reg) -> int */
static PLI_INT32 calltf_rv_get_reg(PLI_BYTE8 *user_data) {
    (void)user_data;
    vpiHandle systf = vpi_handle(vpiSysTfCall, NULL);
    vpiHandle arg_iter = vpi_iterate(vpiArgument, systf);
    if (arg_iter == NULL) {
        set_int_return(0);
        return 0;
    }

    vpiHandle arg = vpi_scan(arg_iter);
    s_vpi_value val;
    val.format = vpiIntVal;
    vpi_get_value(arg, &val);
    int reg = val.value.integer;

    uint32_t reg_val = rv_get_reg((unsigned)reg);
    set_int_return((int)reg_val);

    vpi_free_object(arg);
    vpi_free_object(arg_iter);
    return 0;
}

/* $rv_get_pc() -> int */
static PLI_INT32 calltf_rv_get_pc(PLI_BYTE8 *user_data) {
    (void)user_data;
    uint32_t pc = rv_get_pc();
    set_int_return((int)pc);
    return 0;
}

/* $vpi_read_mmio(idx) -> int */
static PLI_INT32 calltf_vpi_read_mmio(PLI_BYTE8 *user_data) {
    (void)user_data;
    vpiHandle systf = vpi_handle(vpiSysTfCall, NULL);
    vpiHandle arg_iter = vpi_iterate(vpiArgument, systf);
    if (arg_iter == NULL) {
        set_int_return(0);
        return 0;
    }

    vpiHandle arg = vpi_scan(arg_iter);
    s_vpi_value val;
    val.format = vpiIntVal;
    vpi_get_value(arg, &val);
    int idx = val.value.integer;

    uint32_t mmio_val = (idx >= 0 && idx < MMIO_REGS) ? mmio_regs[idx] : 0;
    set_int_return((int)mmio_val);

    vpi_free_object(arg);
    vpi_free_object(arg_iter);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  VPI module registration                                           */
/* ------------------------------------------------------------------ */

/* In Icarus Verilog, the standard entry point is the
 * vlog_startup_routines array. The array elements are function
 * pointers that are called at VPI startup. */
static void register_rv_functions(void) {
    s_vpi_systf_data tf_data;

    /* $rv_init — system task (void return, two args: string, int) */
    memset(&tf_data, 0, sizeof(tf_data));
    tf_data.type      = vpiSysTask;
    tf_data.sysfunctype = 0;
    tf_data.tfname    = (PLI_BYTE8 *)"$rv_init";
    tf_data.calltf    = (PLI_INT32 (*)(PLI_BYTE8 *))calltf_rv_init;
    tf_data.compiletf = NULL;
    tf_data.sizetf    = NULL;
    vpi_register_systf(&tf_data);

    /* $rv_reset — system task (void return, one int arg) */
    memset(&tf_data, 0, sizeof(tf_data));
    tf_data.type      = vpiSysTask;
    tf_data.sysfunctype = 0;
    tf_data.tfname    = (PLI_BYTE8 *)"$rv_reset";
    tf_data.calltf    = (PLI_INT32 (*)(PLI_BYTE8 *))calltf_rv_reset;
    tf_data.compiletf = NULL;
    tf_data.sizetf    = NULL;
    vpi_register_systf(&tf_data);

    /* $vpi_print_mmio — system task (void return, no args) */
    memset(&tf_data, 0, sizeof(tf_data));
    tf_data.type      = vpiSysTask;
    tf_data.sysfunctype = 0;
    tf_data.tfname    = (PLI_BYTE8 *)"$vpi_print_mmio";
    tf_data.calltf    = (PLI_INT32 (*)(PLI_BYTE8 *))calltf_vpi_print_mmio;
    tf_data.compiletf = NULL;
    tf_data.sizetf    = NULL;
    vpi_register_systf(&tf_data);

    /* $rv_step — system function (int return, one int arg) */
    memset(&tf_data, 0, sizeof(tf_data));
    tf_data.type      = vpiSysFunc;
    tf_data.sysfunctype = vpiSysFuncInt;
    tf_data.tfname    = (PLI_BYTE8 *)"$rv_step";
    tf_data.calltf    = (PLI_INT32 (*)(PLI_BYTE8 *))calltf_rv_step;
    tf_data.compiletf = NULL;
    tf_data.sizetf    = NULL;
    vpi_register_systf(&tf_data);

    /* $rv_get_reg — system function (int return, one int arg) */
    memset(&tf_data, 0, sizeof(tf_data));
    tf_data.type      = vpiSysFunc;
    tf_data.sysfunctype = vpiSysFuncInt;
    tf_data.tfname    = (PLI_BYTE8 *)"$rv_get_reg";
    tf_data.calltf    = (PLI_INT32 (*)(PLI_BYTE8 *))calltf_rv_get_reg;
    tf_data.compiletf = NULL;
    tf_data.sizetf    = NULL;
    vpi_register_systf(&tf_data);

    /* $rv_get_pc — system function (int return, no args) */
    memset(&tf_data, 0, sizeof(tf_data));
    tf_data.type      = vpiSysFunc;
    tf_data.sysfunctype = vpiSysFuncInt;
    tf_data.tfname    = (PLI_BYTE8 *)"$rv_get_pc";
    tf_data.calltf    = (PLI_INT32 (*)(PLI_BYTE8 *))calltf_rv_get_pc;
    tf_data.compiletf = NULL;
    tf_data.sizetf    = NULL;
    vpi_register_systf(&tf_data);

    /* $vpi_read_mmio — system function (int return, one int arg) */
    memset(&tf_data, 0, sizeof(tf_data));
    tf_data.type      = vpiSysFunc;
    tf_data.sysfunctype = vpiSysFuncInt;
    tf_data.tfname    = (PLI_BYTE8 *)"$vpi_read_mmio";
    tf_data.calltf    = (PLI_INT32 (*)(PLI_BYTE8 *))calltf_vpi_read_mmio;
    tf_data.compiletf = NULL;
    tf_data.sizetf    = NULL;
    vpi_register_systf(&tf_data);
}

/* This is the entry point called by Icarus vvp at startup.
 * The name is magical — it must be exactly this array. */
void (*vlog_startup_routines[])() = {
    register_rv_functions,
    0  /* NULL sentinel */
};
