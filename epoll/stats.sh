#!/bin/sh
# stats.sh - collect statistics

IFC=`ifconfig | LANG=C perl -nle 'if (/^(\S*):/){$i=$1;} if (/inet 10\.29\.4/){print $i;}'`

STOP=0

trap "STOP=1" HUP INT QUIT TERM

LANG=C sfreport.pl

for O in '' '-a' '-c' '-k' '-g' '-m' '-T'; do :
  echo "ethtool $O $IFC"
  ethtool $O $IFC 2>&1
done >stats.log

while [ $STOP -eq 0 ]; do :
  date >>stats.log
  onload_stackdump lots >stats.tmp
  if [ ! -s stats.tmp ]; then :
    STOP=1
  else :
    cat stats.tmp >>stats.log
    ethtool -S $IFC >>stats.log
    sleep 1
  fi
done
rm stats.tmp

egrep q_max_pkts stats.log;
egrep "drop[^:=]*[:=] *[1-9]" stats.log
