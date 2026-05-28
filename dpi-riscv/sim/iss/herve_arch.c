#include "herve_profiler.h"
#include <string.h>

static const herve_arch_t arch_serv = {
    .name = "serv",
    .pipeline_stages = 1,
    .cycles = {
        [HERVE_INSN_ALU] = 32, // Bit-serial, very slow
        [HERVE_INSN_MUL] = 64,
        [HERVE_INSN_DIV] = 1024,
        [HERVE_INSN_LOAD] = 64,
        [HERVE_INSN_STORE] = 64,
        [HERVE_INSN_BRANCH] = 32,
        [HERVE_INSN_JUMP] = 32,
        [HERVE_INSN_CSR] = 32,
        [HERVE_INSN_SYS] = 32,
        [HERVE_INSN_COMPRESSED] = 32,
    }
};

static const herve_arch_t arch_ibex_small = {
    .name = "ibex_small",
    .pipeline_stages = 2,
    .cycles = {
        [HERVE_INSN_ALU] = 1,
        [HERVE_INSN_MUL] = 3,
        [HERVE_INSN_DIV] = 33,
        [HERVE_INSN_LOAD] = 2,
        [HERVE_INSN_STORE] = 2,
        [HERVE_INSN_BRANCH] = 2,
        [HERVE_INSN_JUMP] = 2,
        [HERVE_INSN_CSR] = 1,
        [HERVE_INSN_SYS] = 1,
        [HERVE_INSN_COMPRESSED] = 1,
    }
};

static const herve_arch_t arch_picorv32 = {
    .name = "picorv32",
    .pipeline_stages = 1, // It's not really pipelined in the traditional sense, CPI is > 1
    .cycles = {
        [HERVE_INSN_ALU] = 3,
        [HERVE_INSN_MUL] = 4,
        [HERVE_INSN_DIV] = 40,
        [HERVE_INSN_LOAD] = 5,
        [HERVE_INSN_STORE] = 5,
        [HERVE_INSN_BRANCH] = 3,
        [HERVE_INSN_JUMP] = 3,
        [HERVE_INSN_CSR] = 3,
        [HERVE_INSN_SYS] = 3,
        [HERVE_INSN_COMPRESSED] = 3,
    }
};

static const herve_arch_t arch_vexriscv = {
    .name = "vexriscv",
    .pipeline_stages = 5,
    .cycles = {
        [HERVE_INSN_ALU] = 1,
        [HERVE_INSN_MUL] = 2,
        [HERVE_INSN_DIV] = 33,
        [HERVE_INSN_LOAD] = 2,
        [HERVE_INSN_STORE] = 1,
        [HERVE_INSN_BRANCH] = 2,
        [HERVE_INSN_JUMP] = 2,
        [HERVE_INSN_CSR] = 1,
        [HERVE_INSN_SYS] = 1,
        [HERVE_INSN_COMPRESSED] = 1,
    }
};

static const herve_arch_t arch_cva6 = {
    .name = "cva6",
    .pipeline_stages = 6,
    .cycles = {
        [HERVE_INSN_ALU] = 1,
        [HERVE_INSN_MUL] = 1,
        [HERVE_INSN_DIV] = 8,
        [HERVE_INSN_LOAD] = 3,
        [HERVE_INSN_STORE] = 2,
        [HERVE_INSN_BRANCH] = 2,
        [HERVE_INSN_JUMP] = 2,
        [HERVE_INSN_CSR] = 1,
        [HERVE_INSN_SYS] = 1,
        [HERVE_INSN_COMPRESSED] = 1,
    }
};

static const herve_arch_t arch_boom = {
    .name = "boom",
    .pipeline_stages = 10, // OoO but let's give some avg
    .cycles = {
        [HERVE_INSN_ALU] = 1,
        [HERVE_INSN_MUL] = 1,
        [HERVE_INSN_DIV] = 4,
        [HERVE_INSN_LOAD] = 4,
        [HERVE_INSN_STORE] = 1,
        [HERVE_INSN_BRANCH] = 1,
        [HERVE_INSN_JUMP] = 1,
        [HERVE_INSN_CSR] = 1,
        [HERVE_INSN_SYS] = 1,
        [HERVE_INSN_COMPRESSED] = 1,
    }
};

static const herve_arch_t arch_xiangshan = {
    .name = "xiangshan",
    .pipeline_stages = 11,
    .cycles = {
        [HERVE_INSN_ALU] = 1,
        [HERVE_INSN_MUL] = 1,
        [HERVE_INSN_DIV] = 3,
        [HERVE_INSN_LOAD] = 4,
        [HERVE_INSN_STORE] = 1,
        [HERVE_INSN_BRANCH] = 1,
        [HERVE_INSN_JUMP] = 1,
        [HERVE_INSN_CSR] = 1,
        [HERVE_INSN_SYS] = 1,
        [HERVE_INSN_COMPRESSED] = 1,
    }
};

static const herve_arch_t arch_ibex_medium = {
    .name = "ibex_medium",
    .pipeline_stages = 2,
    .cycles = {
        [HERVE_INSN_ALU] = 1,
        [HERVE_INSN_MUL] = 1, // Fast multiplier
        [HERVE_INSN_DIV] = 33,
        [HERVE_INSN_LOAD] = 2,
        [HERVE_INSN_STORE] = 2,
        [HERVE_INSN_BRANCH] = 2,
        [HERVE_INSN_JUMP] = 2,
        [HERVE_INSN_CSR] = 1,
        [HERVE_INSN_SYS] = 1,
        [HERVE_INSN_COMPRESSED] = 1,
    }
};

static const herve_arch_t arch_ibex_large = {
    .name = "ibex_large",
    .pipeline_stages = 3, // 3-stage pipeline
    .cycles = {
        [HERVE_INSN_ALU] = 1,
        [HERVE_INSN_MUL] = 1,
        [HERVE_INSN_DIV] = 33,
        [HERVE_INSN_LOAD] = 3,
        [HERVE_INSN_STORE] = 3,
        [HERVE_INSN_BRANCH] = 3,
        [HERVE_INSN_JUMP] = 3,
        [HERVE_INSN_CSR] = 1,
        [HERVE_INSN_SYS] = 1,
        [HERVE_INSN_COMPRESSED] = 1,
    }
};

static const herve_arch_t *arch_list[] = {
    &arch_serv,
    &arch_ibex_small,
    &arch_ibex_medium,
    &arch_ibex_large,
    &arch_picorv32,
    &arch_vexriscv,
    &arch_cva6,
    &arch_boom,
    &arch_xiangshan,
    NULL
};

const herve_arch_t* herve_arch_get_by_name(const char *name) {
    for (int i = 0; arch_list[i] != NULL; i++) {
        if (strcmp(arch_list[i]->name, name) == 0) {
            return arch_list[i];
        }
    }
    return NULL;
}
