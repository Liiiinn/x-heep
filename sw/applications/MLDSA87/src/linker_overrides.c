/*
 * ML-DSA profiling keeps the large sign/verify scratch objects in .bss.
 * The benchmark does not use malloc, and the remaining stack frames are small
 * after scratch migration.
 */
__asm__(".globl __stack_size\n"
        ".set __stack_size, 0x3000\n"
        ".globl __heap_size\n"
        ".set __heap_size, 0x0400\n");
