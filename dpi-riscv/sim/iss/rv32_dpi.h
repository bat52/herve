#ifndef RV32_DPI_H
#define RV32_DPI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "herve_profiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the emulator from a binary file.
 * @param firmware Path to the binary firmware file (NULL = no firmware).
 * @param ram_size Size of the shared RAM in bytes.
 */
void rv_init(const char *firmware, size_t ram_size);

/**
 * Initialize the emulator from an ELF executable file.
 *
 * Parses the ELF header and program headers to load PT_LOAD segments
 * at their correct virtual addresses, then returns the entry point.
 * Supports 32-bit little-endian RISC-V ELF files (EM_RISCV = 0xF3).
 *
 * This enables Herve to run binaries compiled for Spike directly,
 * without needing an intermediate objcopy -O binary conversion step.
 *
 * @param elf_path Path to the ELF executable file.
 * @param ram_size Size of the shared RAM in bytes (must cover all segments).
 * @return Entry point address (e_entry from ELF header), or 0 on failure.
 */
uint32_t rv_init_elf(const char *elf_path, size_t ram_size);

/**
 * Load an ELF executable from an in-memory buffer into pre-allocated RAM.
 *
 * Parses the ELF header and program headers to load PT_LOAD segments
 * at their correct virtual addresses, then returns the entry point.
 * Unlike rv_init_elf(), this function does NOT allocate or free memory —
 * it assumes the RAM buffer is already set up via rv_init(), rv_set_ram(),
 * or a prior rv_init_elf() call.
 *
 * Supports 32-bit little-endian RISC-V ELF files (EM_RISCV = 0xF3).
 *
 * @param elf_data Pointer to the ELF file data in memory.
 * @param elf_size Size of the ELF data in bytes.
 * @return Entry point address (e_entry from ELF header), or 0 on failure.
 */
uint32_t rv_load_elf(const uint8_t *elf_data, size_t elf_size);

/**
 * Initialize the emulator from an in-memory buffer.
 * Useful when firmware is embedded in the testbench binary.
 * @param data Pointer to firmware binary data (NULL = no firmware).
 * @param size Size of firmware data in bytes.
 * @param ram_size Size of the shared RAM in bytes.
 */
void rv_init_from_buffer(const uint8_t *data, size_t size, size_t ram_size);

/**
 * Reset the emulator.
 * @param pc Initial Program Counter.
 */
void rv_reset(uint32_t pc);

/**
 * Execute instructions.
 * @param max_instructions Maximum number of instructions to execute.
 * @return Number of instructions actually executed.
 */
int rv_step(int max_instructions);

/**
 * Set interrupt bitmask.
 * @param mask Bitmask of pending interrupts.
 */
void rv_set_irq(uint32_t mask);

/**
 * Get pointer to shared RAM.
 */
void* rv_get_ram(void);

/**
 * Set the shared RAM buffer externally.
 * This allows the caller to provide a pre-allocated buffer (e.g. via mmap)
 * instead of having rv_init() allocate internally via malloc.
 * Must be called BEFORE rv_init() or rv_init_from_buffer().
 * @param buf Pointer to the RAM buffer.
 * @param size Size of the RAM buffer in bytes.
 */
void rv_set_ram(void *buf, size_t size);

/**
 * Get current Program Counter.
 */
uint32_t rv_get_pc(void);

/**
 * Read a register value.
 * @param reg Register index (0-31).
 * @return Current value of the register.
 */
uint32_t rv_get_reg(unsigned reg);

/**
 * Get current cycle count (mcycle CSR).
 */
uint64_t rv_get_cycles(void);

/**
 * Check if the simulation has halted (e.g., via HTIF exit).
 */
bool rv_is_halted(void);

/**
 * Get the exit code from HTIF (valid when rv_is_halted() is true).
 */
int rv_get_exit_code(void);

/**
 * Get symbol name at a given address.
 */
const char* herve_get_symbol_at(uint32_t addr);

#ifdef __cplusplus
}
#endif

void rv_profiler_set_arch(const herve_arch_t *arch);
void rv_profiler_enable(bool enable);
bool rv_profiler_is_enabled(void);
void rv_profiler_record_insn(herve_insn_type_t type, uint32_t pc, uint32_t insn);
void rv_profiler_report_csv(const char *filename);

#endif // RV32_DPI_H
