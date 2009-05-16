#!/usr/bin/make

SHELL := /bin/bash

KERNEL_VERSION := $(shell uname -r)

MODLIB := $(INSTALL_MOD_PATH)/lib/modules/$(KERNEL_VERSION)
export MODLIB

KDIR := $(MODLIB)/build

PWD := $(shell pwd)

DEPMOD := $(shell which depmod)

ifeq "$(strip $(INSTALL_MOD_PATH))" ""
depmod_opts :=
else
export INSTALL_MOD_PATH
depmod_opts := -b $(INSTALL_MOD_PATH) -r
endif

SOURCES := asus_eee.c

obj-m += $(SOURCES:.c=.o)

.PHONY: all patch clean install reverse_patch

all: patch $(SOURCES:.c=.ko)
$(SOURCES:.c=.ko): $(SOURCES:.c=.o)
.c.o: $@ 
	@$(MAKE) TODO=modules generic

$(SOURCES:.c=.c.patch): patch
patch:
	@declare -i n=$$(grep i2c_smbus_read_block_data asus_eee.c | wc -l);\
		if test $$n -eq 1 -a -e $(SOURCES:.c=.c.patch);\
			then patch -p0 <$(SOURCES:.c=.c.patch);\
		else echo "make: nothing to be done for '$@'."; fi

reverse_patch:
	@declare -i n=$$(grep i2c_smbus_read_block_data asus_eee.c | wc -l);\
		if test $$n -eq 2 -a -e $(SOURCES:.c=.c.patch);\
			then patch -p0 -R <$(SOURCES:.c=.c.patch);\
		else echo "make: nothing to be done for \`$@'."; fi

include $(KDIR)/.config
install: all
	install -d $(MODLIB)/acpi
	install -m644 $(SOURCES:.c=.ko) $(MODLIB)/acpi
	if [ -r System.map -a -x "$(DEPMOD)" ]; then $(DEPMOD) -ae -F System.map $(depmod_opts) $(KERNELRELEASE); fi
	if [ "$(CONFIG_I2C_I801)" == "m" ]; then\
		sed -e "s,^\($(MODLIB)/acpi/asus_eee.ko:\)$$,\\1 $$(find $(MODLIB) -name i2c-i801.ko)," $(MODLIB)/modules.dep >$(MODLIB)/modules.dep.new;\
		mv -f $(MODLIB)/modules.dep.new $(MODLIB)/modules.dep;\
	elif [ "$(CONFIG_I2C_I801)" != "y" ]; then\
		echo "ERROR: this module requires i2c-i801 support.";\
		exit 1;\
	fi

clean: reverse_patch
	rm -f Module.symvers *~ 
	@$(MAKE) TODO=$@ generic

modules:
	@$(MAKE) TODO=$@ generic

help:
	@$(MAKE) TODO=$@ generic

generic:
	cd $(KDIR) && make -C $(KDIR) M=$(PWD) $(TODO)

