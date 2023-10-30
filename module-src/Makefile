# See: https://tldp.org/LDP/lkmpg/2.6/html/x181.html
obj-m += labspectrekm.o

# Shared inc directory with userspace code for common IPC struct definitions
INCLUDES := \
	-I$(PWD)/../inc

EXTRA_CFLAGS += $(INCLUDES)
EXTRA_LDFLAGS += $(IGNORE_WARNINGS)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
