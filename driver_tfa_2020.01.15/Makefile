INCLUDES = -I. -I$(KDIR)/include
KBUILD_CFLAGS += -g
WARN := -W -Wall -Wstrict-prototypes -Wmissing-prototypes

all: kernel_modules

obj-m	:= tfa.o

KDIR    := /lib/modules/$(shell uname -r)/build
PWD    := $(shell pwd)

CFLAGS_tfa.o = -I$(src)

%.ko: %.o
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) ${WARN} $@

debug: CFLAGS += -DDEBUG
debug: CFLAGS += -DVDEBUG
debug: kernel_modules

kernel_modules:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) ${WARN} EXTRA_CFLAGS="${CFLAGS}" modules

modules_install: all
	install -m 644 dsys.ko /lib/modules/`uname -r`/kernel/drivers/tfa.ko
	/sbin/depmod -a

install:	modules_install

clean:
	$(RM) *.o *~ *.ko *.mod.c .*.o.cmd .*.ko.cmd modules.order Module.symvers
	$(RM) -r .tmp_versions
