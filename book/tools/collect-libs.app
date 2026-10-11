#!/bin/sh
# Developer helper: copies the firmware libraries needed to LINK the binaries
# from the book to /mnt/ext1/pageturn-libs (visible as a folder when connected to a PC).
# Not needed for normal use.
OUT=/mnt/ext1/pageturn-libs
mkdir -p "$OUT"
for n in libinkview.so libc.so.6 libdl.so.2 libm.so.6 libpthread.so.0 librt.so.1 ld-linux.so.3; do
  for d in /ebrmain/lib /lib /usr/lib; do
    [ -e "$d/$n" ] && { cp -fL "$d/$n" "$OUT/"; break; }
  done
done
