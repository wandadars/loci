###############################################################################
#
# Copyright 2008-2026, Mississippi State University
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
include $(LOCI_BASE)/version.conf

INCLUDES = -I$(LOCI_BASE)/include
LIB_PATH = $(LOCI_BASE)/lib:$(LD_LIBRARY_PATH)
DY_PATH = $(LOCI_BASE)/lib:$(DYLD_LIBRARY_PATH)

.PHONY: default examples clean distclean

default: TestResults

TestResults: fvmadapt_core.exe
	@(LD_LIBRARY_PATH=$(LIB_PATH) DYLD_LIBRARY_PATH=$(DY_PATH) ./fvmadapt_core.exe --check-only > fvmadapt_core.log || true)
	@tail -n 1 fvmadapt_core.log > fvmadapt_core.log1
	@echo -n FVMAdaptCore/fvmadapt_core":" > $@
	@if grep -q "SUCCESS!" fvmadapt_core.log1 ; then echo " PASSED!"; else echo " FAILED!"; fi >>$@
	@rm fvmadapt_core.log1
	@cat $@

examples: fvmadapt_core.exe
	@rm -fr output
	@mkdir -p output
	LD_LIBRARY_PATH=$(LIB_PATH) DYLD_LIBRARY_PATH=$(DY_PATH) ./fvmadapt_core.exe --write-dir output

fvmadapt_core.exe: fvmadapt_core.o $(LOCI_BASE)/lib/libfvmadaptfunc.$(LIB_SUFFIX)
	$(LD) -o $@ fvmadapt_core.o -L$(LOCI_BASE)/lib -lfvmadaptfunc $(LIBS) $(LDFLAGS)

clean:
	rm -fr *.o *.exe *.d *.log *.log1 output

distclean: clean
	rm -fr TestResults

DEPEND_FILES=$(subst .o,.d,$(wildcard *.o))

ifeq ($(filter $(MAKECMDGOALS),clean distclean ),)
-include $(DEPEND_FILES)
endif
