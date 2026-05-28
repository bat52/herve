#define _GNU_SOURCE
#include "herve_profiler.h"
#include "rv32_dpi.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

typedef struct herve_func_stats {
    char *name;
    uint64_t total_cycles;
    uint64_t calls;
    uint64_t insn_counts[HERVE_INSN_COUNT];
    uint64_t insn_cycles[HERVE_INSN_COUNT];
    struct herve_func_stats *next;
} herve_func_stats_t;

/* ---------- caller-callee pair tracking ---------- */
typedef struct call_pair {
    char *caller;
    char *callee;
    uint64_t count;
    struct call_pair *next;
} call_pair_t;

static bool profiling_enabled = false;
static const herve_arch_t *current_arch = NULL;
static herve_profile_data_t profile_data;
static herve_func_stats_t *func_stats_head = NULL;
static const char *last_func = NULL;
static call_pair_t *call_pairs_head = NULL;

/* ---------- call/return/jump classification ----------
 *
 * RISC-V control-flow transfer instructions relevant to call-stack
 * tracking (RV32I + compressed extension):
 *
 *   CALL  — saves a link register (rd != 0):
 *     JAL  rd, offset            opcode 0x6F, rd != 0
 *     JALR rd, rs1, imm          opcode 0x67, rd != 0
 *     C.JAL                      quad-01 funct3=001 (writes ra)
 *     C.JALR                     quad-10 funct4=1001 rs2=0 (writes ra)
 *
 *   RET   — returns to caller via the link register:
 *     JALR x0, ra, imm           opcode 0x67, rd=0, rs1=1 (ra)
 *     C.JR  ra (rs1=1)           quad-10 funct4=1000 rs2=0 rs1=1
 *
 *   JUMP  — unconditional branch WITHOUT saving a link register.
 *     This is often a tail call when it crosses function boundaries:
 *     JAL  x0, offset            opcode 0x6F, rd=0  ("j" pseudo)
 *     JALR x0, rs1, 0            opcode 0x67, rd=0, rs1!=1
 *     C.J                        quad-01 funct3=101
 *     C.JR  rs1 (rs1 != 1)       quad-10 funct4=1000 rs2=0 rs1!=1
 */
typedef enum {
    CF_NONE,   /* not a control-flow transfer */
    CF_CALL,   /* call — saves link register */
    CF_RET,    /* return — jumps through link register */
    CF_JUMP,   /* tail-call / plain jump — no link register */
} cf_type_t;

static cf_type_t last_cf = CF_NONE;  /* type of the most recently recorded insn */

/* ---------- RISC-V instruction decoding helpers ---------- */

/* 32-bit instruction decoding */
static cf_type_t classify_insn_32(uint32_t insn) {
    uint32_t opcode = insn & 0x7fu;
    uint32_t rd     = (insn >> 7) & 0x1fu;
    uint32_t rs1    = (insn >> 15) & 0x1fu;

    if (opcode == 0x6f) {                /* JAL */
        return (rd != 0) ? CF_CALL : CF_JUMP;
    }
    if (opcode == 0x67) {                /* JALR */
        if (rd != 0) return CF_CALL;
        /* rd == 0: return if jumping through ra, otherwise tail call */
        return (rs1 == 1) ? CF_RET : CF_JUMP;
    }
    return CF_NONE;
}

/* 16-bit compressed instruction decoding.
 * `insn` is passed as the lower 16 bits (upper bits are zero). */
static cf_type_t classify_insn_16(uint16_t insn) {
    uint8_t quadrant = insn & 0x3u;
    uint8_t funct3   = (insn >> 13) & 0x7u;
    uint8_t funct4   = (insn >> 12) & 0xfu;
    uint8_t rs2      = (insn >> 2)  & 0x1fu;
    uint8_t rs1      = (insn >> 7)  & 0x1fu;

    if (quadrant == 0x1) {
        /* C.JAL (funct3=001) — CALL;  C.J (funct3=101) — JUMP */
        if (funct3 == 0x1) return CF_CALL;   /* C.JAL, writes ra */
        if (funct3 == 0x5) return CF_JUMP;    /* C.J */
        return CF_NONE;
    }
    if (quadrant == 0x2) {
        if (funct4 == 0x8 && rs2 == 0) {      /* C.JR or C.MV (rs2=0 -> JR) */
            return (rs1 == 1) ? CF_RET : CF_JUMP;
        }
        if (funct4 == 0x9 && rs2 == 0) {      /* C.JALR (writes ra) */
            return CF_CALL;
        }
    }
    return CF_NONE;
}

/* ---------- call-pair helpers ---------- */
static void record_call_edge(const char *caller, const char *callee) {
    if (!caller || !callee) return;
    call_pair_t *cp = call_pairs_head;
    while (cp) {
        if (strcmp(cp->caller, caller) == 0 &&
            strcmp(cp->callee, callee) == 0) {
            cp->count++;
            return;
        }
        cp = cp->next;
    }
    cp = (call_pair_t*)malloc(sizeof(call_pair_t));
    cp->caller = strdup(caller);
    cp->callee = strdup(callee);
    cp->count  = 1;
    cp->next   = call_pairs_head;
    call_pairs_head = cp;
}

static void free_call_pairs(void) {
    call_pair_t *cp = call_pairs_head;
    while (cp) {
        call_pair_t *next = cp->next;
        free(cp->caller);
        free(cp->callee);
        free(cp);
        cp = next;
    }
    call_pairs_head = NULL;
}

/* ---------- function-level stats ---------- */
static herve_func_stats_t* get_func_stats(const char *name) {
    if (!name) return NULL;
    herve_func_stats_t *curr = func_stats_head;
    while (curr) {
        if (strcmp(curr->name, name) == 0) return curr;
        curr = curr->next;
    }
    herve_func_stats_t *new_stats = (herve_func_stats_t*)calloc(1, sizeof(herve_func_stats_t));
    new_stats->name = strdup(name);
    new_stats->next = func_stats_head;
    func_stats_head = new_stats;
    return new_stats;
}

static void free_func_stats(void) {
    herve_func_stats_t *curr = func_stats_head;
    while (curr) {
        herve_func_stats_t *next = curr->next;
        free(curr->name);
        free(curr);
        curr = next;
    }
    func_stats_head = NULL;
}

/* ---------- public API ---------- */

void herve_profiler_enable(bool enable) {
    profiling_enabled = enable;
    if (enable) {
        memset(&profile_data, 0, sizeof(profile_data));
        free_func_stats();
        free_call_pairs();
        last_func = NULL;
        last_cf   = CF_NONE;
    }
}

bool herve_profiler_is_enabled(void) {
    return profiling_enabled;
}

void herve_profiler_set_arch(const herve_arch_t *arch) {
    current_arch = arch;
}

const herve_arch_t* herve_profiler_get_arch(void) {
    return current_arch;
}

void herve_profiler_record_insn(herve_insn_type_t type, uint32_t pc, uint32_t insn) {
    if (!profiling_enabled) return;

    /* ---- update global counts ---- */
    profile_data.total_instructions++;
    profile_data.insn_counts[type]++;

    uint32_t cycles = 1;
    if (current_arch && type < HERVE_INSN_COUNT) {
        cycles = current_arch->cycles[type];
    }
    profile_data.total_cycles += cycles;
    profile_data.insn_cycles[type] += cycles;

    /* ---- classify this instruction (before updating last_cf) ---- */
    bool is_compressed = (type == HERVE_INSN_COMPRESSED);
    cf_type_t this_cf  = is_compressed
                             ? classify_insn_16((uint16_t)(insn & 0xFFFFu))
                             : classify_insn_32(insn);

    /* ---- update per-function stats ---- */
    const char *func = herve_get_symbol_at(pc);
    if (func) {
        herve_func_stats_t *fs = get_func_stats(func);
        fs->total_cycles += cycles;
        fs->insn_counts[type]++;
        fs->insn_cycles[type] += cycles;

        if (func != last_func) {
            /* --- function boundary crossed ---
             * Use the PREVIOUS instruction's classification to decide
             * what kind of transition this is. */
            switch (last_cf) {
            case CF_CALL:
                /* Regular call: last_func -> func */
                record_call_edge(last_func, func);
                fs->calls++;
                break;
            case CF_JUMP:
                /* Tail call / plain jump: last_func -> func */
                record_call_edge(last_func, func);
                fs->calls++;
                break;
            case CF_RET:
                /* Return: DO NOT count as a new call */
                break;
            case CF_NONE:
                /* Entry through other means (reset, interrupt, fall-through) */
                fs->calls++;
                break;
            }
            last_func = func;
        }
    } else {
        last_func = NULL;
    }

    /* Stash the classification for the next instruction's boundary check.
     * This must happen AFTER the function-entry logic above so that
     * `last_cf` still reflects the *previous* instruction when we cross
     * a function boundary. */
    last_cf = this_cf;
}

static const char* insn_type_to_str(herve_insn_type_t type) {
    switch (type) {
        case HERVE_INSN_ALU:        return "ALU";
        case HERVE_INSN_MUL:        return "MUL";
        case HERVE_INSN_DIV:        return "DIV";
        case HERVE_INSN_LOAD:       return "LOAD";
        case HERVE_INSN_STORE:      return "STORE";
        case HERVE_INSN_BRANCH:     return "BRANCH";
        case HERVE_INSN_JUMP:       return "JUMP";
        case HERVE_INSN_CSR:        return "CSR";
        case HERVE_INSN_SYS:        return "SYS";
        case HERVE_INSN_COMPRESSED: return "COMPRESSED";
        default:                    return "UNKNOWN";
    }
}

void herve_profiler_report_csv(const char *filename) {
    FILE *f = stdout;
    if (filename && strcmp(filename, "-") != 0) {
        f = fopen(filename, "w");
        if (!f) {
            fprintf(stderr, "Could not open %s for writing\n", filename);
            return;
        }
    }

    fprintf(f, "META,arch,%s\n", current_arch ? current_arch->name : "default");
    fprintf(f, "META,isa,rv32im\n");
    fprintf(f, "META,total_cycles,%llu\n", (unsigned long long)profile_data.total_cycles);
    fprintf(f, "META,total_instructions,%llu\n", (unsigned long long)profile_data.total_instructions);

    /* Per-function instruction breakdown */
    herve_func_stats_t *fs = func_stats_head;
    while (fs) {
        fprintf(f, "FUNC,%s,total_cycles,%llu\n", fs->name, (unsigned long long)fs->total_cycles);
        fprintf(f, "FUNC,%s,calls,%llu\n", fs->name, (unsigned long long)fs->calls);
        for (int i = 1; i < HERVE_INSN_COUNT; i++) {
            if (fs->insn_counts[i] > 0) {
                fprintf(f, "INSTR,%s,%s,%llu\n", fs->name, insn_type_to_str((herve_insn_type_t)i), (unsigned long long)fs->insn_cycles[i]);
                fprintf(f, "ICOUNT,%s,%s,%llu\n", fs->name, insn_type_to_str((herve_insn_type_t)i), (unsigned long long)fs->insn_counts[i]);
            }
        }
        fs = fs->next;
    }

    /* Caller-callee edges */
    call_pair_t *cp = call_pairs_head;
    while (cp) {
        fprintf(f, "CALL,%s,%s,%llu\n", cp->caller, cp->callee, (unsigned long long)cp->count);
        cp = cp->next;
    }

    if (f != stdout) {
        fclose(f);
    }
}
