/*
 * HQC-256 keeps the large KEM/PKE scratch buffers in .bss. The default
 * X-HEEP on-chip linker reserves 32 KiB stack and 16 KiB heap; this app does
 * not use malloc, and the remaining stack frames are small after scratch
 * migration.
 */
__asm__(".globl __stack_size\n"
        ".set __stack_size, 0x3000\n"
        ".globl __heap_size\n"
        ".set __heap_size, 0x0400\n");
