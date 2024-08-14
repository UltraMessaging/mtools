#!/bin/sh
# bld.sh - build mtools on Linux.

if [ $# -ge 1 ]; then :
  D="$1"
  shift
  cd "$D"
fi

gcc -Wall -g -o msnd msnd.c -l rt
if [ $? -ne 0 ]; then exit 1; fi

gcc -Wall -g -o mrcv mrcv.c -l rt
if [ $? -ne 0 ]; then exit 1; fi

gcc -Wall -g -o mforwarder mforwarder.c -l rt -l onload_ext
if [ $? -ne 0 ]; then exit 1; fi
