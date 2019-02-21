#!/bin/sh
# remove text lines (select lines starting with numerics)
if [ ! -f "mpong.raw" ]; then :
	echo "file 'mpong.raw' not found" >&2
	exit 1
fi

# remove text lines (select lines starting with numerics)
grep "^[0-9]" mpong.raw >mpong.dat

gnuplot <<__EOF__
## IF TYPE eq ps   set terminal postscript landscape color
## IF TYPE eq eps  set terminal postscript eps color 
set terminal jpeg  
## IF TYPE eq png  set terminal png
## IF TYPE eq pbm  set terminal pbm
set output "mpong.jpg"
set xlabel "time (sec)"
set ylabel "RTT (usec)"
set multiplot
set autoscale
set data style lines
set border 3
set xtics border nomirror
set ytics border nomirror
set origin 0.0,0.0
set title "mpong results"
set style line 10 lt 1 lw 1 pt 5 ps 0.65
plot 'mpong.dat' using 1:2 title 'mpong.dat' with linespoints linestyle 10
__EOF__

echo "Output left in mpong.jpg"
