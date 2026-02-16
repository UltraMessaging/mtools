rem bld.bat

cl /std:c11 /W4 /O2 /MT /nologo /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE msend.c ws2_32.lib /Fe:Win64\msend.exe

cl /std:c11 /W4 /O2 /MT /nologo /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE mdump.c ws2_32.lib /Fe:Win64\mdump.exe

cl /std:c11 /W4 /O2 /MT /nologo /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE mpong.c ws2_32.lib /Fe:Win64\mpong.exe
