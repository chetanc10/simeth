
obj-m = simeth.o
KVERSION = $(shell uname -r)

EXTRA_CFLAGS := -Wall
EXTRA_CFLAGS += -I$(src)/../include/

all:
	echo "$(shell pwd)/../include/"
	$(MAKE) -C /lib/modules/$(KVERSION)/build M=$(shell pwd) modules

clean:
	$(MAKE) -C /lib/modules/$(KVERSION)/build M=$(shell pwd) clean

