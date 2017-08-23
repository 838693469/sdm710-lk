UEFI_TOP_DIR := .

ifndef $(BOOTLOADER_OUT)
	BOOTLOADER_OUT := .
endif
export $(BOOTLOADER_OUT)

BUILDDIR=$(shell pwd)

ifeq ($(ANDROID_BUILD_TOP),)
export CLANG35_AARCH64_PREFIX := $(CLANG_PREFIX)
else
export CLANG35_AARCH64_PREFIX := $(ANDROID_BUILD_TOP)/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-$(TARGET_GCC_VERSION)/bin/aarch64-linux-android-
export CLANG35_AARCH64_GCC_TOOLCHAIN := $(ANDROID_BUILD_TOP)/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-$(TARGET_GCC_VERSION)
endif
export CLANG35_BIN := $(CLANG_BIN)
export CLANG35_ARM_PREFIX := $(CLANG_BIN)/tools/bin
export CLANG_ARM_LINK_PATH := $(CLANG_LINK_PATH)

export BUILD_REPORT_DIR := $(BOOTLOADER_OUT)/build_report
ANDROID_PRODUCT_OUT := $(BOOTLOADER_OUT)/Build

ifeq ($(TARGET_ARCHITECTURE),arm)
export ARCHITECTURE := ARM
else
export ARCHITECTURE := AARCH64
export CLANG35_GCC_TOOLCHAIN := $(CLANG35_AARCH64_GCC_TOOLCHAIN)
endif

WORKSPACE=$(BUILDDIR)
TARGET_TOOLS := CLANG35
TARGET := DEBUG
BUILD_ROOT := $(ANDROID_PRODUCT_OUT)/$(TARGET)_$(TARGET_TOOLS)
LOAD_ADDRESS := 0X9FA00000
EDK_TOOLS := $(BUILDDIR)/BaseTools
EDK_TOOLS_BIN := $(EDK_TOOLS)/Source/C/bin
ABL_FV_IMG := $(BUILD_ROOT)/FV/abl.fv
ABL_FV_ELF := $(BOOTLOADER_OUT)/../../abl.elf
SHELL:=/bin/bash

# UEFI UBSAN Configuration
# ENABLE_UEFI_UBSAN := true

ifeq "$(ENABLE_UEFI_UBSAN)" "true"
	UBSAN_GCC_FLAG_UNDEFINED := -fsanitize=undefined
	UBSAN_GCC_FLAG_ALIGNMENT := -fno-sanitize=alignment
else
	UBSAN_GCC_FLAG_UNDEFINED :=
	UBSAN_GCC_FLAG_ALIGNMENT :=
endif

.PHONY: all cleanall

all: ABL_FV_ELF

cleanall:
	@. ./edksetup.sh BaseTools && \
	build -p $(WORKSPACE)/QcomModulePkg/QcomModulePkg.dsc -a $(ARCHITECTURE) -t $(TARGET_TOOLS) -b $(TARGET) -j build_modulepkg.log cleanall
	rm -rf $(WORKSPACE)/QcomModulePkg/Bin64

EDK_TOOLS_BIN:
	@. ./edksetup.sh BaseTools && \
	$(MAKE) -C $(EDK_TOOLS) -j1

ABL_FV_IMG: EDK_TOOLS_BIN
	@. ./edksetup.sh BaseTools && \
	build -p $(WORKSPACE)/QcomModulePkg/QcomModulePkg.dsc -a $(ARCHITECTURE) -t $(TARGET_TOOLS) -b $(TARGET) -D ABL_OUT_DIR=$(ANDROID_PRODUCT_OUT) -D VERIFIED_BOOT=$(VERIFIED_BOOT) -D VERIFIED_BOOT_2=$(VERIFIED_BOOT_2) -D USER_BUILD_VARIANT=$(USER_BUILD_VARIANT) -D UBSAN_UEFI_GCC_FLAG_UNDEFINED=$(UBSAN_GCC_FLAG_UNDEFINED) -D UBSAN_UEFI_GCC_FLAG_ALIGNMENT=$(UBSAN_GCC_FLAG_ALIGNMENT) -j build_modulepkg.log $*
	cp $(BUILD_ROOT)/FV/FVMAIN_COMPACT.Fv $(ABL_FV_IMG)

ABL_FV_ELF: ABL_FV_IMG
	python $(WORKSPACE)/QcomModulePkg/Tools/image_header.py $(ABL_FV_IMG) $(ABL_FV_ELF) $(LOAD_ADDRESS) elf 32
