#include <features.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/uio.h>

#include "bits/syscall.h"
#include "sys/mman.h"
#include "syscall_arch.h"
#include "openvm_vm.h"

struct syscall_context {
  long n;
  long a;
  long b;
  long c;
  long d;
  long e;
  long f;
  int *processed;
};

extern char _end[];

static long default_brk_min = (long)_end;
static long default_brk_max = OPENVM_HEAP_END;

weak_alias(default_brk_min, __ckb_hijack_brk_min);
weak_alias(default_brk_max, __ckb_hijack_brk_max);

static long default_brk(void *c) {
  struct syscall_context *context = (struct syscall_context *)c;

  if (context->n != SYS_brk) {
    return (long)-1;
  }

  *(context->processed) = 1;
  long a = context->a;
  if (a == 0) {
    return (long)__ckb_hijack_brk_min;
  } else if ((a >= __ckb_hijack_brk_min) && (a <= __ckb_hijack_brk_max)) {
    return a;
  } else {
    return (long)-1;
  }
}
weak_alias(default_brk, __ckb_hijack_brk);

/* mmap always result in a failure to give alloc functions a chance to recover
 */
static long default_mmap(void *c) {
  struct syscall_context *context = (struct syscall_context *)c;

  if (context->n != SYS_mmap2) {
    return (long)-1;
  }

  *(context->processed) = 1;
  return (long)-1;
}
weak_alias(default_mmap, __ckb_hijack_mmap);

/* madvise with MADV_DONTNEED will be ignored, others will not be processed
 */
static long default_madvise(void *c) {
  struct syscall_context *context = (struct syscall_context *)c;

  if (context->n != SYS_madvise) {
    return (long)-1;
  }
  if (context->c != MADV_DONTNEED) {
    return (long)-1;
  }

  *(context->processed) = 1;
  return (long)0;
}
weak_alias(default_madvise, __ckb_hijack_madvise);

/* set_tid_address is an always success with 0 as return result
 */
static long default_set_tid_address(void *c) {
  struct syscall_context *context = (struct syscall_context *)c;

  if (context->n != SYS_set_tid_address) {
    return (long)-1;
  }

  *(context->processed) = 1;
  *((int *)context->a) = 0;
  return 0;
}

/* ioctl is tested when writing to stdout
 */
static long default_ioctl(void *c) {
  struct syscall_context *context = (struct syscall_context *)c;

  if (context->n != SYS_ioctl) {
    return (long)-1;
  }

  int fd = (int)context->a;
  if (fd != 1 && fd != 2) {
    /* Only ioctl to stdout/stderr is processed */
    return (long)-1;
  }
  int op = (int)context->b;
  if (op != TIOCGWINSZ) {
    return (long)-1;
  }

  struct winsize *wsz = (struct winsize *)context->c;
  wsz->ws_row = 80;
  wsz->ws_col = 25;
  wsz->ws_xpixel = 14;
  wsz->ws_ypixel = 14;
  *(context->processed) = 1;
  return 0;
}
weak_alias(default_ioctl, __ckb_hijack_ioctl);

/* writev result is reinterpreted to OpenVM IO handler
 */
static long default_writev(void *c) {
  struct syscall_context *context = (struct syscall_context *)c;

  if (context->n != SYS_writev) {
    return (long)-1;
  }

  int fd = (int)context->a;
  if (fd != 1 && fd != 2) {
    /* Only writev to stdout/stderr is processed */
    return (long)-1;
  }

  *(context->processed) = 1;
  const struct iovec *iov = (const struct iovec *)context->b;
  int iovcnt = (int)context->c;

  ssize_t total = 0;
  for (int i = 0; i < iovcnt; i++) {
    openvm_print(iov[i].iov_base, iov[i].iov_len);
    total += iov[i].iov_len;
  }
  return total;
}
weak_alias(default_writev, __ckb_hijack_writev);

hidden long default_hijack_syscall(long n, long a, long b, long c, long d,
                                   long e, long f, int *processed) {
  struct syscall_context context = {.n = n,
                                    .a = a,
                                    .b = b,
                                    .c = c,
                                    .d = d,
                                    .e = e,
                                    .f = f,
                                    .processed = processed};

  long code = __ckb_hijack_brk(&context);
  if (*(context.processed) != 0) {
    return code;
  }
  code = __ckb_hijack_mmap(&context);
  if (*(context.processed) != 0) {
    return code;
  }
  code = __ckb_hijack_writev(&context);
  if (*(context.processed) != 0) {
    return code;
  }
  code = __ckb_hijack_ioctl(&context);
  if (*(context.processed) != 0) {
    return code;
  }
  code = default_set_tid_address(&context);
  if (*(context.processed) != 0) {
    return code;
  }
  code = __ckb_hijack_madvise(&context);
  if (*(context.processed) != 0) {
    return code;
  }

  return (long)-1;
}
weak_alias(default_hijack_syscall, __ckb_hijack_syscall);
