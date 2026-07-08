/* jolt_vm.h — single-header for jolt guest integration (C)
 * Requires: clang/gcc with -std=gnu99 or later
 *           (register asm extension + ULL literals in #if)
 *
 * Addresses are macro-computed from config values to mirror
 * common/src/jolt_device.rs:296 (MemoryLayout::new).
 * All macros are preprocessor-safe — usable in #if directives.
 *
 * Usable from both C and C++ (wraps extern "C" automatically).
 */
#ifndef JOLT_VM_H
#define JOLT_VM_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===================================================================
 * USER CONFIGURATION — must match the Rust CLI's MemoryConfig.
 * Advice sizes MUST be powers of two (or zero).
 *
 * Override before including this header, e.g.:
 *   #define JOLT_VM_MAX_INPUT_SIZE  (1 << 20)
 *   #include "jolt_vm.h"
 * =================================================================== */
#ifndef JOLT_VM_MAX_INPUT_SIZE
#define JOLT_VM_MAX_INPUT_SIZE            4096
#endif
#ifndef JOLT_VM_MAX_OUTPUT_SIZE
#define JOLT_VM_MAX_OUTPUT_SIZE           4096
#endif
#ifndef JOLT_VM_MAX_TRUSTED_ADVICE_SIZE
#define JOLT_VM_MAX_TRUSTED_ADVICE_SIZE   4096
#endif
#ifndef JOLT_VM_MAX_UNTRUSTED_ADVICE_SIZE
#define JOLT_VM_MAX_UNTRUSTED_ADVICE_SIZE 4096
#endif

/* ===================================================================
 * CONSTANTS
 * =================================================================== */
#define JOLT_VM_RAM_START 0x80000000ULL

/* clzll — override to use a platform-specific implementation */
#ifndef JOLT_VM_BUILTIN_CLZLL
#define JOLT_VM_BUILTIN_CLZLL __builtin_clzll
#endif

/* ===================================================================
 * INTERNAL MACROS — constexpr-equivalent computation of MemoryLayout
 * (common/src/jolt_device.rs:296-437).
 * =================================================================== */

/* align_up to 8 bytes — uses /8*8 to stay preprocessor-safe */
#define JOLT_VM_D_ALIGN8(v) (((v) + 7) / 8 * 8)

/* next_power_of_two via clzll (compile-time for constant v) */
#define JOLT_VM_D_NEXT_POW2(v) \
    ((uint64_t)(v) <= 1 ? 1 : (uint64_t)1 << (64 - JOLT_VM_BUILTIN_CLZLL((unsigned long long)((uint64_t)(v) - 1))))

/* Aligned config sizes */
#define JOLT_VM_D_TA  JOLT_VM_D_ALIGN8(JOLT_VM_MAX_TRUSTED_ADVICE_SIZE)
#define JOLT_VM_D_UA  JOLT_VM_D_ALIGN8(JOLT_VM_MAX_UNTRUSTED_ADVICE_SIZE)
#define JOLT_VM_D_IN  JOLT_VM_D_ALIGN8(JOLT_VM_MAX_INPUT_SIZE)
#define JOLT_VM_D_OUT JOLT_VM_D_ALIGN8(JOLT_VM_MAX_OUTPUT_SIZE)

/* io_bytes = next_pow2((in + ta + ua + out + 16) / 8) * 8  (jolt_device.rs:337-351) */
#define JOLT_VM_D_IO_BYTES \
    (JOLT_VM_D_NEXT_POW2((JOLT_VM_D_IN + JOLT_VM_D_TA + JOLT_VM_D_UA + JOLT_VM_D_OUT + 16) / 8) * 8)

/* ===================================================================
 * DERIVED ADDRESSES — compile-time constants.
 * input_start is order-independent (ta+ua == ua+ta) so no branching.
 * =================================================================== */

/* input_start = RAM_START - io_bytes + ta + ua  (jolt_device.rs:361-397) */
#define JOLT_VM_INPUT_START       (JOLT_VM_RAM_START - JOLT_VM_D_IO_BYTES + JOLT_VM_D_TA + JOLT_VM_D_UA)
#define JOLT_VM_OUTPUT_START      (JOLT_VM_INPUT_START + JOLT_VM_D_IN)
#define JOLT_VM_OUTPUT_END        (JOLT_VM_OUTPUT_START + JOLT_VM_D_OUT)
#define JOLT_VM_PANIC_ADDR        JOLT_VM_OUTPUT_END
#define JOLT_VM_TERMINATION_ADDR  (JOLT_VM_PANIC_ADDR + 8)

/* Sanity checks — guard against invalid config values.
 * These use only preprocessor-safe arithmetic (no __builtin_clzll). */

/* Advice sizes must be powers of two or zero (jolt_device.rs:326-333) */
#if JOLT_VM_MAX_TRUSTED_ADVICE_SIZE != 0 && \
    (JOLT_VM_MAX_TRUSTED_ADVICE_SIZE & (JOLT_VM_MAX_TRUSTED_ADVICE_SIZE - 1)) != 0
#error "JOLT_VM_MAX_TRUSTED_ADVICE_SIZE must be a power of two or zero"
#endif
#if JOLT_VM_MAX_UNTRUSTED_ADVICE_SIZE != 0 && \
    (JOLT_VM_MAX_UNTRUSTED_ADVICE_SIZE & (JOLT_VM_MAX_UNTRUSTED_ADVICE_SIZE - 1)) != 0
#error "JOLT_VM_MAX_UNTRUSTED_ADVICE_SIZE must be a power of two or zero"
#endif

/* I/O region must fit below RAM_START (prevents address underflow) */
#if (JOLT_VM_D_TA + JOLT_VM_D_UA + JOLT_VM_D_IN + JOLT_VM_D_OUT + 16) >= JOLT_VM_RAM_START
#error "JOLT_VM: I/O region exceeds RAM_START (0x80000000) — reduce config sizes"
#endif

/* ===================================================================
 * VirtualHostIO call IDs (jolt-platform/src/print.rs:16)
 * =================================================================== */
#define JOLT_VM_PRINT_CALL_ID 0x505249u  /* "PRI" */
#define JOLT_VM_PRINT_STRING  1u
#define JOLT_VM_PRINT_LINE    2u

/* ===================================================================
 * Postcard &[u8] reader (zero-copy, stable for entire execution)
 * =================================================================== */
typedef struct {
    const uint8_t *data;
    size_t len;
} jolt_vm_byte_view_t;

static inline jolt_vm_byte_view_t jolt_vm_read_input(void) {
    const uint8_t *p = (const uint8_t *)JOLT_VM_INPUT_START;
    size_t n = 0;
    int s = 0;
    while (1) {
        uint8_t b = *p++;
        n |= (size_t)(b & 0x7f) << s;
        if (!(b & 0x80)) break;
        s += 7;
    }
    jolt_vm_byte_view_t v = { p, n };
    return v;
}

/* ===================================================================
 * Postcard &[u8] writer
 * Varint length prefix + raw data, contiguous (postcard format).
 * =================================================================== */
static inline void jolt_vm_write_output(const uint8_t *data, size_t len) {
    uint8_t *p = (uint8_t *)JOLT_VM_OUTPUT_START;

    /* Varint length prefix (1-5 bytes, byte stores) */
    size_t n = len;
    while (n >= 0x80) { *p++ = (uint8_t)(n | 0x80); n >>= 7; }
    *p++ = (uint8_t)n;

    /* Data — must immediately follow varint for postcard compatibility */
    memcpy(p, data, len);
}

/* ===================================================================
 * VirtualHostIO print (not proven; host-side only)
 * Encoding: .insn i 0x5B, 2, x0, x0, 0  (jolt-platform/src/print.rs:38)
 * =================================================================== */
static inline void jolt_vm_print(const void *buf, size_t len) {
    register uint64_t a0 asm("x10") = JOLT_VM_PRINT_CALL_ID;
    register uint64_t a1 asm("x11") = (uint64_t)buf;
    register uint64_t a2 asm("x12") = (uint64_t)len;
    register uint64_t a3 asm("x13") = JOLT_VM_PRINT_STRING;
    __asm__ volatile(
        ".insn i 0x5B, 2, x0, x0, 0"
        : : "r"(a0), "r"(a1), "r"(a2), "r"(a3) : "memory"
    );
}

static inline void jolt_vm_println(const void *buf, size_t len) {
    register uint64_t a0 asm("x10") = JOLT_VM_PRINT_CALL_ID;
    register uint64_t a1 asm("x11") = (uint64_t)buf;
    register uint64_t a2 asm("x12") = (uint64_t)len;
    register uint64_t a3 asm("x13") = JOLT_VM_PRINT_LINE;
    __asm__ volatile(
        ".insn i 0x5B, 2, x0, x0, 0"
        : : "r"(a0), "r"(a1), "r"(a2), "r"(a3) : "memory"
    );
}

/* ===================================================================
 * Exit / panic (termination via PC stall: tracer/src/lib.rs:216)
 * =================================================================== */
__attribute__((noreturn))
static inline void jolt_vm_exit(void) {
    *(volatile uint8_t *)JOLT_VM_TERMINATION_ADDR = 1;
    __asm__ volatile("j .");
    __builtin_unreachable();
}

__attribute__((noreturn))
static inline void jolt_vm_panic(void) {
    *(volatile uint8_t *)JOLT_VM_PANIC_ADDR = 1;
    __asm__ volatile("j .");
    __builtin_unreachable();
}

#ifdef __cplusplus
}
#endif

#endif /* JOLT_VM_H */
