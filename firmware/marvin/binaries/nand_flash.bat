sam-ba -p serial -b sam9x75-curiosity -a nandflash -c erase::0x100000
sam-ba -p serial -b sam9x75-curiosity -a nandflash -c writeboot:sam9x7-nandflashboot-uboot-4.0.13.bin -c verifyboot:sam9x7-nandflashboot-uboot-4.0.13.bin
sam-ba -p serial -b sam9x75-curiosity -a nandflash -c write:harmony.bin:0x40000 -c verify:harmony.bin:0x40000
