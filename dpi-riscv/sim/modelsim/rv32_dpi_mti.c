/**
 * rv32_dpi_mti.c — C support library for ModelSim DPI-C testbenches.
 *
 * Provides DPI-C importable wrapper functions that bridge SystemVerilog
 * testbenches to the Herve ISS (rv32_dpi.c).
 *
 * Functions:
 *   rv_mti_write_ram(int word_offset, int value)
 *     — Write a 32-bit word to ISS RAM at word_offset * 4.
 *       Useful for pre-loading instruction sequences programmatically.
 *
 *   rv_mti_set_irq(int mask)
 *     — Thin wrapper around rv_set_irq().
 *       Exists because rv_set_irq takes uint32_t and direct DPI-C import
 *       may have type-matching issues with SV 'int'.
 *
 * Compile (shared library for ModelSim):
 *   gcc -shared -fPIC -I../sim/iss -o rv32_dpi_mti.so rv32_dpi_mti.c ../sim/iss/rv32_dpi.c
 *
 * Load in ModelSim:
 *   vsim -sv_lib rv32_dpi_mti ...
 */

#include "rv32_dpi.h"
#include <stdint.h>

/**
 * Write a 32-bit word to ISS RAM at the given word offset.
 *
 * @param word_offset  Word index into RAM (byte address = word_offset * 4)
 * @param value        32-bit value to write
 */
void rv_mti_write_ram(int word_offset, int value)
{
    uint32_t *ram = (uint32_t *)rv_get_ram();
    if (ram) {
        ram[word_offset] = (uint32_t)value;
    }
}

/**
 * Set interrupt bitmask — thin wrapper around rv_set_irq().
 *
 * @param mask  Bitmask of pending interrupts (bit 0 = external IRQ)
 */
void rv_mti_set_irq(int mask)
{
    rv_set_irq((uint32_t)mask);
}
