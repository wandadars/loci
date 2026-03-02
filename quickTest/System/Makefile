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

include $(LOCI_BASE)/Loci.conf

SOURCE_DIR := $(dir $(realpath $(firstword $(MAKEFILE_LIST))))
vpath %.cc $(SOURCE_DIR)

INCLUDES = -I$(SOURCE_DIR)/../Containers/include

TESTS = test_depend_graph2
OBJS = test_depend_graph2.o

.PHONY: tests coverage coverage-clean $(TESTS)

tests: $(TESTS)

coverage:
	$(SOURCE_DIR)/run_depend_graph2_coverage.sh

coverage-clean:
	rm -fr $(SOURCE_DIR)/coverage

test_depend_graph2.o: $(SOURCE_DIR)/test_depend_graph2.cc
	$(CXX) $(COPT) $(DEFINES) $(LOCI_INCLUDES) $(INCLUDES) -o $@ -c $<

test_depend_graph2: test_depend_graph2.o
	$(LD) -o $@.exe $^ $(LOCAL_LIBS) $(LIBS) $(LDFLAGS)
	./$@.exe
	rm $@.exe

clean:
	rm -fr *.o *.exe *.d

# Junk files that are created while editing and running cases
JUNK = $(wildcard *~) $(wildcard crash_dump.*) core debug output $(wildcard *.o)

distclean: clean
	rm -fr $(TESTS) $(JUNK) $(DEPEND_FILES) $(SOURCE_DIR)/coverage

DEPEND_FILES=$(subst .o,.d,$(OBJS))

# include automatically generated dependencies
ifeq ($(filter $(MAKECMDGOALS),clean distclean ),)
-include $(DEPEND_FILES)
endif
