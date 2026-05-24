###############################################################################
#
# Copyright 2008-2025, Mississippi State University
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

TARGET=parametricRuleTest
FILES = $(AUTOMATIC_FILES)

include $(LOCI_BASE)/Loci.conf
include $(LOCI_BASE)/version.conf
include $(TEST_BASE)/test.conf

AUTOMATIC_FILES = $(call loci_compile_files,)
AUTOMATIC_OBJS = $(call loci_file2objs,$(FILES))

OBJS := $(AUTOMATIC_OBJS)

default: TestResults

$(TARGET): $(OBJS)
	$(LD) -o $(TARGET) $(OBJS) $(LOCAL_LIBS) $(LIBS) $(LDFLAGS)

TestResults: $(TARGET) FRC
	@(LD_LIBRARY_PATH=$(LD_LIBRARY_PATH) DYLD_LIBRARY_PATH=$(DYLD_LIBRARY_PATH) $(MPIRUN) ./$(TARGET) $(LOCI_CMD_FLAGS) > splitLoopPartition.log 2>&1 || true)
	@echo -n ParametricRules/splitLoopPartition: > TestResults
	@if tail -n 1 splitLoopPartition.log | grep -q "SUCCESS!" ; then echo " PASSED!" >> TestResults ; else echo " FAILED!" >> TestResults ; fi
	@cat TestResults

FRC:

clean:
	rm -fr $(OBJS) $(TARGET) TestResults

JUNK = $(wildcard *~) $(wildcard crash_dump.*) core debug output $(wildcard *.o)
LOCI_LPP_FILES = $(LOCI_FILES:.loci=.$(LPP_I_SUFFIX))

distclean: clean
	rm -fr $(JUNK) $(LOCI_LPP_FILES) $(DEPEND_FILES) *.log

DEPEND_FILES=$(subst .o,.d,$(OBJS))

ifeq ($(filter $(MAKECMDGOALS),clean distclean ),)
-include $(DEPEND_FILES)
endif
