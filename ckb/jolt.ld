/*
 * jolt.ld — linker script for bare-metal C/C++ guests on jolt
 *
 * Compile: clang++ --target=riscv64-unknown-elf -march=rv64imac -mabi=lp64 \
 *                  -mcmodel=medany -nostdlib -static -ffreestanding \
 *                  -Wl,-T,jolt.ld -o guest.elf ...
 *
 * Memory layout mirrors jolt's MemoryLayout (common/src/jolt_device.rs:296):
 *   program image → canary(128) → stack(grows down) → heap(grows up)
 *
 * Override sizes at link time:
 *   ld.lld ... --defsym,__heap_size=268435456 --defsym,__stack_size=16384 -T jolt.ld
 *
 * __heap_size and __stack_size must match Rust CLI's MemoryConfig.
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

/* Overridable defaults — use --defsym to change without editing this file.
 * PROVIDE means: only define if not already set (e.g. by --defsym).
 *
 * __ram_length: total RAM region. Must be ≥ program + canary + stack + heap.
 * __heap_size / __stack_size: reserved within RAM, must match MemoryConfig.
 */
PROVIDE(__ram_length          = 64M);
PROVIDE(__heap_size           = 32M);
PROVIDE(__stack_size          = 4K);
__stack_canary_size = 128;

MEMORY {
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = __ram_length
}

PHDRS {
    text   PT_LOAD FLAGS(5);   /* R-X */
    rodata PT_LOAD FLAGS(4);   /* R-- */
    data   PT_LOAD FLAGS(6);   /* RW- */
    tls    PT_TLS;
}

SECTIONS {
    . = 0x80000000;

    .text : {
        KEEP(*(.text.boot))    /* ZeroOS convention */
        KEEP(*(.text.init))    /* our convention */
        *(.text .text.*)
        . = ALIGN(4);
    } > RAM : text

    .rodata : {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        . = ALIGN(8);
    } > RAM : rodata

    /* EH frames — terminated by LONG(0) for __register_frame safety.
     * If compiling with -fno-exceptions, replace with /DISCARD/. */
    .eh_frame_hdr : ALIGN(4) {
        PROVIDE_HIDDEN(__eh_frame_hdr_start = .);
        KEEP(*(.eh_frame_hdr))
        PROVIDE_HIDDEN(__eh_frame_hdr_end = .);
        . = ALIGN(8);
    } > RAM : rodata

    .eh_frame : ALIGN(8) {
        PROVIDE_HIDDEN(__eh_frame_start = .);
        KEEP(*(.eh_frame))
        KEEP(*(.eh_frame.*))
        LONG(0)
        PROVIDE_HIDDEN(__eh_frame_end = .);
        . = ALIGN(8);
    } > RAM : rodata

    /* C++ constructors / destructors — must come before .data (GNU convention).
     * Musl's __libc_start_init iterates __init_array before calling main. */
    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > RAM : data

    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > RAM : data

    .data : {
        *(.data .data.*)
        PROVIDE(__sdata_start = .);
        *(.sdata .sdata.*)
        . = ALIGN(8);
    } > RAM : data

    /* gp placed 0x800 into .sdata for ±2KB gp-relative addressing */
    PROVIDE(__global_pointer$ = __sdata_start + 0x800);

    /* TLS sections — needed for musl/libc++ thread-local storage */
    .tdata : ALIGN(16) {
        PROVIDE_HIDDEN(__tdata_start = .);
        *(.tdata .tdata.*)
        . = . + 8;  /* force non-empty */
        PROVIDE_HIDDEN(__tdata_end = .);
    } > RAM : data : tls

    .tbss : ALIGN(16) {
        PROVIDE_HIDDEN(__tbss_start = .);
        *(.tbss .tbss.*)
        *(.tcommon)
        . = . + 8;  /* force non-empty */
        PROVIDE_HIDDEN(__tbss_end = .);
    } > RAM : data : tls

    .bss : {
        PROVIDE(__bss_start = .);
        *(.bss .bss.*)
        *(COMMON)
        *(.sbss .sbss.*)
        . = ALIGN(8);
        PROVIDE(__bss_end = .);
    } > RAM : data

    __program_end = __bss_end;
    ASSERT(__program_end <= ORIGIN(RAM) + LENGTH(RAM), "program image does not fit in RAM")

    /* Stack: canary + stack, grows DOWN from __stack_top.
     * Must match MemoryLayout: program → canary → stack → heap. */
    . = ALIGN(8);
    __stack_bottom = .;
    . = . + __stack_canary_size;
    . = . + __stack_size;
    __stack_top = .;

    /* Heap: grows UP from __heap_start. */
    . = ALIGN(8);
    __heap_start = .;
    . = . + __heap_size;
    __heap_end = .;

    __memory_end = .;
    _end = .;  /* alias for musl/libc++ malloc */
}

/* Verify everything fits within RAM. */
ASSERT(__memory_end <= ORIGIN(RAM) + LENGTH(RAM),
       "program + canary + stack + heap exceeds __ram_length — increase __ram_length")

/* tohost switches the emulator into riscv-tests mode (tracer/src/emulator/mod.rs:270). */
ASSERT(!DEFINED(tohost), "tohost symbol must NOT be defined for jolt guests");
