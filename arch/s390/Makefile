#
# s390/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#

ifdef CONFIG_ARCH_S390_31
LDFLAGS		:= -m elf_s390
CFLAGS		+= -m31
AFLAGS		+= -m31
UTS_MACHINE	:= s390
STACK_SIZE	:= 8192
endif

ifdef CONFIG_ARCH_S390X
LDFLAGS		:= -m elf64_s390
MODFLAGS	+= -fpic -D__PIC__
CFLAGS		+= -m64
AFLAGS		+= -m64
UTS_MACHINE	:= s390x
STACK_SIZE	:= 16384
endif

cflags-$(CONFIG_MARCH_G5)   += $(call cc-option,-march=g5)
cflags-$(CONFIG_MARCH_Z900) += $(call cc-option,-march=z900)
cflags-$(CONFIG_MARCH_Z990) += $(call cc-option,-march=z990)

# old style option for packed stacks
ifeq ($(call cc-option-yn,-mkernel-backchain),y)
cflags-$(CONFIG_PACK_STACK)  += -mkernel-backchain -D__PACK_STACK
aflags-$(CONFIG_PACK_STACK)  += -D__PACK_STACK
cflags-$(CONFIG_SMALL_STACK) += -D__SMALL_STACK
aflags-$(CONFIG_SMALL_STACK) += -D__SMALL_STACK
ifdef CONFIG_SMALL_STACK
STACK_SIZE := $(shell echo $$(($(STACK_SIZE)/2)) )
endif
endif

# new style option for packed stacks
ifeq ($(call cc-option-yn,-mpacked-stack),y)
cflags-$(CONFIG_PACK_STACK)  += -mpacked-stack -D__PACK_STACK
aflags-$(CONFIG_PACK_STACK)  += -D__PACK_STACK
cflags-$(CONFIG_SMALL_STACK) += -D__SMALL_STACK
aflags-$(CONFIG_SMALL_STACK) += -D__SMALL_STACK
ifdef CONFIG_SMALL_STACK
STACK_SIZE := $(shell echo $$(($(STACK_SIZE)/2)) )
endif
endif

ifeq ($(call cc-option-yn,-mstack-size=8192 -mstack-guard=128),y)
cflags-$(CONFIG_CHECK_STACK) += -mstack-size=$(STACK_SIZE)
cflags-$(CONFIG_CHECK_STACK) += -mstack-guard=$(CONFIG_STACK_GUARD)
endif

ifeq ($(call cc-option-yn,-mwarn-dynamicstack),y)
cflags-$(CONFIG_WARN_STACK) += -mwarn-dynamicstack
cflags-$(CONFIG_WARN_STACK) += -mwarn-framesize=$(CONFIG_WARN_STACK_SIZE)
endif

CFLAGS		+= -mbackchain -msoft-float $(cflags-y)
CFLAGS		+= $(call cc-option,-finline-limit=10000)
CFLAGS 		+= -pipe -fno-strength-reduce -Wno-sign-compare 
AFLAGS		+= $(aflags-y)

OBJCOPYFLAGS	:= -O binary
LDFLAGS_vmlinux := -e start

head-$(CONFIG_ARCH_S390_31)	+= arch/$(ARCH)/kernel/head.o
head-$(CONFIG_ARCH_S390X)	+= arch/$(ARCH)/kernel/head64.o
head-y				+= arch/$(ARCH)/kernel/init_task.o

core-y		+= arch/$(ARCH)/mm/ arch/$(ARCH)/kernel/ arch/$(ARCH)/crypto/ \
		   arch/$(ARCH)/appldata/
libs-y		+= arch/$(ARCH)/lib/
drivers-y	+= drivers/s390/
drivers-$(CONFIG_MATHEMU) += arch/$(ARCH)/math-emu/

# must be linked after kernel
drivers-$(CONFIG_OPROFILE)	+= arch/s390/oprofile/

boot		:= arch/$(ARCH)/boot

all: image

install: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $@

image: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

prepare: include/asm-$(ARCH)/offsets.h

arch/$(ARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/offsets.h: arch/$(ARCH)/kernel/asm-offsets.s
	$(call filechk,gen-asm-offsets)

CLEAN_FILES += include/asm-$(ARCH)/offsets.h

# Don't use tabs in echo arguments
define archhelp
  echo  '* image           - Kernel image for IPL ($(boot)/image)'
endef
