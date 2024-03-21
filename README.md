My experience with the BL808 SoC driven Pine64 Ox64
---------------------------------------------------


Pin layout (of what's important) for the BL808/Pine64 Ox64:

                                mUSB (power)

                        +-------------------+
UART0_TX        GPIO14  1                  40
                        +-------------------+
UART0_RX        GPIO15  2                  39
                        +-------------------+
GND                     3                  38                   GND
                        +-------------------+
GPIO12                  4                  37                   3V3_EN
                        +-------------------+
GPIO13                  5                  36                   3V3_OUT
                        +-------------------+
GPIO41                  6                  35
                        +-------------------+
GPIO40                  7                  34   GPIO18
                        +-------------------+
GND                     8                  33                   GND/AGND
                        +-------------------+
GPIO33                  9                  32   GPIO16          UART1_TX
                        +-------------------+
GPIO32                  10                 31   GPIO17          UART1_RX
                        +-------------------+
GPIO21                  11                 30
                        +-------------------+
GPIO20                  12                 29   GPIO11
                        +-------------------+
GND                     13                 28                   GND
                        +-------------------+
GPIO23                  14                 27   GPIO6
                        +-------------------+
GPIO22                  15                 26   GPIO7
                        +-------------------+
GPIO25                  16                 25   GPIO30
                        +-------------------+
GPIO24                  17                 24   GPIO31
                        +-------------------+
GND                     18                 23                   GND
                        +-------------------+
GPIO27                  19                 22   GPIO28
                        +-------------------+
GPIO26                  20                 21   GPIO29
                        +-------------------+

                                USB-C


Flashing:

I flashed the Ox64 with u-boot built with Linux, instructions from:

https://wiki.pine64.org/wiki/Ox64#Build

 Once the image was made I connected pins 1 (UART0_TX),2 (UART0_RX) and GND with 
a CP2102 USB to UART dongle and applied power to the microUSB port while 
holding down the "BOOT" button (only button closest to microUSB port).
I used the bflb-iot-tool at BAUD 115200, which worked well.

Booting for first time:

You then have to connect the UART pins to the Ox64's pins 32 (TX-GPIO16)
and pins 31 (RX-GPIO17).  No need to press any buttons here, apply power
and it should go.

I didn't insert a microSD card the first time as the u-boot should be on
flash.  The CP2102 I have (Waveshare) did not work at BAUD 2,000,000 and I
got a lot of garble.  Settig the settings to different did not work either.

Since I have three ESP32-WROOM's here that I bought for another approach of
this project, I noticed the instructions said that these would work.  After
playing around for about a day I finally read that on the ESP32 the EN and
GND need to be shorted (connected) in order to get a passthrough UART mode.
Once this was done I was able to connect at BAUD 2000000 (2,000,000) and
see beautiful U-Boot ascii.  The pins on the ESP32 that I used are:

  1. GND (when looking down on the board from top and pins to the bottom where
pin 1 left would be closest to where the USB-C cable goes for power) it would 
be on pin 2 left.  The GND is shorted with "EN" which is labelled the topmost
pin on the left side.  These pins are labelled but you need a magnifying glass
or really squint your eyes to read them.  Be careful of deviations from other
ESP32's!

2. GND for serial is pin 2 right.  UART TX (third from top right) which would
be pin 13 right, and UART RX (fourth from top right) which would be pin 12 on
the right.

Here is a quick ascii drawing of my ESP32-WROOM (as looked down on it):
                                         
                                WIFI
                        +-------------------+
EN (pin 14 left)        14                 15
                        +-------------------+
                        13                 16
                        +-------------------+
                        12                 17           UART TX (pin13 right)
                        +-------------------+
                        11                 18           UART RX (pin12 right)
                        +-------------------+
                        10                 19
                        +-------------------+
                        9                  20
                        +-------------------+
                        8                  21
                        +-------------------+
                        7                  22
                        +-------------------+
                        6                  23
                        +-------------------+
                        5                  24
                        +-------------------+
                        4                  25
                        +-------------------+
                        3                  26
                        +-------------------+
GND (pin 2 left)        2                  27           GND (pin 2 right)
                        +-------------------+
                        1                  28
                        +-------------------+
                                +--+
                             USB-C (power)

After this process I'm able to boot into OpenBSD/riscv64 but no drivers are
detected other than the simplebus/mainbus/cpu (yikes).  BTW I bought three
ESP32's from Alibaba back last year, today I'd probably not do that again,
but I'm glad these ESP32's aren't just laying around doing nothing.  The
price was about 20 dollars USD.
On to OpenBSD from an old C906/Allwinner-D1 install (thanks Mark!):

Here is a dmesg (of an old 7.4-current OpenBSD).

----->
Copyright (c) 1982, 1986, 1989, 1991, 1993
        The Regents of the University of California.  All rights reserved.
Copyright (c) 1995-2024 OpenBSD. All rights reserved.  https://www.OpenBSD.org

OpenBSD 7.4-current (RAMDISK) #426: Tue Jan 30 07:46:45 MST 2024
    deraadt@riscv64.openbsd.org:/usr/src/sys/arch/riscv64/compile/RAMDISK
real mem  = 67108864 (64MB)
avail mem = 34902016 (33MB)
SBI: OpenSBI v1.2, SBI Specification Version 1.0
random: boothowto does not indicate good seed
mainbus0 at root: Pine64 Ox64 (D0)
cpu0 at mainbus0: T-Head arch 0 imp 0 rv64imafdc
intc0 at cpu0
cpu0: 32KB 64b/line 128-way L1 I-cache, 32KB 64b/line 256-way L1 D-cache
plic0 at mainbus0
"clk-ext-xtal" at mainbus0 not configured
simplebus0 at mainbus0: "bus"
"syscon" at simplebus0 not configured
"gpip" at simplebus0 not configured
"clock-controller" at simplebus0 not configured
"serial" at simplebus0 not configured
"spi" at simplebus0 not configured
"i2c" at simplebus0 not configured
"pwm" at simplebus0 not configured
"timer" at simplebus0 not configured
"ir" at simplebus0 not configured
"i2c" at simplebus0 not configured
"serial" at simplebus0 not configured
"i2s" at simplebus0 not configured
"dma" at simplebus0 not configured
"syscon" at simplebus0 not configured
"syscon" at simplebus0 not configured
"audio" at simplebus0 not configured
"efuse" at simplebus0 not configured
"mmc" at simplebus0 not configured
"emac" at simplebus0 not configured
"dma" at simplebus0 not configured
"usb" at simplebus0 not configured
 simplebus1 at mainbus0: "bus"
"syscon" at simplebus1 not configured
"dma" at simplebus1 not configured
"serial" at simplebus1 not configured
"i2c" at simplebus1 not configured
"i2c" at simplebus1 not configured
"clock-controller" at simplebus1 not configured
"spi" at simplebus1 not configured
"timer" at simplebus1 not configured
"memory-controller" at simplebus1 not configured
"timer" at mainbus0 not configured
softraid0 at root
scsibus0 at softraid0: 256 targets
root on rd0a swap on rd0b dump on rd0b
WARNING: CHECK AND RESET THE DATE!
<-------

Notice no Userland (due to no serial UART driver).

I had to use this command sequence to boot into OpenBSD loader (bootriscv64.efi):

fdt addr $fdtcontroladdr ; fdt move $fdtcontroladdr 50ffc000 ;  \
env set loadaddr 50f00000 ; load mmc 0 $loadaddr efi/boot/bootriscv64.efi ; \
bootefi $loadaddr 50ffc000

The addresses are really somewhat wack.  I believe the loadaddr could be
0x50000000 (50 million which is the start of the "PSRAM" on the BL808).

Eventually I'll make a boot.scr script (done with u-boot mkimage) to automate
this process.


TODO:

UART driver
DMA driver
USB driver
Watchdog/timer driver

everything else has lesser importance in my books.  A nice to have would
be to spin up CPU 1 which is the 32-bit core and run a copy of NUTTX inside
a memory mapped space or something.  IPC would be done through DMA or a
door FIFO or something.  That would get access to the wifi chip.  NUTTX would
be loaded like a firmware (hahaha).

The document I will be using mostly is 
https://mainrechner.de/BL808_RM_en_1.3.pdf which I downloaded from the
Ox64 site.

Best Regards,
-pjp
