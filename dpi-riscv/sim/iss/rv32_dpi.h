#ifndef RV32_DPI_H
#define RV32_DPI_H

#include <stdint.h>
#include <stddef.h>

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

#ifdef __cplusplus
}
#endif

#endif // RV32_DPI_H
