#!/bin/bash
set -e
# Cross-build of the book binaries. Run from book/ with: docker run --rm -v "$PWD:/work" debian:bookworm-slim bash /work/build.sh
# Needs in book/: toolchain.tar.bz2 (Bootlin armv7-eabihf glibc), libs/ (from the book, see tools/collect-libs.app),
# src/inkview.h (+ SDK headers, see README). Afterwards run patch.ps1 on out/pageturn and out/pageturn-ui.
mkdir -p /work/out
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq bzip2 >/dev/null 2>&1
mkdir -p /tc
tar xjf /work/toolchain.tar.bz2 -C /tc --strip-components=1
export PATH=/tc/bin:$PATH
cd /work
CC=arm-buildroot-linux-gnueabihf-gcc
COMMON="-O2 -static-libgcc -I src -L libs -l:libinkview.so -l:libc.so.6 -l:libpthread.so.0 -l:libdl.so.2 -l:libm.so.6 -Wl,--allow-shlib-undefined -Wl,--dynamic-linker=/lib/ld-linux.so.3 -Wl,-rpath=/var/tmp/ptlibs"
echo "== server =="
$CC -Wall -o out/pageturn src/server.c $COMMON
echo "== ui =="
$CC -Wall -Wno-unused-result -o out/pageturn-ui src/ui.c src/qr.c $COMMON
echo "compile exit=0"
