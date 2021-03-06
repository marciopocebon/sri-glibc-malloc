# Copyright (C) 1991-2015 Free Software Foundation, Inc.
# This file is part of the GNU C Library.

# The GNU C Library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.

# The GNU C Library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.

# You should have received a copy of the GNU Lesser General Public
# License along with the GNU C Library; if not, see
# <http://www.gnu.org/licenses/>.

#
#	Makefile for malloc routines
#
subdir	:= malloc

include ../Makeconfig

dist-headers := malloc.h
headers := $(dist-headers) gassert.h obstack.h mcheck.h \
	utils.h chunkinfo.h memcxt.h metadata.h \
	locking.h debug.h lookup.h lfht.h sri_atomic.h	
tests := mallocbug tst-malloc tst-valloc tst-calloc tst-obstack \
	 tst-mallocstate tst-mcheck tst-mallocfork tst-trim1 \
	 tst-malloc-usable tst-realloc tst-posix_memalign \
	 tst-pvalloc tst-memalign tst-mallopt tst-scratch_buffer \
	 tst-malloc-backtrace 

test-srcs = tst-mtrace

routines = malloc morecore mcheck mtrace obstack \
  memcxt metadata utils debug lookup lfht \
  scratch_buffer_grow scratch_buffer_grow_preserve \
  scratch_buffer_set_array_size

install-lib := libmcheck.a
non-lib.a := libmcheck.a

# Additional library.
extra-libs = libmemusage
extra-libs-others = $(extra-libs)

libmemusage-routines = memusage
libmemusage-inhibit-o = $(filter-out .os,$(object-suffixes))

$(objpfx)tst-malloc-backtrace: $(common-objpfx)nptl/libpthread.so \
			       $(common-objpfx)nptl/libpthread_nonshared.a

# These should be removed by `make clean'.
extra-objs = mcheck-init.o libmcheck.a

# Include the cleanup handler.
aux := set-freeres thread-freeres

# The Perl script to analyze the output of the mtrace functions.
ifneq ($(PERL),no)
install-bin-script = mtrace
generated += mtrace

# The Perl script will print addresses and to do this nicely we must know
# whether we are on a 32 or 64 bit machine.
ifneq ($(findstring wordsize-32,$(config-sysdirs)),)
address-width=10
else
address-width=18
endif
endif

# Unless we get a test for the availability of libgd which also works
# for cross-compiling we disable the memusagestat generation in this
# situation.
ifneq ($(cross-compiling),yes)
# If the gd library is available we build the `memusagestat' program.
ifneq ($(LIBGD),no)
others: $(objpfx)memusage
install-bin = memusagestat
install-bin-script += memusage
generated += memusagestat memusage
extra-objs += memusagestat.o

# The configure.ac check for libgd and its headers did not use $SYSINCLUDES.
# The directory specified by --with-headers usually contains only the basic
# kernel interface headers, not something like libgd.  So the simplest thing
# is to presume that the standard system headers will be ok for this file.
$(objpfx)memusagestat.o: sysincludes = # nothing
endif
endif

# Another goal which can be used to override the configure decision.
.PHONY: do-memusagestat
do-memusagestat: $(objpfx)memusagestat

memusagestat-modules = memusagestat

cpp-srcs-left := $(memusagestat-modules)
lib := memusagestat
include $(patsubst %,$(..)cppflags-iterator.mk,$(cpp-srcs-left))

$(objpfx)memusagestat: $(memusagestat-modules:%=$(objpfx)%.o)
	$(LINK.o) -o $@ $^ $(libgd-LDFLAGS) -lgd -lpng -lz -lm

ifeq ($(run-built-tests),yes)
ifeq (yes,$(build-shared))
ifneq ($(PERL),no)
tests-special += $(objpfx)tst-mtrace.out
endif
endif
endif

include ../Rules

CFLAGS-mcheck-init.c = $(PIC-ccflag)
CFLAGS-obstack.c = $(uses-callbacks)

# SRI hack to compile our version of malloc without optimizations.
# you will need to make sure that you have commented out line 4 
# of config.h in the glibc-build directory.
# can also use SRI_POOL_DEBUG to debug pool
# and SRI_MALLOC_LOG to log events
# uses canaries to keep track of a chunks history.
# -DSRI__HEADERS_DEBUG=1
# logs calls to a file
# -DSRI_MALLOC_LOG=1 
# turns on all the assertion
# -DMALLOC_DEBUG=1
# uses the google hash rather than jenkins
# -DSRI_NOT_JENKINS
# includes a histogram of the bins in the stats per arena hashtable
# -DSRI_HISTOGRAM=1
# Here are some sample configurations.

# Enable all debug checking. Very slow. Even a untouched glibc
# can report issues. SO be warned.
#CFLAGS-malloc.c =  -O0 -DSRI_DEBUG_HEADERS=1   -DMALLOC_DEBUG=1
#CFLAGS-debug.c =  -O0 -DSRI_DEBUG_HEADERS=1    -DMALLOC_DEBUG=1 
#CFLAGS-lookup.c =  -O0 -DSRI_DEBUG_HEADERS=1   -DMALLOC_DEBUG=1
#CFLAGS-utils.c =  -O0 -DSRI_DEBUG_HEADERS=1    -DMALLOC_DEBUG=1 
#CFLAGS-memcxt.c =  -O0 -DSRI_DEBUG_HEADERS=1   -DMALLOC_DEBUG=1
#CFLAGS-metadata.c =  -O0 -DSRI_DEBUG_HEADERS=1 -DMALLOC_DEBUG=1 -DSRI_HISTOGRAM=1

# Good for using gdb on a core. The SRI_DEBUG flag adds debugging
# metadata to the fray, and the canary indicates the state of the chunk.
# it should also match the canary in the metadata.
#
#CFLAGS-malloc.c =  -O0 -DSRI_DEBUG_HEADERS=1
#CFLAGS-debug.c =  -O0 -DSRI_DEBUG_HEADERS=1
#CFLAGS-lookup.c =  -O0 -DSRI_DEBUG_HEADERS=1
#CFLAGS-utils.c =  -O0 -DSRI_DEBUG_HEADERS=1
#CFLAGS-memcxt.c =  -O0 -DSRI_DEBUG_HEADERS=1
#CFLAGS-metadata.c =  -O0 -DSR_HEADERSI_DEBUG=1 -DSRI_HISTOGRAM=1

# no metadata; but good to debug with gdb.
#CFLAGS-malloc.c =  -O0 -g
#CFLAGS-debug.c =  -O0 -g
#CFLAGS-lookup.c =  -O0 -g
#CFLAGS-utils.c =  -O0 -g
#CFLAGS-memcxt.c =  -O0 -g
#CFLAGS-metadata.c =  -O0 -g

# metadata and good to debug with gdb.
#CFLAGS-malloc.c =  -Og -DSRI_DEBUG_HEADERS=1
#CFLAGS-debug.c =  -Og -DSRI_DEBUG_HEADERS=1
#CFLAGS-lookup.c =  -Og -DSRI_DEBUG_HEADERS=1
#CFLAGS-utils.c =  -Og -DSRI_DEBUG_HEADERS=1
#CFLAGS-memcxt.c =  -Og -DSRI_DEBUG_HEADERS=1
#CFLAGS-metadata.c =  -Og -DSRI_DEBUG_HEADERS=1

# production build with metadata
#CFLAGS-malloc.c =  -DNDEBUG -O2 -DSRI_DEBUG_HEADERS=1
#CFLAGS-debug.c =  -DNDEBUG -O2 -DSRI_DEBUG_HEADERS=1
#CFLAGS-lookup.c =  -DNDEBUG -O2 -DSRI_DEBUG_HEADERS=1
#CFLAGS-utils.c =  -DNDEBUG  -O2 -DSRI_DEBUG_HEADERS=1
#CFLAGS-memcxt.c = -DNDEBUG  -O2 -DSRI_DEBUG_HEADERS=1
#CFLAGS-metadata.c = -DNDEBUG  -O2 -DSRI_DEBUG_HEADERS=1 -DSRI_HISTOGRAM=1

# production build with NO metadata
CFLAGS-malloc.c =  -DNDEBUG -O2
CFLAGS-debug.c =  -DNDEBUG -O2
CFLAGS-lookup.c =  -DNDEBUG -O2
CFLAGS-utils.c =  -DNDEBUG  -O2
CFLAGS-memcxt.c = -DNDEBUG  -O2
CFLAGS-metadata.c = -DNDEBUG  -O2


$(objpfx)libmcheck.a: $(objpfx)mcheck-init.o
	-rm -f $@
	$(patsubst %/,cd % &&,$(objpfx)) \
	$(LN_S) $(<F) $(@F)

lib: $(objpfx)libmcheck.a

ifeq ($(run-built-tests),yes)
ifeq (yes,$(build-shared))
ifneq ($(PERL),no)
$(objpfx)tst-mtrace.out: tst-mtrace.sh $(objpfx)tst-mtrace
	$(SHELL) $< $(common-objpfx) '$(test-program-prefix-before-env)' \
		 '$(run-program-env)' '$(test-program-prefix-after-env)' ; \
	$(evaluate-test)
endif
endif
endif

tst-mcheck-ENV = MALLOC_CHECK_=3
tst-malloc-usable-ENV = MALLOC_CHECK_=3

# Uncomment this for test releases.  For public releases it is too expensive.
#CPPFLAGS-malloc.o += -DMALLOC_DEBUG=1

sLIBdir := $(shell echo $(slibdir) | sed 's,lib\(\|64\)$$,\\\\$$LIB,')

$(objpfx)mtrace: mtrace.pl
	rm -f $@.new
	sed -e 's|@PERL@|$(PERL)|' -e 's|@XXX@|$(address-width)|' \
	    -e 's|@VERSION@|$(version)|' \
	    -e 's|@PKGVERSION@|$(PKGVERSION)|' \
	    -e 's|@REPORT_BUGS_TO@|$(REPORT_BUGS_TO)|' $^ > $@.new \
	&& rm -f $@ && mv $@.new $@ && chmod +x $@

$(objpfx)memusage: memusage.sh
	rm -f $@.new
	sed -e 's|@BASH@|$(BASH)|' -e 's|@VERSION@|$(version)|' \
	    -e 's|@SLIBDIR@|$(sLIBdir)|' -e 's|@BINDIR@|$(bindir)|' \
	    -e 's|@PKGVERSION@|$(PKGVERSION)|' \
	    -e 's|@REPORT_BUGS_TO@|$(REPORT_BUGS_TO)|' $^ > $@.new \
	&& rm -f $@ && mv $@.new $@ && chmod +x $@


# The implementation uses `dlsym'
$(objpfx)libmemusage.so: $(libdl)

# Extra dependencies
$(foreach o,$(all-object-suffixes),$(objpfx)malloc$(o)): arena.c hooks.c


