/* Stubs for __sync_fetch_and_add_* for single-core RV32 without A extension.
 *
 * The riscv-tests benchmark barrier() function in util.h uses
 * __sync_fetch_and_add() which GCC compiles to __sync_fetch_and_add_4
 * on RV32.  Without the A (atomic) extension, this built-in is not
 * available as an instruction and libatomic is not present in the
 * bare-metal toolchain.
 *
 * Since we run single-core, these can be simple non-atomic operations.
 */

#include <stdint.h>

/* The built-in signature is: type __sync_fetch_and_add_N(volatile void*, type) */
uint32_t __sync_fetch_and_add_4(volatile void *vptr, uint32_t val) {
    volatile uint32_t *ptr = (volatile uint32_t *)vptr;
    uint32_t old = *ptr;
    *ptr = old + val;
    return old;
}
