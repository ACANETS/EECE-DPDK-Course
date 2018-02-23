ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif

# Default target, can be overridden by command line or environment
RTE_TARGET ?= x86_64-native-linuxapp-gcc
include $(RTE_SDK)/mk/rte.vars.mk

# binary name
APP = test_echo

# all source are stored in SRCS-y
SRCS-y := main.c
CFLAGS += -O3

include $(RTE_SDK)/mk/rte.extapp.mk

clean:
	sudo rm -rf ./build *~
