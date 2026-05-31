#ifndef HERVE_PROFILER_H
#define HERVE_PROFILER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    HERVE_INSN_UNKNOWN = 0,
    HERVE_INSN_ALU,
    HERVE_INSN_MUL,
    HERVE_INSN_DIV,
    HERVE_INSN_LOAD,
    HERVE_INSN_STORE,
    HERVE_INSN_BRANCH,
    HERVE_INSN_JUMP,
    HERVE_INSN_CSR,
    HERVE_INSN_SYS,
    HERVE_INSN_COMPRESSED,
    HERVE_INSN_COUNT
} herve_insn_type_t;

typedef struct {
    const char *name;
    uint32_t pipeline_stages;
    uint32_t cycles[HERVE_INSN_COUNT];
} herve_arch_t;

typedef struct {
    uint64_t total_cycles;
    uint64_t total_instructions;
    uint64_t insn_counts[HERVE_INSN_COUNT];
    uint64_t insn_cycles[HERVE_INSN_COUNT];
} herve_profile_data_t;

#ifdef __cplusplus
extern "C" {
#endif

void herve_profiler_enable(bool enable);
bool herve_profiler_is_enabled(void);
void herve_profiler_set_arch(const herve_arch_t *arch);
const herve_arch_t* herve_profiler_get_arch(void);
void herve_profiler_record_insn(herve_insn_type_t type, uint32_t pc, uint32_t insn);
void herve_profiler_report_csv(const char *filename);

#ifdef __cplusplus
}
#endif

#endif // HERVE_PROFILER_H
