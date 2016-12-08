/*
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
 */

#include "LinuxLoaderLib.h"
#include "BootLinux.h"
#include "KeyPad.h"
#include <Library/MemoryAllocationLib.h>
#include "BootStats.h"
#include <Library/PartitionTableUpdate.h>
#include <Library/DrawUI.h>
#include <Library/StackCanary.h>

#define MAX_APP_STR_LEN      64
#define MAX_NUM_FS           10

STATIC BOOLEAN BootReasonAlarm = FALSE;
STATIC BOOLEAN BootIntoFastboot = FALSE;
STATIC BOOLEAN BootIntoRecovery = FALSE;
DeviceInfo DevInfo;

// This function would load and authenticate boot/recovery partition based
// on the partition type from the entry function.
STATIC EFI_STATUS LoadLinux (CHAR16 *Pname, BOOLEAN MultiSlotBoot, BOOLEAN BootIntoRecovery)
{
	EFI_STATUS Status = EFI_SUCCESS;
	VOID* ImageBuffer = NULL;
	UINT32 ImageSizeActual = 0;
	CHAR16* CurrentSlot = NULL;

	Status = LoadImage(Pname, (VOID**)&ImageBuffer, &ImageSizeActual);
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "ERROR: Failed to load image from partition: %r\n", Status));
		return EFI_LOAD_ERROR;
	}

	if (MultiSlotBoot) {
		CurrentSlot = GetCurrentSlotSuffix();
		MarkPtnActive(CurrentSlot);
	}
	// call start Linux here
	BootLinux(ImageBuffer, ImageSizeActual, &DevInfo, Pname, BootIntoRecovery);
	// would never return here
	return EFI_ABORTED;
}

STATIC UINT8 GetRebootReason(UINT32 *ResetReason)
{
	EFI_RESETREASON_PROTOCOL *RstReasonIf;
	EFI_STATUS Status;

	Status = gBS->LocateProtocol(&gEfiResetReasonProtocolGuid, NULL, (VOID **) &RstReasonIf);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Error locating the reset reason protocol\n"));
		return Status;
	}

	RstReasonIf->GetResetReason(RstReasonIf, ResetReason, NULL, NULL);
	if (RstReasonIf->Revision >= EFI_RESETREASON_PROTOCOL_REVISION)
		RstReasonIf->ClearResetReason(RstReasonIf);
	return Status;
}

/**
  Linux Loader Application EntryPoint

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.
  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

 **/

EFI_STATUS EFIAPI LinuxLoaderEntry(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_STATUS Status;

	UINT32 BootReason = NORMAL_MODE;
	UINT32 KeyPressed;
	CHAR8 Fastboot[MAX_APP_STR_LEN];
	CHAR8 *AppList[] = {Fastboot};
	UINT32 i;
	CHAR16 Pname[MAX_GPT_NAME_SIZE];
	CHAR16 BootableSlot[MAX_GPT_NAME_SIZE];
	/* MultiSlot Boot */
	BOOLEAN MultiSlotBoot;

	DEBUG((EFI_D_INFO, "Loader Build Info: %a %a\n", __DATE__, __TIME__));

	StackGuardChkSetup();

	BootStatsSetTimeStamp(BS_BL_START);

	// Initialize verified boot & Read Device Info
	Status = ReadWriteDeviceInfo(READ_CONFIG, (UINT8 *)&DevInfo, sizeof(DevInfo));
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Unable to Read Device Info: %r\n", Status));
		return Status;
	}

	if (CompareMem(DevInfo.magic, DEVICE_MAGIC, DEVICE_MAGIC_SIZE))
	{
		DEBUG((EFI_D_ERROR, "Device Magic does not match\n"));
		CopyMem(DevInfo.magic, DEVICE_MAGIC, DEVICE_MAGIC_SIZE);
		if (IsSecureBootEnabled())
		{
			DevInfo.is_unlocked = FALSE;
			DevInfo.is_unlock_critical = FALSE;
		}
		else
		{
			DevInfo.is_unlocked = TRUE;
			DevInfo.is_unlock_critical = TRUE;
		}
		DevInfo.is_charger_screen_enabled = FALSE;
		DevInfo.verity_mode = TRUE;
		Status = ReadWriteDeviceInfo(WRITE_CONFIG, (UINT8 *)&DevInfo, sizeof(DevInfo));
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "Unable to Write Device Info: %r\n", Status));
			return Status;
		}
	}

	Status = EnumeratePartitions();

	if (EFI_ERROR (Status)) {
		DEBUG ((EFI_D_ERROR, "LinuxLoader: Could not enumerate partitions: %r\n", Status));
		return Status;
	}

	UpdatePartitionEntries();
	/*Check for multislot boot support*/
	MultiSlotBoot = PartitionHasMultiSlot(L"boot");
	if(MultiSlotBoot) {
		DEBUG((EFI_D_VERBOSE, "Multi Slot boot is supported\n"));
		FindPtnActiveSlot();
	}

	Status = GetKeyPress(&KeyPressed);
	if (Status == EFI_SUCCESS)
	{
		if (KeyPressed == SCAN_DOWN)
			BootIntoFastboot = TRUE;
		if (KeyPressed == SCAN_UP)
			BootIntoRecovery = TRUE;
		if (KeyPressed == SCAN_ESC)
			RebootDevice(EMERGENCY_DLOAD);
	}
	else if (Status == EFI_DEVICE_ERROR)
	{
		DEBUG((EFI_D_ERROR, "Error reading key status: %r\n", Status));
		return Status;
	}

	// check for reboot mode
	Status = GetRebootReason(&BootReason);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Failed to get Reboot reason: %r\n", Status));
		return Status;
	}

	switch (BootReason)
	{
		case FASTBOOT_MODE:
			BootIntoFastboot = TRUE;
			break;
		case RECOVERY_MODE:
			BootIntoRecovery = TRUE;
			break;
		case ALARM_BOOT:
			BootReasonAlarm = TRUE;
			break;
		case DM_VERITY_ENFORCING:
			DevInfo.verity_mode = 1;
			// write to device info
			Status = ReadWriteDeviceInfo(WRITE_CONFIG, &DevInfo, sizeof(DevInfo));
			if (Status != EFI_SUCCESS)
			{
				DEBUG((EFI_D_ERROR, "VBRwDeviceState Returned error: %r\n", Status));
				return Status;
			}
			break;
		case DM_VERITY_LOGGING:
			DevInfo.verity_mode = 0;
			// write to device info
			Status = ReadWriteDeviceInfo(WRITE_CONFIG, &DevInfo, sizeof(DevInfo));
			if (Status != EFI_SUCCESS)
			{
				DEBUG((EFI_D_ERROR, "VBRwDeviceState Returned error: %r\n", Status));
				return Status;
			}
			break;
		case DM_VERITY_KEYSCLEAR:
			Status = ResetDeviceState();
			if (Status != EFI_SUCCESS) {
				DEBUG((EFI_D_ERROR, "VB Reset Device State error: %r\n", Status));
				return Status;
			}
			break;
		default:
			break;
	}

	/* Backup the boot logo blt buffer */
	Status = BackUpBootLogoBltBuffer();
	if (Status != EFI_SUCCESS)
		DEBUG((EFI_D_VERBOSE, "Backup the boot logo blt buffer failed: %r\n", Status));

	Status = RecoveryInit(&BootIntoRecovery);
	if (Status != EFI_SUCCESS)
		DEBUG((EFI_D_VERBOSE, "RecoveryInit failed ignore: %r\n", Status));

	if (!BootIntoFastboot) {

		if (MultiSlotBoot) {
			FindBootableSlot(BootableSlot, sizeof(BootableSlot));
			if(!BootableSlot[0])
				goto fastboot;
			StrnCpyS(Pname, MAX_GPT_NAME_SIZE, BootableSlot, StrLen(BootableSlot));
		} else {

			if(BootIntoRecovery == TRUE) {
				DEBUG((EFI_D_INFO, "Booting Into Recovery Mode\n"));
				StrnCpyS(Pname, MAX_GPT_NAME_SIZE, L"recovery", StrLen(L"recovery"));
			} else {
				DEBUG((EFI_D_INFO, "Booting Into Mission Mode\n"));
				StrnCpyS(Pname, MAX_GPT_NAME_SIZE, L"boot", StrLen(L"boot"));
			}
		}

		Status = LoadLinux(Pname, MultiSlotBoot, BootIntoRecovery);
		if (Status != EFI_SUCCESS)
			DEBUG((EFI_D_ERROR, "Failed to boot Linux, Reverting to fastboot mode\n"));
	}

fastboot:
	DEBUG((EFI_D_INFO, "Launching fastboot\n"));
	for (i = 0 ; i < MAX_NUM_FS; i++)
	{
		SetMem(Fastboot, MAX_APP_STR_LEN, 0);
		AsciiSPrint(Fastboot, MAX_APP_STR_LEN, "fs%d:Fastboot", i);
		Status = LaunchApp(1, AppList);
	}

	if (EFI_ERROR(Status))
	{
		DEBUG((EFI_D_ERROR, "Failed to Launch Fastboot App: %d\n", Status));
		return Status;
	}

	return Status;
}
