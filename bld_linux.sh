#!/bin/sh
# bld_linux.sh - build mtools on Linux.


gcc -Wno-format-truncation -g -o Linux64/msend -l rt msend.c
if [ $? -ne 0 ]; then exit 1; fi

gcc -Wno-format-truncation -g -o Linux64/mdump -l rt mdump.c
if [ $? -ne 0 ]; then exit 1; fi

gcc -g -o Linux64/mpong mpong.c -l rt -l m
if [ $? -ne 0 ]; then exit 1; fi
