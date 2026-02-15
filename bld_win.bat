rem bld.bat

cl /std:c11 /W4 /O2 /MT /nologo msend.c ws2_32.lib /Fe:Win64\msend.exe

cl /std:c11 /W4 /O2 /MT /nologo mdump.c ws2_32.lib /Fe:Win64\mdump.exe

cl /std:c11 /W4 /O2 /MT /nologo mpong.c ws2_32.lib /Fe:Win64\mpong.exe
