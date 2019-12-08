### Put here the path to kernel sources! ###
KERNELDIR := ../../Labo2/linux-socfpga/

obj-m := pushbutton_example.o compte_rebours.o kfifo_ex2.o kfifo_ex3.o

PWD := $(shell pwd)
WARN := -W -Wall -Wstrict-prototypes -Wmissing-prototypes

all: pushbutton_example compte_rebours kfifo_ex2

pushbutton_example:
	@echo "Building with kernel sources in $(KERNELDIR)"
	$(MAKE) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(KERNELDIR) M=$(PWD) ${WARN}
	
compte_rebours:
	@echo "Building with kernel sources in $(KERNELDIR) for compte_rebours"
	$(MAKE) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(KERNELDIR) M=$(PWD) ${WARN}
	
kfifo_ex2:
	@echo "Building with kernel sources in $(KERNELDIR) for kfifo_ex2"
	$(MAKE) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(KERNELDIR) M=$(PWD) ${WARN}
	
kfifo_ex3:
	@echo "Building with kernel sources in $(KERNELDIR) for kfifo_ex2"
	$(MAKE) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(KERNELDIR) M=$(PWD) ${WARN}

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions modules.order Module.symvers
