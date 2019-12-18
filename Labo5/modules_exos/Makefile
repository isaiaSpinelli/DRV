### Put here the path to kernel sources! ###
KERNELDIR := ../../Labo2/linux-socfpga/

obj-m := pushbutton_example.o compte_rebours.o kfifo_ex2.o kfifo_ex3.o Synch_Ex4.o Synch_Ex5.o Synch_Ex5_clean.o

PWD := $(shell pwd)
WARN := -W -Wall -Wstrict-prototypes -Wmissing-prototypes

all: pushbutton_example compte_rebours kfifo_ex2 kfifo_ex3 Synch_Ex4 Synch_Ex5 Synch_Ex5_clean

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
	@echo "Building with kernel sources in $(KERNELDIR) for kfifo_ex3"
	$(MAKE) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(KERNELDIR) M=$(PWD) ${WARN}
	
Synch_Ex4:
	@echo "Building with kernel sources in $(KERNELDIR) for Synch_Ex4"
	$(MAKE) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(KERNELDIR) M=$(PWD) ${WARN}
	
Synch_Ex5:
	@echo "Building with kernel sources in $(KERNELDIR) for Synch_Ex5"
	$(MAKE) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(KERNELDIR) M=$(PWD) ${WARN}
	
Synch_Ex5_clean:
	@echo "Building with kernel sources in $(KERNELDIR) for Synch_Ex5_clean"
	$(MAKE) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C $(KERNELDIR) M=$(PWD) ${WARN}
	
clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions modules.order Module.symvers
