rem Windows build

cl -ML -DWIN32_LEAN_AND_MEAN -DWIN32_EXTRA_LEAN /Ob1 /Oi /Ot /Oy /G6  -Fomsend.obj -c msend.c
cl -ML -DWIN32_LEAN_AND_MEAN -DWIN32_EXTRA_LEAN /Ob1 /Oi /Ot /Oy /G6  -Fomdump.obj -c mdump.c
cl -ML -DWIN32_LEAN_AND_MEAN -DWIN32_EXTRA_LEAN /Ob1 /Oi /Ot /Oy /G6  -Fompong.obj -c mpong.c
link /OUT:msend.exe /INCREMENTAL:NO /NOLOGO -subsystem:console,5.0 kernel32.lib  ws2_32.lib mswsock.lib advapi32.lib /DEBUG /NODEFAULTLIB:libcmt /NOLOGO /INCREMENTAL:no /MACHINE:I386 /SUBSYSTEM:console  msend.obj
link /OUT:mdump.exe /INCREMENTAL:NO /NOLOGO -subsystem:console,5.0 kernel32.lib  ws2_32.lib mswsock.lib advapi32.lib /DEBUG /NODEFAULTLIB:libcmt /NOLOGO /INCREMENTAL:no /MACHINE:I386 /SUBSYSTEM:console  mdump.obj
link /OUT:mpong.exe /INCREMENTAL:NO /NOLOGO -subsystem:console,5.0 kernel32.lib  ws2_32.lib mswsock.lib advapi32.lib /DEBUG /NODEFAULTLIB:libcmt /NOLOGO /INCREMENTAL:no /MACHINE:I386 /SUBSYSTEM:console  mpong.obj
