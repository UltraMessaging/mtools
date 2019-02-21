$! Command file to build mtools
$ CFLAGS := /define=HAVE_CONFIG_H/names=(as_is,shortened)/debug/noopt
$ LDFLAGS := /threads_enable/map/debug
$!
$ cc 'CFLAGS mdump
$ link 'LDFLAGS mdump
$ cc 'CFLAGS msend
$ link 'LDFLAGS msend
