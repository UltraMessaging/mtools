#!/bin/sh
# build_mtools.sh

D=`pwd`

rm -rf mtools mtools.zip mtools.tar.gz
if [ -d mtools -o -f mtools.zip -o -f mtools.tar.gz ]; then :
	echo ""; echo "Could not remove old mtools files" >&2
	exit 1
fi

mkdir mtools
cat /dev/null >build_mtools.log

# First build headless (so that the mtools dir is empty)
# Create a batch file to build the tools on headless
cat >build.bat <<__EOF__
mkdir $USER
copy /y $USER.1 $USER\\msend.c
copy /y $USER.2 $USER\\mdump.c
copy /y $USER.3 $USER\\mpong.c
cd $USER
del *.obj
del *.exe
call "C:\Program Files\Microsoft Visual Studio\VC98\Bin\VCVARS32.BAT"
rem cl -D_MT -D_DLL -MD -DWIN32_LEAN_AND_MEAN -DWIN32_EXTRA_LEAN /Ob1 /Oi /Ot /Oy /Z7 -D_CRT_SECURE_NO_DEPRECATE -D_CRT_NONSTDC_NO_DEPRECATE -Fd.\\ -Fomsend.obj -c msend.c
rem link /OUT:msend.exe /INCREMENTAL:NO /NOLOGO /subsystem:console kernel32.lib  ws2_32.lib mswsock.lib advapi32.lib bufferoverflowu.lib /NODEFAULTLIB:libcmt /MACHINE:I386 /SUBSYSTEM:console  msend.obj
rem cl -D_MT -D_DLL -MD -DWIN32_LEAN_AND_MEAN -DWIN32_EXTRA_LEAN /Ob1 /Oi /Ot /Oy /Z7 -D_CRT_SECURE_NO_DEPRECATE -D_CRT_NONSTDC_NO_DEPRECATE -Fd.\\ -Fomdump.obj -c mdump.c
rem link /OUT:mdump.exe /INCREMENTAL:NO /NOLOGO /subsystem:console kernel32.lib  ws2_32.lib mswsock.lib advapi32.lib bufferoverflowu.lib /NODEFAULTLIB:libcmt /MACHINE:I386 /SUBSYSTEM:console  mdump.obj
rem cl -D_MT -D_DLL -MD -DWIN32_LEAN_AND_MEAN -DWIN32_EXTRA_LEAN /Ob1 /Oi /Ot /Oy /Z7 -D_CRT_SECURE_NO_DEPRECATE -D_CRT_NONSTDC_NO_DEPRECATE -Fd.\\ -Fompong.obj -c mpong.c
rem link /OUT:mpong.exe /INCREMENTAL:NO /NOLOGO /subsystem:console kernel32.lib  ws2_32.lib mswsock.lib advapi32.lib bufferoverflowu.lib /NODEFAULTLIB:libcmt /MACHINE:I386 /SUBSYSTEM:console  mpong.obj
cl -ML -DWIN32_LEAN_AND_MEAN -DWIN32_EXTRA_LEAN /Ob1 /Oi /Ot /Oy /G6  -Fomsend.obj -c msend.c
cl -ML -DWIN32_LEAN_AND_MEAN -DWIN32_EXTRA_LEAN /Ob1 /Oi /Ot /Oy /G6  -Fomdump.obj -c mdump.c
cl -ML -DWIN32_LEAN_AND_MEAN -DWIN32_EXTRA_LEAN /Ob1 /Oi /Ot /Oy /G6  -Fompong.obj -c mpong.c
link /OUT:msend.exe /INCREMENTAL:NO /NOLOGO -subsystem:console,5.0 kernel32.lib  ws2_32.lib mswsock.lib advapi32.lib /DEBUG /NODEFAULTLIB:libcmt /NOLOGO /INCREMENTAL:no /MACHINE:I386 /SUBSYSTEM:console  msend.obj
link /OUT:mdump.exe /INCREMENTAL:NO /NOLOGO -subsystem:console,5.0 kernel32.lib  ws2_32.lib mswsock.lib advapi32.lib /DEBUG /NODEFAULTLIB:libcmt /NOLOGO /INCREMENTAL:no /MACHINE:I386 /SUBSYSTEM:console  mdump.obj
link /OUT:mpong.exe /INCREMENTAL:NO /NOLOGO -subsystem:console,5.0 kernel32.lib  ws2_32.lib mswsock.lib advapi32.lib /DEBUG /NODEFAULTLIB:libcmt /NOLOGO /INCREMENTAL:no /MACHINE:I386 /SUBSYSTEM:console  mpong.obj
__EOF__

# Copy the source files and the bat file to headless
cp msend.c mtools/$USER.1
cp mdump.c mtools/$USER.2
cp mpong.c mtools/$USER.3
cp build.bat mtools/$USER.bat
echo ""; echo "Use password 'user'"
scp mtools/* user@headless.29west.com:
# delete transfer files
rm mtools/*

# Build headless (run the bat file) and retrieve the executables
echo "headless" >>build_mtools.log
mkdir mtools/Win2k-i386
echo ""; echo "run build: Use password 'user'"
ssh >>build_mtools.log user@headless.29west.com "cmd <$USER.bat >$USER.log 2>&1"
echo "" >>build_mtools.log
echo ""; echo "Use password 'user'"
scp user@headless.29west.com:$USER/*.exe mtools/Win2k-i386
scp user@headless.29west.com:$USER.log .

cat $USER.log >>build_mtools.log

# Save the build script
echo "rem visual studio 6 build script" >mtools/Win2k-i386/build.bat
egrep "^cl|^link" build.bat >>mtools/Win2k-i386/build.bat
ssh >>build_mtools.log oldhat "cd $D; unix2dos mtools/Win2k-i386/build.bat 2>&1"

echo ""; echo "If you don't have shared keys set up, you'll need to enter your"
echo "Unix password several times."

# Build the rest of the platforms
echo oldhat >>build_mtools.log
BINDIR=mtools/Linux-2.4-glibc-2.3-i686
mkdir $BINDIR
cat >$BINDIR/build.sh <<__EOF__
gcc -o $BINDIR/msend msend.c
gcc -o $BINDIR/mdump mdump.c
gcc -o $BINDIR/mpong -lm mpong.c
__EOF__
ssh >>build_mtools.log oldhat "cd $D; . $BINDIR/build.sh 2>&1"

echo day >>build_mtools.log
BINDIR=mtools/FreeBSD-6-i386
mkdir $BINDIR
cat >$BINDIR/build.sh <<__EOF__
/usr/bin/gcc -o $BINDIR/msend msend.c
/usr/bin/gcc -o $BINDIR/mdump mdump.c
/usr/bin/gcc -o $BINDIR/mpong -lm mpong.c
__EOF__
ssh day >>build_mtools.log "cd $D; . $BINDIR/build.sh 2>&1"

echo wormwood >>build_mtools.log
BINDIR=mtools/Darwin-9.4.0-i386
mkdir $BINDIR
cat >$BINDIR/build.sh <<__EOF__
/usr/bin/gcc -o $BINDIR/msend msend.c
/usr/bin/gcc -o $BINDIR/mdump mdump.c
/usr/bin/gcc -o $BINDIR/mpong mpong.c
__EOF__
ssh wormwood >>build_mtools.log "cd $D; . $BINDIR/build.sh 2>&1"

echo blitz >>build_mtools.log
BINDIR=mtools/SunOS-5.10-sparc
mkdir $BINDIR
cat >$BINDIR/build.sh <<__EOF__
/openpkg/bin/gcc -o $BINDIR/msend -lsocket -lnsl msend.c
/openpkg/bin/gcc -o $BINDIR/mdump -lsocket -lnsl mdump.c
/openpkg/bin/gcc -o $BINDIR/mpong -lsocket -lnsl -lm mpong.c
__EOF__
ssh >>build_mtools.log blitz "cd $D; . $BINDIR/build.sh 2>&1"

echo ice >>build_mtools.log
BINDIR=mtools/AIX-5-powerpc64
mkdir $BINDIR
cat >$BINDIR/build.sh <<__EOF__
gcc -o $BINDIR/msend msend.c
gcc -o $BINDIR/mdump mdump.c
gcc -o $BINDIR/mpong -lm mpong.c
__EOF__
ssh >>build_mtools.log ice "cd $D; . $BINDIR/build.sh 2>&1"

echo vex >>build_mtools.log
BINDIR=mtools/SunOS-5.10-i386
mkdir $BINDIR
cat >$BINDIR/build.sh <<__EOF__
/openpkg/bin/gcc -o $BINDIR/msend -lsocket -lnsl msend.c
/openpkg/bin/gcc -o $BINDIR/mdump -lsocket -lnsl mdump.c
/openpkg/bin/gcc -o $BINDIR/mpong -lsocket -lnsl -lm mpong.c
__EOF__
ssh >>build_mtools.log vex "cd $D; . $BINDIR/build.sh 2>&1"

echo copy in testnet doc >>build_mtools.log
mkdir mtools/TestNet
for F in docbook.css testnet.html testnet.pdf `cat HTML.manifest`; do :
  cp $F mtools/TestNet/
done

cp README.txt mdump.c msend.c mpong.c mtools/

tar cf - mtools | gzip -c >mtools.tar.gz

# Assume the .zip file is used by windows, so make readme windows-friendly
ssh >>build_mtools.log oldhat "cd $D; unix2dos mtools/README.txt 2>&1"
zip -r mtools.zip mtools >>build_mtools.log

# Check for errors
echo ""; echo "Validity check: there should be no more output lines after this."
cat <<__EOF__ >build_mtools.sed
# This file is automatically generated by "build_mtools.sh"
s/
//
/^[ 	]*$/d
/^Microsoft Windows XP/d
/Copyright /d
/^C:\\\\cygwin\\\\home/d
/^A subdirectory or file [^ ]* already exists/d
/[0-9] file(s) copied/d
/Setting environment for using /d
/If you have another version/d
/to use its tools from /d
/^Microsoft (R) 32-bit C/d
/^msend.c$/d
/^mdump.c$/d
/^mpong.c$/d
/: benign redefinition of type/d
/: In function .main.:/d
/: warning: passing arg 5 of .getsockopt. from incompatible pointer/d
/: warning C4761: integral size mismatch in argument; conversion supplied/d
/^headless$/d
/^day$/d
/^oldhat$/d
/^blitz$/d
/^ice$/d
/^vex$/d
/^wormwood$/d
/^copy in testnet doc/d
/^  adding: /d
/^unix2dos: converting file /d
__EOF__
sed <build_mtools.log -f build_mtools.sed

# Make sure all output files are present
sort <<__EOF__ >build_mtools.1
mtools/AIX-5-powerpc64/build.sh
mtools/AIX-5-powerpc64/mdump
mtools/AIX-5-powerpc64/mpong
mtools/AIX-5-powerpc64/msend
mtools/Darwin-9.4.0-i386/build.sh
mtools/Darwin-9.4.0-i386/mdump
mtools/Darwin-9.4.0-i386/mpong
mtools/Darwin-9.4.0-i386/msend
mtools/FreeBSD-6-i386/build.sh
mtools/FreeBSD-6-i386/mdump
mtools/FreeBSD-6-i386/mpong
mtools/FreeBSD-6-i386/msend
mtools/Linux-2.4-glibc-2.3-i686/build.sh
mtools/Linux-2.4-glibc-2.3-i686/mdump
mtools/Linux-2.4-glibc-2.3-i686/mpong
mtools/Linux-2.4-glibc-2.3-i686/msend
mtools/README.txt
mtools/SunOS-5.10-i386/build.sh
mtools/SunOS-5.10-i386/mdump
mtools/SunOS-5.10-i386/mpong
mtools/SunOS-5.10-i386/msend
mtools/SunOS-5.10-sparc/build.sh
mtools/SunOS-5.10-sparc/mdump
mtools/SunOS-5.10-sparc/mpong
mtools/SunOS-5.10-sparc/msend
mtools/TestNet/docbook.css
mtools/TestNet/index.html
mtools/TestNet/initial-five.html
mtools/TestNet/mpong.html
mtools/TestNet/testnet.html
mtools/TestNet/testnet.pdf
mtools/TestNet/tool-notes.html
mtools/Win2k-i386/build.bat
mtools/Win2k-i386/mdump.exe
mtools/Win2k-i386/mpong.exe
mtools/Win2k-i386/msend.exe
mtools/mdump.c
mtools/mpong.c
mtools/msend.c
__EOF__
find mtools -type f -print | sort >build_mtools.2
diff build_mtools.1 build_mtools.2
