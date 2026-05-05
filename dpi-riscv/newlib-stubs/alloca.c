/* Minimal alloca() implementation for bare-metal RV32.
 *
 * Dhrystone uses alloca() for dynamic stack allocation during
 * initialization. Since we compile with -nostdlib, we provide
 * a simple bump-pointer allocator that carves memory from the
 * end of the BSS segment (_end symbol defined by test.ld).
 *
 * alloca() normally allocates on the stack, but for a bare-metal
 * benchmark with a single initialization call, a bump pointer
 * is sufficient. Memory is never freed.
 */

#include <stddef.h>

extern char _end;  /* defined by test.ld linker script */

static char *heap_ptr = 0;

void *alloca(size_t size)
{
    if (heap_ptr == 0) {
        heap_ptr = &_end;
    }

    /* Align to 16 bytes */
    size = (size + 15) & ~15;

    void *ptr = (void *)heap_ptr;
    heap_ptr += size;

    return ptr;
}
