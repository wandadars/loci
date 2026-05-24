###############################################################################
#
# Copyright 2026, Mississippi State University
#
# This file is part of the Loci Framework.
#
# The Loci Framework is free software: you can redistribute it and/or modify
# it under the terms of the Lesser GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# The Loci Framework is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# Lesser GNU General Public License for more details.
#
# You should have received a copy of the Lesser GNU General Public License
# along with the Loci Framework.  If not, see <http://www.gnu.org/licenses>
#
###############################################################################
LOCI_BASE ?= /usr/tmp/lush/Loci

OBJS = adaption.o transfer.o restart.o
MODULE_NAME = fvmamr

include $(LOCI_BASE)/Loci.conf

INCLUDES = -I../include -I$(LOCI_BASE)/include/FVMAMR -I$(LOCI_BASE)/include/FVMAdapt

JUNK = *~ core ti_files ii_files rii_files

LIB_OBJS=$(OBJS:.o=_lo.o)

default: $(MODULE_NAME)_m.so

all: $(MODULE_NAME)_m.so

$(MODULE_NAME)_m.so: $(LIB_OBJS)
	$(SHARED_LD) $(SHARED_LD_FLAGS) $(MODULE_NAME)_m.so $(LIB_FLAGS) $(LIB_OBJS) $(BASE_LIBS)

FRC :

clean:
	rm -fr $(OBJS) $(LIB_OBJS) $(MODULE_NAME)_m.so

LOCI_INTERMEDIATE_FILES = $(subst .loci,.$(LPP_I_SUFFIX), $(wildcard *.loci) )

uninstall:
	rm -f $(LOCI_INSTALL_PATH)/lib/$(MODULE_NAME)_m.so
	rm -fr $(LOCI_INSTALL_PATH)/include/FVMAMR

distclean:
	rm -fr $(OBJS) $(LIB_OBJS) $(MODULE_NAME)_m.so $(JUNK) $(LOCI_INTERMEDIATE_FILES) $(DEPEND_FILES)

DEPEND_FILES=$(subst .o,.d,$(OBJS))

ifeq ($(filter $(MAKECMDGOALS),clean distclean uninstall),)
-include $(DEPEND_FILES)
endif
