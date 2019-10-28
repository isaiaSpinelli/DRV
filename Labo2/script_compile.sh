#!/bin/sh —

make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -j12
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
rm ./tmp -rf
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- INSTALL_MOD_PATH="./tmp" modules_install
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- socfpga_cyclone5_sockit.dtb

cp /var/lib/tftpboot/socfpga.dtb /var/lib/tftpboot/socfpga.dtb.old
cp /var/lib/tftpboot/zImage /var/lib/tftpboot/zImage.old
cp arch/arm/boot/dts/socfpga_cyclone5_sockit.dtb /var/lib/tftpboot/socfpga.dtb
cp arch/arm/boot/zImage /var/lib/tftpboot/
sudo cp ./tmp/lib/modules/4.14.130-ltsi-13527-g567dd6b-dirty/ /export/drv/ -R

modprobe -r uio_pdrv_genirq; modprobe uio_pdrv_genirq of_id="drv-btn"
