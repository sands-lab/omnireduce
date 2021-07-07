# DAIET project
# author: amedeo.sapio@kaust.edu.sa

ifeq ($(PS_PATH),)
PS_PATH = $(shell pwd)
export PS_PATH
endif

ifeq ($(DAIET_PATH),)
DAIET_PATH = ${PS_PATH}/..
export DAIET_PATH
endif

RTE_SDK = ${DAIET_PATH}/lib/dpdk
RTE_TARGET = build

include $(RTE_SDK)/mk/rte.vars.mk

# binary name
APP = ps

# all source are stored in SRCS-y
SRCS-y := $(shell find ${PS_PATH}/src -maxdepth 1 -name "*.cpp")
HDRS := $(shell find ${PS_PATH}/src -maxdepth 1 -name "*.hpp" -o -name "*.h")

#SIMDFLAGS = -msse2 -mssse3 -msse4.1 -msse4.2 -mavx -fabi-version=0 -mfma -mavx2 -mavx512f -mavx512dq -mavx512cd -mavx512bw -mavx512vl 
CXXFLAGS += -Wall -Wextra -std=c++11 -fPIC -I ${DAIET_PATH}/lib/
LDFLAGS += -lstdc++ -l boost_program_options -l boost_system

ifeq ($(NOSCALING),ON)
$(info "NOSCALING ON")
CXXFLAGS += -DNOSCALING
endif

ifeq ($(ALGO2),ON)
$(info "ALGO2 ON")
CXXFLAGS += -DALGO2
endif

ifeq ($(TIMERS),ON)
$(info "TIMERS ON")
CXXFLAGS += -DTIMERS
endif

ifeq ($(DEBUG),ON)
$(info "DEBUG ON")
CXXFLAGS += -DDEBUG -g -O0
else
CXXFLAGS += -O3
endif

.PHONY: final_clean
final_clean: _postbuild
	$(Q)$(MAKE) clean
	$(RM) build/ps.map lib build/_postbuild

include $(RTE_SDK)/mk/rte.extapp.mk
