/*
 * openvm_sha256.h — SHA-256 built on OpenVM's sha256_compress custom
 * instruction.
 *
 * OpenVM provides only the SHA-256 compression function (one 64-byte block ->
 * next 32-byte state) as a custom instruction (R-type, opcode 0x0b, funct3
 * 0b100, funct7 0x02). This header supplies the surrounding NIST FIPS 180-4
 * scaffolding: initial state, message padding, and block iteration. The
 * output matches any standard SHA-256 implementation, so the host can
 * verify with `sha256sum` or `openssl dgst -sha256`.
 *
 * Layout choice (matches OpenVM's openvm-sha2-guest crate):
 *   state: 8 u32 words held in natural C order (little-endian on RV32). The
 *          compress instruction reads/writes these as "8 u32 words in
 *          little-endian memory order", so passing (uint8_t*)state directly
 *          is correct.
 *   block: 64 raw bytes; the compress instruction interprets them per spec
 *          (16 big-endian u32 words for the message schedule).
 *   digest: 32 bytes in big-endian word order (FIPS 180-4 §5.2).
 */
#ifndef OPENVM_SHA256_H
#define OPENVM_SHA256_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SHA-256 compression intrinsic.
 *   out:   32 bytes (next state, written by the VM)
 *   state: 32 bytes (current state, 8 u32 words in LE memory order)
 *   block: 64 raw bytes (interpreted per FIPS 180-4) */
static inline void openvm_sha256_compress(uint8_t* out, const uint8_t* state,
                                          const uint8_t* block) {
  register uintptr_t a0 asm("x10") = (uintptr_t)out;
  register uintptr_t a1 asm("x11") = (uintptr_t)state;
  register uintptr_t a2 asm("x12") = (uintptr_t)block;
  __asm__ volatile(".insn r 0x0b, 0b100, 0x02, x10, x11, x12"
                   :
                   : "r"(a0), "r"(a1), "r"(a2)
                   : "memory");
}

typedef struct {
  uint32_t state[8];
  uint8_t buffer[64];
  uint64_t len;
  uint32_t idx;
} openvm_sha256_ctx;

/* SHA-256 initial hash value (FIPS 180-4 §5.3.3). */
static const uint32_t OPENVM_SHA256_H0[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
};

static inline void openvm_sha256_init(openvm_sha256_ctx* ctx) {
  for (int i = 0; i < 8; i++) ctx->state[i] = OPENVM_SHA256_H0[i];
  ctx->len = 0;
  ctx->idx = 0;
  memset(ctx->buffer, 0, 64);
}

/* Run one compression: pass current state + 64-byte block to the OpenVM
 * sha256_compress instruction, then copy the 32 output bytes back into
 * ctx->state. Both the state input and output use natural C memory order
 * (LE on RV32), so a byte-copy preserves word values. */
static inline void openvm_sha256_compress_block(openvm_sha256_ctx* ctx) {
  uint8_t next[32];
  openvm_sha256_compress(next, (const uint8_t*)ctx->state, ctx->buffer);
  memcpy(ctx->state, next, 32);
}

static inline void openvm_sha256_update(openvm_sha256_ctx* ctx,
                                        const uint8_t* data, size_t len) {
  ctx->len += len;
  while (len > 0) {
    size_t to_copy = 64 - ctx->idx;
    if (to_copy > len) to_copy = len;
    memcpy(ctx->buffer + ctx->idx, data, to_copy);
    ctx->idx += (uint32_t)to_copy;
    data += to_copy;
    len -= to_copy;
    if (ctx->idx == 64) {
      openvm_sha256_compress_block(ctx);
      ctx->idx = 0;
    }
  }
}

/* Apply FIPS 180-4 §5 padding: append 0x80, zero-fill, then 64-bit big-endian
 * bit length. Writes at least one final compression (possibly two if the
 * current buffer is too full to fit the length in one block). */
static inline void openvm_sha256_finalize(openvm_sha256_ctx* ctx,
                                          uint8_t out[32]) {
  uint64_t bit_len = ctx->len * 8u;

  /* Append 0x80 (we always have at least one byte free: idx < 64 invariant). */
  ctx->buffer[ctx->idx++] = 0x80u;

  /* If not enough room for the 8 length bytes, pad this block and compress. */
  if (ctx->idx > 56) {
    while (ctx->idx < 64) ctx->buffer[ctx->idx++] = 0;
    openvm_sha256_compress_block(ctx);
    ctx->idx = 0;
  }
  while (ctx->idx < 56) ctx->buffer[ctx->idx++] = 0;

  /* 64-bit big-endian bit length. */
  for (int i = 0; i < 8; i++) {
    ctx->buffer[56 + i] = (uint8_t)(bit_len >> (56 - 8 * i));
  }
  openvm_sha256_compress_block(ctx);

  /* Emit the digest in big-endian byte order per FIPS 180-4. */
  for (int i = 0; i < 8; i++) {
    out[4 * i + 0] = (uint8_t)(ctx->state[i] >> 24);
    out[4 * i + 1] = (uint8_t)(ctx->state[i] >> 16);
    out[4 * i + 2] = (uint8_t)(ctx->state[i] >> 8);
    out[4 * i + 3] = (uint8_t)(ctx->state[i] >> 0);
  }
}

/* One-shot convenience. */
static inline void openvm_sha256(const uint8_t* data, size_t len,
                                 uint8_t out[32]) {
  openvm_sha256_ctx ctx;
  openvm_sha256_init(&ctx);
  openvm_sha256_update(&ctx, data, len);
  openvm_sha256_finalize(&ctx, out);
}

#ifdef __cplusplus
}
#endif

#endif /* OPENVM_SHA256_H */
