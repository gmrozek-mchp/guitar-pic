sam-ba -p serial -b sam9x75-curiosity -a qspiflash -c erase
sam-ba -p serial -b sam9x75-curiosity -a qspiflash -c writeboot:sam9x7-dataflashboot-uboot-4.0.13.bin -c verifyboot:sam9x7-dataflashboot-uboot-4.0.13.bin
sam-ba -p serial -b sam9x75-curiosity -a qspiflash -c write:harmony.bin:0x40000 -c verify:harmony.bin:0x40000
