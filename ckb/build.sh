#!/usr/bin/env bash
set -ex

CLANG="${CLANG:-clang-18}"
BASE_CFLAGS="${BASE_CFLAGS:---target=riscv32 -march=rv32im -DPAGE_SIZE=4096 -O3}"
N_PROC="${N_PROC:-$(nproc)}"

mkdir -p release
CC="${CLANG}" CFLAGS="${BASE_CFLAGS}" \
  ./configure \
    --target=riscv32-linux-musl \
    --disable-shared \
    --with-malloc=oldmalloc \
    --prefix=`pwd`/release
make AR="${CLANG/clang/llvm-ar}" RANLIB="${CLANG/clang/llvm-ranlib}" -j ${N_PROC}
make AR="${CLANG/clang/llvm-ar}" RANLIB="${CLANG/clang/llvm-ranlib}" install
rm -rf release/bin

rm -rf release/lib/libgcc.a

CKB_FILES=("crt1" "hijack_syscall")
for f in ${CKB_FILES[@]}; do
  $CLANG $BASE_CFLAGS \
    -nostdinc \
    -I./arch/riscv64 -I./arch/generic -Iobj/src/internal -I./src/include -I./src/internal -Iobj/include -I./include \
    -Wall -Werror \
    -c ckb/$f.c -o release/$f.o

  "${CLANG/clang/llvm-ar}" rc release/lib/libgcc.a release/$f.o

  rm -rf release/$f.o
done

CKB_HEADERS=("musl_options")
rm -rf release/include/ckb
mkdir -p release/include/ckb
for f in ${CKB_HEADERS[@]}; do
  cp ckb/$f.h release/include/ckb/$f.h
done

rm -rf release/include/openvm
mkdir -p release/include/openvm
cp ckb/openvm* release/include/openvm/
