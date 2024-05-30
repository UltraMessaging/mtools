#!/bin/sh
# bld.sh - build mtools on Linux.

gcc -Wall -g -o msnd msnd.c -l rt
if [ $? -ne 0 ]; then exit 1; fi

gcc -Wall -g -o mrcv mrcv.c -l rt
if [ $? -ne 0 ]; then exit 1; fi
