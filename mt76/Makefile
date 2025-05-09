# SPDX-License-Identifier: GPL-2.0-only

# Build modules
obj-m += mt76.o
obj-m += mt76-usb.o
obj-m += mt76-sdio.o
obj-m += mt76x02-lib.o
obj-m += mt76x02-usb.o
obj-m += mt76-connac-lib.o

mt76-y := \
	mmio.o util.o trace.o dma.o mac80211.o debugfs.o eeprom.o \
	tx.o agg-rx.o mcu.o pci.o
	#testmode.o

mt76-usb-y := usb.o usb_trace.o
mt76-sdio-y := sdio.o sdio_txrx.o

CFLAGS_trace.o := -I$(src)
CFLAGS_usb_trace.o := -I$(src)
CFLAGS_mt76x02_trace.o := -I$(src)

mt76x02-lib-y := mt76x02_util.o mt76x02_mac.o mt76x02_mcu.o \
		 mt76x02_eeprom.o mt76x02_phy.o mt76x02_mmio.o \
		 mt76x02_txrx.o mt76x02_trace.o mt76x02_debugfs.o \
		 mt76x02_dfs.o mt76x02_beacon.o

mt76x02-usb-y := mt76x02_usb_mcu.o mt76x02_usb_core.o
mt76-connac-lib-y := mt76_connac_mcu.o mt76_connac_mac.o

obj-m += mt76x0/
obj-m += mt76x2/
obj-m += mt7603/
obj-m += mt7615/
obj-m += mt7915/
obj-m += mt7921/

all:
	$(MAKE) EXTRA_CFLAGS="-w" ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERNEL_SRC) M=$(PWD) modules

clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers
