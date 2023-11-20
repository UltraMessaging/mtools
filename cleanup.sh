#!/bin/sh
# cleanup.sh
rm -rf x x.* *.x *.tmp tmp.* *.tmp[0-9] ~* .DS_Store .vs *.log *.recipe *.sdf *.opensdf *.suo

rm -rf x64 */x64 Debug */Debug

rm -rf */*.vcxproj.user
