#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The Initial Developer of the Original Code and related documentation
# is America Online, Inc. Portions created by AOL are Copyright (C) 1999
# America Online, Inc. All Rights Reserved.
#

ifndef NAVISERVER
    NAVISERVER  = /usr/local/ns
endif

#
# Ability to specify include and library separately:
#
ifdef PGINCLUDE
    CFLAGS     = -I$(PGINCLUDE)
endif
ifdef PGLIB
    MODLIBS    = -L$(PGLIB) -Wl,-rpath,$(PGLIB)
endif

#
# Alternate configuration based on a common root for includes and
# libraries (legacy and backward compatibility):
#
ifdef POSTGRES
    CFLAGS     = -I$(POSTGRES)/include
    MODLIBS    = -L$(POSTGRES)/lib -Wl,-rpath,$(POSTGRES)/lib
endif

MODNAME    = nsdbpg
MOD        = nsdbpg.so
MODOBJS    = nsdbpg.o tclcmds.o
HDRS       = dbpg.h
MODLIBS    += -lnsdb -lpq

include  $(NAVISERVER)/include/Makefile.module

