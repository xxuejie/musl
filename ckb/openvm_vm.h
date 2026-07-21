/*
 * OpenVM guest integration methods
 */
#ifndef OPENVM_VM_H
#define OPENVM_VM_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENVM_MEM_SIZE 0x20000000 /* 512 MiB total guest address space */
#define OPENVM_GUEST_MIN_MEM 0x00000400
#define OPENVM_STACK_TOP 0x00200400  /* sp starts here, grows DOWN */
#define OPENVM_TEXT_START 0x00200800 /* ELF text loaded here */

/*
 * Technically, OpenVM does not have this output region, but we carve a little
 * section in our experiment, so we are passing data back for VM to read.
 * For security those data should be hashed and included as public values
 */
#define OPENVM_CARVE_SIZE (32 * 1024 * 1024)
#define OPENVM_HEAP_END (OPENVM_MEM_SIZE - OPENVM_CARVE_SIZE)
#define OPENVM_CARVE_START OPENVM_HEAP_END

__attribute__((noreturn)) static inline void openvm_terminate_success(void) {
  __asm__ volatile(".insn i 0x0b, 0b000, x0, x0, 0");
  __builtin_unreachable();
}

__attribute__((noreturn)) static inline void openvm_terminate_panic(void) {
  __asm__ volatile(".insn i 0x0b, 0b000, x0, x0, 1");
  __builtin_unreachable();
}

static inline void openvm_print(const void* buf, uint32_t len) {
  register uintptr_t a0 asm("x10") = (uintptr_t)buf;
  register uintptr_t a1 asm("x11") = (uintptr_t)len;
  __asm__ volatile(".insn i 0x0b, 0b011, x10, x11, 1"
                   :
                   : "r"(a0), "r"(a1)
                   : "memory");
}

static inline void openvm_reveal_u32(uint32_t byte_offset, uint32_t value) {
  register uintptr_t a0 asm("x10") = (uintptr_t)byte_offset;
  register uintptr_t a1 asm("x11") = (uintptr_t)value;
  __asm__ volatile(".insn i 0x0b, 0b010, x10, x11, 0"
                   :
                   : "r"(a0), "r"(a1)
                   : "memory");
}

#ifdef __cplusplus
}
#endif

#endif /* OPENVM_VM_H */
