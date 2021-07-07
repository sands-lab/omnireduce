# DAIET project
# author: amedeo.sapio@kaust.edu.sa

ifeq ($(DAIET_PATH),)
DAIET_PATH = $(shell pwd)
export DAIET_PATH
endif

RTE_SDK = ${DAIET_PATH}/lib/dpdk
RTE_TARGET = build

include $(RTE_SDK)/mk/rte.vars.mk

# App name
APPNAME = daiet

# binary name
LIB = libdaiet.a

# install directory
PREFIX = /usr/local

# all source are stored in SRCS-y
SRCS-y := $(shell find ${DAIET_PATH}/src -maxdepth 1 -name "*.cpp")
HDRS := $(shell find ${DAIET_PATH}/src -maxdepth 1 -name "*.hpp" -o -name "*.h")

#SIMDFLAGS = -msse2 -mssse3 -msse4.1 -msse4.2 -mavx -fabi-version=0 -mfma -mavx2 -mavx512f -mavx512dq -mavx512cd -mavx512bw -mavx512vl 
CXXFLAGS += -Wall -Wextra -std=c++11 -fPIC -I ${DAIET_PATH}/../gloo/ -I ${DAIET_PATH}/lib/
LDFLAGS += -lstdc++ -l boost_program_options

ifeq ($(COLOCATED),ON)
$(info "COLOCATED ON")
CXXFLAGS += -DCOLOCATED
endif

ifeq ($(MLX),ON)
$(info "MLX ON")
CXXFLAGS += -DMLX
endif

ifeq ($(LATENCIES),ON)
$(info "LATENCIES ON")
CXXFLAGS += -DLATENCIES
endif

ifeq ($(TIMERS),ON)
$(info "TIMERS ON")
CXXFLAGS += -DTIMERS
endif

ifeq ($(NOSCALING),ON)
$(info "NOSCALING ON")
CXXFLAGS += -DNOSCALING
endif

ifeq ($(ALGO2),ON)
$(info "ALGO2 ON")
CXXFLAGS += -DALGO2
endif

ifeq ($(COUNTERS),ON)
$(info "COUNTERS ON")
CXXFLAGS += -DCOUNTERS
endif

ifeq ($(NO_FILL_STORE),ON)
$(info "NO_FILL_STORE ON")
CXXFLAGS += -DNO_FILL_STORE
endif

ifeq ($(DEBUG),ON)
$(info "DEBUG ON")
CXXFLAGS += -DDEBUG -g -O0
else
CXXFLAGS += -O3
endif

ifeq ($(OFFLOAD_BITMAP),ON)
$(info "OFFLOAD BITMAP ON")
CXXFLAGS += -DOFFLOAD_BITMAP
endif

.PHONY: local_install
local_install: _postbuild
	$(Q)$(MAKE) clean
	$(RM) build/_postbuild
	$(RM) _postclean
	mkdir -p build/include/$(APPNAME)
	cp $(HDRS) build/include/$(APPNAME)

include $(RTE_SDK)/mk/rte.extlib.mk

distclean: clean
	$(RM) -r build

.PHONY: libinstall
libinstall:
	mkdir -p $(DESTDIR)$(PREFIX)/lib
	mkdir -p $(DESTDIR)$(PREFIX)/include/$(APPNAME)
	cp $(RTE_OUTPUT)/$(LIB) $(DESTDIR)$(PREFIX)/lib/$(LIB)
	cp $(HDRS) $(DESTDIR)$(PREFIX)/include/$(APPNAME)

.PHONY: libuninstall
libuninstall:
	$(RM) $(DESTDIR)$(PREFIX)/lib/$(LIB)
	$(RM) -r $(DESTDIR)$(PREFIX)/include/$(APPNAME)
