LOCI_ROOT_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
OBJDIR ?= $(LOCI_ROOT_DIR)/OBJ
DOXYGEN_DIR := $(LOCI_ROOT_DIR)/docs/doxygen
DOXYFILE := $(DOXYGEN_DIR)/Doxyfile
CPP_OUTPUT_DIR := $(OBJDIR)/docs/api-cpp

.PHONY: doxygen html clean

doxygen html:
	@command -v doxygen >/dev/null 2>&1 || { \
		echo "doxygen was not found in PATH"; \
		exit 1; \
	}
	@if [ ! -f "$(OBJDIR)/include/Config/version_info.h" ]; then \
		echo "Missing $(OBJDIR)/include/Config/version_info.h"; \
		echo "Run ./configure first, then invoke 'make api-docs' from the repo root."; \
		exit 1; \
	fi
	@rm -rf "$(CPP_OUTPUT_DIR)"
	@mkdir -p "$(CPP_OUTPUT_DIR)"
	cd "$(DOXYGEN_DIR)" && doxygen "$(DOXYFILE)"
	@echo "Generated C++ API docs in $(CPP_OUTPUT_DIR)/html/index.html"

clean:
	rm -rf "$(CPP_OUTPUT_DIR)"
