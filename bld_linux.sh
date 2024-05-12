#!/bin/sh
# bld_linux.sh - build mtools on Linux.

rm -f temp

gcc -Wno-format-truncation -g -o temp msend.c -l rt
if [ $? -ne 0 ]; then exit 1; fi
mv temp Linux64/msend

gcc -Wno-format-truncation -g -o temp mdump.c -l rt
if [ $? -ne 0 ]; then exit 1; fi
mv temp Linux64/mdump

gcc -Wno-format-truncation -g -o temp mpong.c -l rt -l m
if [ $? -ne 0 ]; then exit 1; fi
mv temp Linux64/mpong
