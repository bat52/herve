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
void rv_init(const char *firmware, int ram_size);

/**
 * Initialize the emulator from an in-memory buffer.
 * Useful when firmware is embedded in the testbench binary.
 * @param data Pointer to firmware binary data (NULL = no firmware).
 * @param size Size of firmware data in bytes.
 * @param ram_size Size of the shared RAM in bytes.
 */
void rv_init_from_buffer(const uint8_t *data, size_t size, int ram_size);

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
 * Get current Program Counter.
 */
uint32_t rv_get_pc(void);

#ifdef __cplusplus
}
#endif

#endif // RV32_DPI_H
