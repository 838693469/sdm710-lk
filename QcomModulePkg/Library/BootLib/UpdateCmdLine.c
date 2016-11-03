/** @file UpdateCmdLine.c
 *
 * Copyright (c) 2009, Google Inc.
 * All rights reserved.
 *
 * Copyright (c) 2009-2016, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of The Linux Foundation nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 **/

#include "UpdateCmdLine.h"
#include "Recovery.h"
#include <Library/PrintLib.h>
#include <Library/BootLinux.h>
#include <Library/PartitionTableUpdate.h>
#include <Protocol/EFICardInfo.h>
#include <Protocol/EFIChipInfoTypes.h>
#include <Protocol/Print2.h>
#include <Protocol/EFIPmicPon.h>
#include <Protocol/EFIChargerEx.h>
#include <DeviceInfo.h>
#include <LinuxLoaderLib.h>

STATIC CONST CHAR8 *bootdev_cmdline = " androidboot.bootdevice=1da4000.ufshc";
STATIC CONST CHAR8 *usb_sn_cmdline = " androidboot.serialno=";
STATIC CONST CHAR8 *androidboot_mode = " androidboot.mode=";
STATIC CONST CHAR8 *loglevel         = " quite";
STATIC CONST CHAR8 *battchg_pause = " androidboot.mode=charger";
STATIC CONST CHAR8 *auth_kernel = " androidboot.authorized_kernel=true";
//STATIC CONST CHAR8 *secondary_gpt_enable = "gpt";

/*Send slot suffix in cmdline with which we have booted*/
STATIC CHAR8 *AndroidSlotSuffix = " androidboot.slot_suffix=";
STATIC CHAR8 *MultiSlotCmdSuffix = " rootwait ro init=/init";
STATIC CHAR8 *SkipRamFs = " skip_initramfs";
STATIC CHAR8 *SystemPath;

/* Assuming unauthorized kernel image by default */
STATIC INT32 auth_kernel_img = 0;

/* Display command line related structures */
#define MAX_DISPLAY_CMD_LINE 256
CHAR8 display_cmdline[MAX_DISPLAY_CMD_LINE];
UINTN display_cmdline_len = sizeof(display_cmdline);

#if VERIFIED_BOOT
DeviceInfo DevInfo;
STATIC CONST CHAR8 *verity_mode = " androidboot.veritymode=";
STATIC CONST CHAR8 *verified_state = " androidboot.verifiedbootstate=";
STATIC struct verified_boot_verity_mode vbvm[] =
{
	{FALSE, "logging"},
	{TRUE, "enforcing"},
};

STATIC struct verified_boot_state_name vbsn[] =
{
	{GREEN, "green"},
	{ORANGE, "orange"},
	{YELLOW, "yellow"},
	{RED, "red"},
};
#endif

/*Function that returns whether the kernel is signed
 *Currently assumed to be signed*/
BOOLEAN target_use_signed_kernel(VOID)
{
	return 1;
}

/*Determines whether to pause for batter charge,
 *Serves only performance purposes, defaults to return zero*/
UINT32 target_pause_for_battery_charge(VOID)
{
	EFI_STATUS Status;
	EFI_PM_PON_REASON_TYPE pon_reason;
	EFI_QCOM_PMIC_PON_PROTOCOL *PmicPonProtocol;
	EFI_QCOM_CHARGER_EX_PROTOCOL *ChgDetectProtocol;
	BOOLEAN chgpresent;
	BOOLEAN WarmRtStatus;
	BOOLEAN IsColdBoot;

	Status = gBS->LocateProtocol(&gQcomPmicPonProtocolGuid, NULL,
			(VOID **) &PmicPonProtocol);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error locating pmic pon protocol: %r\n", Status));
		return Status;
	}

	/* Passing 0 for PMIC device Index since the protocol infers internally */
	Status = PmicPonProtocol->GetPonReason(0, &pon_reason);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error getting pon reason: %r\n", Status));
		return Status;
	}
	Status = PmicPonProtocol->WarmResetStatus(0, &WarmRtStatus);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error getting warm reset status: %r\n", Status));
		return Status;
	}
	IsColdBoot = !WarmRtStatus;
	Status = gBS->LocateProtocol(&gQcomChargerExProtocolGuid, NULL, (void **) &ChgDetectProtocol);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error locating charger detect protocol: %r\n", Status));
		return Status;
	}
	Status = ChgDetectProtocol->GetChargerPresence(&chgpresent);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error getting charger info: %r\n", Status));
		return Status;
	}
	DEBUG((EFI_D_INFO, " pon_reason is %d cold_boot:%d charger path: %d\n",
		pon_reason, IsColdBoot, chgpresent));
	/* In case of fastboot reboot,adb reboot or if we see the power key
	 * pressed we do not want go into charger mode.
	 * fastboot/adb reboot is warm boot with PON hard reset bit set.
	 */
	if (IsColdBoot &&
			(!(pon_reason.HARD_RESET) &&
			(!(pon_reason.KPDPWR)) &&
			(pon_reason.PON1) &&
			(chgpresent)))
		return 1;
	else
		return 0;
}

/**
  Check battery status
  @param[out] BatteryPresent  The pointer to battry's presence status.
  @param[out] ChargerPresent  The pointer to battry's charger status.
  @param[out] BatteryVoltage  The pointer to battry's voltage.
  @retval     EFI_SUCCESS     Check battery status successfully.
  @retval     other           Failed to check battery status.
**/
STATIC EFI_STATUS TargetCheckBatteryStatus(BOOLEAN *BatteryPresent, BOOLEAN *ChargerPresent,
	UINT32 *BatteryVoltage)
{
	EFI_STATUS Status = EFI_SUCCESS;
	EFI_QCOM_CHARGER_EX_PROTOCOL *ChgDetectProtocol;

	Status = gBS->LocateProtocol(&gQcomChargerExProtocolGuid, NULL, (void **) &ChgDetectProtocol);
	if (EFI_ERROR(Status) || (NULL == ChgDetectProtocol))
	{
		DEBUG((EFI_D_ERROR, "Error locating charger detect protocol\n"));
		return EFI_PROTOCOL_ERROR;
	}

	Status = ChgDetectProtocol->GetBatteryPresence(BatteryPresent);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error getting battery presence: %r\n", Status));
		return Status;
	}

	Status = ChgDetectProtocol->GetBatteryVoltage(BatteryVoltage);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error getting battery voltage: %r\n", Status));
		return Status;
	}

	Status = ChgDetectProtocol->GetChargerPresence(ChargerPresent);
	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Error getting charger presence: %r\n", Status));
		return Status;
	}

	return Status;
}

/**
   Add safeguards such as refusing to flash if the battery levels is lower than the min voltage
   or bypass if the battery is not present.
   @param[out] BatteryVoltage  The pointer to battry's voltage.
   @retval     BOOLEAN         The value whether the device is allowed to flash image.
 **/
BOOLEAN TargetBatterySocOk(UINT32  *BatteryVoltage)
{
	EFI_STATUS  BatteryStatus;
	BOOLEAN BatteryPresent = FALSE;
	BOOLEAN ChargerPresent = FALSE;

	BatteryStatus = TargetCheckBatteryStatus(&BatteryPresent, &ChargerPresent, BatteryVoltage);
	if ((BatteryStatus == EFI_SUCCESS) && (!BatteryPresent || (BatteryPresent && (BatteryVoltage > BATT_MIN_VOLT))))
		return TRUE;

	return FALSE;
}

VOID GetDisplayCmdline()
{
	EFI_STATUS Status;

	Status = gRT->GetVariable(
			L"DisplayPanelConfiguration",
			&gQcomTokenSpaceGuid,
			NULL,
			&display_cmdline_len,
			display_cmdline);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Unable to get Panel Config, %r\n", Status));
	}
}

/*
 * Returns length = 0 when there is failure.
 */
STATIC UINT32 GetSystemPath(CHAR8 **SysPath)
{
	INTN Index;
	UINTN Lun;
	CHAR16 PartitionName[MAX_GPT_NAME_SIZE];
	CHAR16* CurSlotSuffix = GetCurrentSlotSuffix();
	CHAR8 LunCharMapping[] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
	HandleInfo HandleInfoList[HANDLE_MAX_INFO_LIST];
	UINT32 MaxHandles = ARRAY_SIZE(HandleInfoList);
	MemCardType Type = UNKNOWN;

	*SysPath = AllocatePool(sizeof(char) * MAX_PATH_SIZE);
	if (!*SysPath) {
		DEBUG((EFI_D_ERROR, "Failed to allocated memory for System path query\n"));
		return 0;
	}

	StrnCpyS(PartitionName, MAX_GPT_NAME_SIZE, L"system", StrLen(L"system"));
	StrnCatS(PartitionName, MAX_GPT_NAME_SIZE, CurSlotSuffix, StrLen(CurSlotSuffix));

	Index = GetPartitionIndex(PartitionName);
	if (Index == INVALID_PTN) {
		DEBUG((EFI_D_ERROR, "System partition does not exit\n"));
		FreePool(*SysPath);
		return 0;
	}

	Lun = GetPartitionLunFromIndex(Index);
	Type = CheckRootDeviceType(HandleInfoList, MaxHandles);
	if (Type == UNKNOWN)
		return 0;

	if (Type == EMMC)
		AsciiSPrint(*SysPath, MAX_PATH_SIZE, " root=/dev/mmcblk0p%d", (Index + 1));
	else
		AsciiSPrint(*SysPath, MAX_PATH_SIZE, " root=/dev/sd%c%d", LunCharMapping[Lun],
				GetPartitionIdxInLun(PartitionName, Lun));

	DEBUG((EFI_D_VERBOSE, "System Path - %a \n", *SysPath));

	return AsciiStrLen(*SysPath);
}


/*Update command line: appends boot information to the original commandline
 *that is taken from boot image header*/
UINT8 *update_cmdline(CONST CHAR8 * cmdline, CHAR16 *pname, DeviceInfo *devinfo, BOOLEAN Recovery)
{
	EFI_STATUS Status;
	UINT32 cmdline_len = 0;
	UINT32 have_cmdline = 0;
	UINT32 SysPathLength = 0;
	CHAR8  *cmdline_final = NULL;
	UINT32 pause_at_bootup = 0; //this would have to come from protocol
	BOOLEAN boot_into_ffbm = FALSE;
	CHAR8 SlotSuffixAscii[MAX_SLOT_SUFFIX_SZ];
	BOOLEAN MultiSlotBoot;
	CHAR8 ChipBaseBand[CHIP_BASE_BAND_LEN];

	CHAR8 ffbm[FFBM_MODE_BUF_SIZE];
	if ((!StrnCmp(pname, L"boot_a", StrLen(pname)))
		|| (!StrnCmp(pname, L"boot_b", StrLen(pname))))
	{
		SetMem(ffbm, FFBM_MODE_BUF_SIZE, 0);
		Status = GetFfbmCommand(ffbm, sizeof(ffbm));
		if (Status == EFI_NOT_FOUND)
			DEBUG((EFI_D_ERROR, "No Ffbm cookie found, ignore\n"));
		else if (Status == EFI_SUCCESS)
			boot_into_ffbm = TRUE;

	}

	MEM_CARD_INFO card_info = {};
	EFI_MEM_CARDINFO_PROTOCOL *pCardInfoProtocol=NULL;
	CHAR8 StrSerialNum[64];

	Status = BoardSerialNum(StrSerialNum, sizeof(StrSerialNum));
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Error Finding board serial num: %x\n", Status));
		return Status;
	}

	if (cmdline && cmdline[0])
	{
		cmdline_len = AsciiStrLen(cmdline);
		have_cmdline = 1;
	}
#if VERIFIED_BOOT
	if ((DevInfo.verity_mode != 0) && (DevInfo.verity_mode != 1))
	{
		DEBUG((EFI_D_ERROR, "Devinfo partition possibly corrupted!!!. Please erase devinfo partition to continue booting.\n"));
		return NULL;
	}
	cmdline_len += AsciiStrLen(verity_mode) + AsciiStrLen(vbvm[DevInfo.verity_mode].name);
#endif

	cmdline_len += AsciiStrLen(bootdev_cmdline);

	cmdline_len += AsciiStrLen(usb_sn_cmdline);
	cmdline_len += AsciiStrLen(StrSerialNum);

	if (boot_into_ffbm)
	{
		cmdline_len += AsciiStrLen(androidboot_mode);
		cmdline_len += AsciiStrLen(ffbm);
		/* reduce kernel console messages to speed-up boot */
		cmdline_len += AsciiStrLen(loglevel);
	}
	else if (target_pause_for_battery_charge() && devinfo->is_charger_screen_enabled)
	{
		DEBUG((EFI_D_INFO, "Device will boot into off mode charging mode\n"));
		pause_at_bootup = 1;
		cmdline_len += AsciiStrLen(battchg_pause);
	}

	if(target_use_signed_kernel() && auth_kernel_img)
	{
		cmdline_len += AsciiStrLen(auth_kernel);
	}

	if (NULL == BoardPlatformChipBaseBand()) {
		DEBUG((EFI_D_ERROR, "Invalid BaseBand String\n"));
		return NULL;
	}

	cmdline_len += AsciiStrLen(BOOT_BASE_BAND);
	cmdline_len += AsciiStrLen(BoardPlatformChipBaseBand());

	MultiSlotBoot = PartitionHasMultiSlot(L"boot");
	if(MultiSlotBoot) {
		cmdline_len += AsciiStrLen(AndroidSlotSuffix) + 2;

		cmdline_len += AsciiStrLen(MultiSlotCmdSuffix);

		if (!Recovery)
			cmdline_len += AsciiStrLen(SkipRamFs);

		SysPathLength = GetSystemPath(&SystemPath);
		if (!SysPathLength)
			return NULL;
		cmdline_len += SysPathLength;
	}

	GetDisplayCmdline();
	cmdline_len += AsciiStrLen(display_cmdline);

#define STR_COPY(dst,src)  {while (*src){*dst = *src; ++src; ++dst; } *dst = 0; ++dst;}
	if (cmdline_len > 0)
	{
		CONST CHAR8 *src;

		CHAR8* dst;
		dst = AllocatePool (cmdline_len + 4);
		if (!dst)
		{
			DEBUG((EFI_D_ERROR, "CMDLINE: Failed to allocate destination buffer\n"));
			return NULL;
		}

		SetMem(dst, cmdline_len + 4, 0x0);

		/* Save start ptr for debug print */
		cmdline_final = dst;
		if (have_cmdline)
		{
			src = cmdline;
			STR_COPY(dst,src);
		}

		src = bootdev_cmdline;
		if (have_cmdline) --dst;
		have_cmdline = 1;
		STR_COPY(dst,src);

		src = usb_sn_cmdline;
		if (have_cmdline) --dst;
		have_cmdline = 1;
		STR_COPY(dst,src);
		if (have_cmdline) --dst;
		have_cmdline = 1;
		src = StrSerialNum;
		STR_COPY(dst,src);

		if (boot_into_ffbm) {
			src = androidboot_mode;
			if (have_cmdline) --dst;
			STR_COPY(dst,src);
			src = ffbm;
			if (have_cmdline) --dst;
			STR_COPY(dst,src);
			src = loglevel;
			if (have_cmdline) --dst;
			STR_COPY(dst,src);
		} else if (pause_at_bootup) {
			src = battchg_pause;
			if (have_cmdline) --dst;
			STR_COPY(dst,src);
		}

		if(target_use_signed_kernel() && auth_kernel_img)
		{
			src = auth_kernel;
			if (have_cmdline) --dst;
			STR_COPY(dst,src);
		}

		src = BOOT_BASE_BAND;
		if (have_cmdline) --dst;
		STR_COPY(dst,src);
		--dst;
		SetMem(ChipBaseBand, CHIP_BASE_BAND_LEN, 0);
		AsciiStrnCpyS(ChipBaseBand, CHIP_BASE_BAND_LEN, BoardPlatformChipBaseBand(), CHIP_BASE_BAND_LEN-1);
		ToLower(ChipBaseBand);
		src = ChipBaseBand;
		STR_COPY(dst,src);

		src = display_cmdline;
		if (have_cmdline) --dst;
		STR_COPY(dst,src);
		if (MultiSlotBoot)
		{
			/* Slot suffix */
			src = AndroidSlotSuffix;
			if (have_cmdline) --dst;
			STR_COPY(dst,src);
			--dst;
			UnicodeStrToAsciiStr(GetCurrentSlotSuffix(), SlotSuffixAscii);
			src = SlotSuffixAscii;
			STR_COPY(dst,src);

			/* Skip Initramfs*/
			if (!Recovery) {
				src = SkipRamFs;
				if (have_cmdline) --dst;
				STR_COPY(dst, src);
			}

			/*Add Multi slot command line suffix*/
			src = MultiSlotCmdSuffix;
			if (have_cmdline) --dst;
			STR_COPY(dst, src);

			/* Suffix System path in command line*/
			if (*SystemPath) {
				src = SystemPath;
				if (have_cmdline) --dst;
				STR_COPY(dst, src);
			}
		}
	}
	DEBUG((EFI_D_INFO, "Cmdline: %a\n", cmdline_final));

	return (UINT8 *)cmdline_final;
}
