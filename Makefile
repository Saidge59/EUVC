target = euvc
euvc-objs = module.o control.o device.o videobuf.o
obj-m = $(target).o

CFLAGS_utils = -O2 -Wall -Wextra -pedantic -std=c99

.PHONY: all
all: kmod euvc-cli

euvc-cli: euvc-cli.c euvc.h
	$(CC) $(CFLAGS_utils) -o $@ $<

kmod:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

.PHONY: clean
clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	$(RM) euvc-cli
