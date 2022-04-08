$! bld_vms.com - Command file to build mtools on VMS.
$ CFLAGS := /define=HAVE_CONFIG_H/names=(as_is,shortened)/debug/noopt
$ LDFLAGS := /threads_enable/map/debug
$!
$ cc 'CFLAGS mdump
$ link 'LDFLAGS mdump
$ cc 'CFLAGS msend
$ link 'LDFLAGS msend
