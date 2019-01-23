BOOT_DEVINFO_DIR := $(ANDROID_BUILD_TOP)/bootable/bootloader/edk2/devinfo

DEVINFO_IMAGE_OUT := $(ANDROID_BUILD_TOP)/out/target/product/K81926AA1/

DEVINFO_IMAGE := $(ANDROID_BUILD_TOP)/bootable/bootloader/edk2/devinfo/tools/devinfo.bin

SW_VERSION := $(ANDROID_BUILD_TOP)/out/target/product/K81926AA1/sw_version

MACHINE_TYPE := 0x926
PRELOADER_VER := 0x001

ifeq ($(TARGET_BUILD_VARIANT),user)
    BUILD_VARIANT := 1
else
    BUILD_VARIANT := 0
endif

ifeq ($(strip $(MZ_INTERNATIONAL)),intl)
  MODEL_TYPE := 5
  ifeq ($(strip $(AGENCY_CUSTOMIZE)),true)
    MODEL_TYPE := 4
  endif
else
  ifeq ($(strip $(ALIYUNOS_CUSTOMIZE)),true)
    MODEL_TYPE := A
    ifeq ($(strip $(CHINAMOBILE_CUSTOMIZE)),true)
      MODEL_TYPE := 7
    endif
    ifeq ($(strip $(CHINAMOBILE_CUSTOMIZE_F)),true)
      MODEL_TYPE := 7
    endif
    ifeq ($(strip $(CHINAMOBILE_TEST)),true)
      MODEL_TYPE := 7
    endif
    ifeq ($(strip $(CHINAUNICOM_CUSTOMIZE)),true)
      MODEL_TYPE := 8
    endif
    ifeq ($(strip $(CHINATELECOM_CUSTOMIZE)),true)
      MODEL_TYPE := 9
    endif
    ifeq ($(strip $(CHINATELECOM_PUBLIC)),true)
      MODEL_TYPE := E
    endif
  else
    MODEL_TYPE := 0
    ifeq ($(strip $(CHINAUNICOM_CUSTOMIZE)),true)
      MODEL_TYPE := 1
    endif
    ifeq ($(strip $(CHINAMOBILE_CUSTOMIZE)),true)
      MODEL_TYPE := 2
    endif
    ifeq ($(strip $(CHINAMOBILE_CUSTOMIZE_F)),true)
      MODEL_TYPE := 2
    endif
    ifeq ($(strip $(CHINAMOBILE_TEST)),true)
      MODEL_TYPE := 2
    endif
    ifeq ($(strip $(CHINAMOBILE_PUBLIC)),true)
      MODEL_TYPE := 3
    endif
    ifeq ($(strip $(CHINATELECOM_CUSTOMIZE)),true)
      MODEL_TYPE := 6
    endif
    ifeq ($(strip $(CHINATELECOM_PUBLIC)),true)
      MODEL_TYPE := B
    endif
  endif
endif

MKDEVINFO := $(BOOT_DEVINFO_DIR)/tools/mkdevinfo

$(TARGET_ABL):$(DEVINFO_IMAGE)

$(DEVINFO_IMAGE):
	@echo "make sw_version:"
	@echo "$(MACHINE_TYPE)$(MODEL_TYPE)$(BUILD_VARIANT)$(subst 0x,,$(PRELOADER_VER))" > $(SW_VERSION)
	@cat $(SW_VERSION)
	@echo "MKDEVINFO $(DEVINFO_IMAGE) start"
	$(MKDEVINFO) $(DEVINFO_IMAGE) $(subst 0x,,$(MACHINE_TYPE)) $(MODEL_TYPE) $(BUILD_VARIANT) $(subst 0x,,$(PRELOADER_VER))
	@mv $(BOOT_DEVINFO_DIR)/tools/devinfo.bin $(DEVINFO_IMAGE_OUT)
