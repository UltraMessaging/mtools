#!/bin/sh
# cp_win.sh - copy windows executables.

for F in mdump msend mpong; do :
  if [ -f x64/Debug/$F.exe ]; then cp x64/Debug/$F.exe Win64/; fi
done
