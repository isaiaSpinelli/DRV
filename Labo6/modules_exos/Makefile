### Put here the path to kernel sources! ###
KERNELDIR := ../../Labo2/linux-socfpga/

obj-m := Aff_num.o

PWD := $(shell pwd)
WARN := -W -Wall -Wstrict-prototypes -Wmissing-prototypes

all: Aff_num

Aff_num:
	@echo "Building with kernel sources in $(KERNELDIR) for Aff_num"
	$(MAKE) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(KERNELDIR) M=$(PWD) ${WARN}
	
clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions modules.order Module.symvers
