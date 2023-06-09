EXTRA_CFLAGS += -I $(PWD)
obj-m += btc.o
btc-objs := loader.o host/mm.o host/cpu.o vm/world.o host/world.o guest/world.o vm/mm.o vm/lru_cache.o guest/mm.o guest/cpu.o host/interrupt.o host/bt.o vm/bt.o guest/bt.o vm/see.o vm/perf.o host/perf.o guest/perf.o guest/seed.o
ifeq (${ARCH}, x86)
	btc-objs += guest/dev/fb.o guest/dev/pic.o guest/dev/fdc.o guest/dev/dma.o guest/dev/kb.o
	C_OPTION += -C /lib/modules/$(shell uname -r)/build
	EXTRA_CFLAGS += -fno-pic -Wall -O3
	SDIRS := $(PWD)
endif
ifeq (${ARCH}, arm)
#	ARM_KDIR := /km/kernel-herring/samsung-android-kernel
#	ARM_KDIR := /km/kernel-ubuntu/ubuntu-precise
	ARM_KDIR := /km/kernel-n4/msm
	C_OPTION += -C $(ARM_KDIR)
#	C_OPTION += -C /lib/modules/$(shell uname -r)/build
	EXTRA_CFLAGS += -fno-pic -save-temps -Wall
	EXTRA_CFLAGS += -I $(ARM_KDIR)/include/
	SDIRS := $(PWD)
	C_OPTION += CROSS_COMPILE=/km/cc/android-ndk-r8e/toolchains/arm-linux-androideabi-4.6/prebuilt/linux-x86/bin/arm-linux-androideabi-
endif

.PHONY: all clean
all:
	rm -f host
	rm -f guest
	ln -s -f host-$(ARCH) host
	ln -s -f guest-$(ARCH) guest
	make $(C_OPTION) SUBDIRS=$(SDIRS) modules

clean:
	rm host
	rm guest
	make $(C_OPTION) SUBDIRS=$(SDIRS) clean


