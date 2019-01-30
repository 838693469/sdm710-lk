/*
 * Copyright (c) 2009, Google Inc.
 * All rights reserved.
 *
 * Copyright (c) 2009-2018, The Linux Foundation. All rights reserved.
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

#include "AutoGen.h"
#include "BootLinux.h"
#include "BootStats.h"
#include "KeyPad.h"
#include "LinuxLoaderLib.h"
#include <FastbootLib/FastbootMain.h>
#include <Library/DeviceInfo.h>
#include <Library/DrawUI.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PartitionTableUpdate.h>
#include <Library/ShutdownServices.h>
#include <Library/StackCanary.h>
#include <Protocol/EFITlmm.h>
#include <Library/wt_system_monitor.h>
#include <Library/HypervisorMvCalls.h>

#define MAX_APP_STR_LEN 64
#define MAX_NUM_FS 10
#define DEFAULT_STACK_CHK_GUARD 0xc0c0c0c0

CHAR8 boardID_cmdline[48] = {'\0'};  //bug400055 add board id info to uefi,gouji@wt,20181023
CHAR8 g_SSN[30] = {'\0'}, g_PSN[30]={'\0'};//bug847136 add read ssn info,dingxiaobo@wt,20181102
CHAR8 project_name_cmdline[48] = {'\0'};  //bug875090 add board id info to uefi,gouji@wt,20190117

BOOLEAN uart_log_enable = FALSE;
STATIC BOOLEAN BootReasonAlarm = FALSE;
STATIC BOOLEAN BootIntoFastboot = FALSE;
STATIC BOOLEAN BootIntoRecovery = FALSE;

STATIC UINT32 board_value = 0;
STATIC UINT32 board_value2 = 0;

//bug400055 add board id info to uefi,gouji@wt,20181023,start
struct board_resistor_name
{
	UINT32 resistor_value;
	CHAR8 board_name[64];
};

STATIC struct board_resistor_name board_id_arr[] =
{
	{0, "Reserved"},
	{1, "K81926EA1"},
	{2, "K81926DA1"},
	{3, "K81926AA1"},
};

STATIC struct board_resistor_name project_name_arr[] =
{
	{0, "Reserved"},
	{1, "K81926EA1"},
	{2, "K81926FA1"},
	{3, "K81926AA1"},
};
//bug400055 add board id info to uefi,gouji@wt,20181023,end
STATIC VOID* UnSafeStackPtr;

STATIC EFI_STATUS __attribute__ ( (no_sanitize ("safe-stack")))
AllocateUnSafeStackPtr (VOID)
{

  EFI_STATUS Status = EFI_SUCCESS;

  UnSafeStackPtr = AllocateZeroPool (BOOT_LOADER_MAX_UNSAFE_STACK_SIZE);
  if (UnSafeStackPtr == NULL) {
    DEBUG ((EFI_D_ERROR, "Failed to Allocate memory for UnSafeStack \n"));
    Status = EFI_OUT_OF_RESOURCES;
    return Status;
  }

  UnSafeStackPtr += BOOT_LOADER_MAX_UNSAFE_STACK_SIZE;

  return Status;
}

//This function is to return the Unsafestack ptr address
VOID** __attribute__ ( (no_sanitize ("safe-stack")))
__safestack_pointer_address (VOID)
{

  return (VOID**) &UnSafeStackPtr;
}

// This function is used to Deactivate MDTP by entering recovery UI

STATIC EFI_STATUS MdtpDisable (VOID)
{
  BOOLEAN MdtpActive = FALSE;
  EFI_STATUS Status = EFI_SUCCESS;
  QCOM_MDTP_PROTOCOL *MdtpProtocol;

  if (FixedPcdGetBool (EnableMdtpSupport)) {
    Status = IsMdtpActive (&MdtpActive);

    if (EFI_ERROR (Status))
      return Status;

    if (MdtpActive) {
      Status = gBS->LocateProtocol (&gQcomMdtpProtocolGuid, NULL,
                                    (VOID **)&MdtpProtocol);
      if (EFI_ERROR (Status)) {
        DEBUG ((EFI_D_ERROR, "Failed to locate MDTP protocol, Status=%r\n",
                Status));
        return Status;
      }
      /* Perform Local Deactivation of MDTP */
      Status = MdtpProtocol->MdtpDeactivate (MdtpProtocol, FALSE);
    }
  }

  return Status;
}

//bug400055 add board id info to uefi,gouji@wt,20181023,start
#define WT_GPIO_BOARDID1  115
#define WT_GPIO_BOARDID2  116

#define WT_HW_BOARDID3  117  //最低位
#define WT_HW_BOARDID2  113
#define WT_HW_BOARDID1  114  //最高位

STATIC UINT8 Read_BoardId(VOID)
{
	EFI_STATUS Status = EFI_SUCCESS;
	EFI_TLMM_PROTOCOL *TLMMProtocol = NULL;
	UINT32 config;
	UINT32 value;

	/* Locate the TLMM protocol & then configure the GPIO 115/116 */
	Status = gBS->LocateProtocol( &gEfiTLMMProtocolGuid, NULL, (void**)&TLMMProtocol);
	if(EFI_SUCCESS != Status)
	{
		DEBUG((EFI_D_ERROR, "Locate TLMM Protocol Failed!\n"));
		return Status;
	}

	/* Configure the BoardId line & enable it */
	config =  EFI_GPIO_CFG( WT_GPIO_BOARDID2, 0, GPIO_INPUT, GPIO_PULL_UP, GPIO_2MA );
	Status = TLMMProtocol->ConfigGpio((UINT32)config,TLMM_GPIO_ENABLE);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Unable to Configure MSM GPIO %d ENABLE !!\n",WT_GPIO_BOARDID2));
	}
	//Status = TLMMProtocol->GpioIn(config, &value);
	TLMMProtocol->GpioIn(config, &value);

	DEBUG((EFI_D_ERROR, "board_value, MSM GPIO %u =%u\n",WT_GPIO_BOARDID2,value));
	board_value = value << 1;
	config =  EFI_GPIO_CFG( WT_GPIO_BOARDID1, 0, GPIO_INPUT, GPIO_PULL_UP, GPIO_2MA );
	Status = TLMMProtocol->ConfigGpio((UINT32)config,TLMM_GPIO_ENABLE);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Unable to Configure MSM GPIO %d ENABLE !!\n",WT_GPIO_BOARDID1));
	}
	Status = TLMMProtocol->GpioIn(config, &value);

	DEBUG((EFI_D_ERROR, "board_value,MSM GPIO %u =%u\n",WT_GPIO_BOARDID1,value));
	board_value += value;

	DEBUG((EFI_D_ERROR, "board_value is 0x%x !!\n",board_value));
	return Status;
}
STATIC UINT8 Read_HW_BoardId(VOID)
{
	EFI_STATUS Status = EFI_SUCCESS;
	EFI_TLMM_PROTOCOL *TLMMProtocol = NULL;
	UINT32 config;
	UINT32 value;

	/* Locate the TLMM protocol & then configure the GPIO 42/43/40 */
	Status = gBS->LocateProtocol( &gEfiTLMMProtocolGuid, NULL, (void**)&TLMMProtocol);
	if(EFI_SUCCESS != Status)
	{
		DEBUG((EFI_D_ERROR, "Locate TLMM Protocol Failed!\n"));
		return Status;
	}

	/* Configure the BoardId line & enable it */
	config =  EFI_GPIO_CFG( WT_HW_BOARDID1, 0, GPIO_INPUT, GPIO_PULL_UP, GPIO_2MA );
	Status = TLMMProtocol->ConfigGpio((UINT32)config,TLMM_GPIO_ENABLE);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Unable to Configure MSM GPIO %d ENABLE !!\n",WT_HW_BOARDID1));
	}
	//Status = TLMMProtocol->GpioIn(config, &value);
	TLMMProtocol->GpioIn(config, &value);
	board_value2 = value << 2;
	config =  EFI_GPIO_CFG( WT_HW_BOARDID2, 0, GPIO_INPUT, GPIO_PULL_UP, GPIO_2MA );
	Status = TLMMProtocol->ConfigGpio((UINT32)config,TLMM_GPIO_ENABLE);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Unable to Configure MSM GPIO %d ENABLE !!\n",WT_HW_BOARDID2));
	}
	//Status = TLMMProtocol->GpioIn(config, &value);
	TLMMProtocol->GpioIn(config, &value);
	board_value2 = value << 1;

	config =  EFI_GPIO_CFG( WT_HW_BOARDID3, 0, GPIO_INPUT, GPIO_PULL_UP, GPIO_2MA );
	Status = TLMMProtocol->ConfigGpio((UINT32)config,TLMM_GPIO_ENABLE);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Unable to Configure MSM GPIO %d ENABLE !!\n",WT_HW_BOARDID3));
	}
	//Status = TLMMProtocol->GpioIn(config, &value);
	TLMMProtocol->GpioIn(config, &value);
	board_value2 += value;

	DEBUG((EFI_D_ERROR, "board_value2 is 0x%x !!\n",board_value2));
	return Status;
}
//bug400055 add board id info to uefi,gouji@wt,20181023,end

STATIC UINT8
GetRebootReason (UINT32 *ResetReason)
{
  EFI_RESETREASON_PROTOCOL *RstReasonIf;
  EFI_STATUS Status;

  Status = gBS->LocateProtocol (&gEfiResetReasonProtocolGuid, NULL,
                                (VOID **)&RstReasonIf);
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Error locating the reset reason protocol\n"));
    return Status;
  }

  RstReasonIf->GetResetReason (RstReasonIf, ResetReason, NULL, NULL);
  if (RstReasonIf->Revision >= EFI_RESETREASON_PROTOCOL_REVISION)
    RstReasonIf->ClearResetReason (RstReasonIf);
  return Status;
}

/**
  Linux Loader Application EntryPoint

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.
  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

 **/

EFI_STATUS EFIAPI  __attribute__ ( (no_sanitize ("safe-stack")))
LinuxLoaderEntry (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;

  UINT32 BootReason = NORMAL_MODE;
  UINT32 KeyPressed;
  UINT32 i; /*board_index,*/  //bug400055 add board id info to uefi,gouji@wt,20181023
  /* MultiSlot Boot */
  BOOLEAN MultiSlotBoot;

  DEBUG ((EFI_D_INFO, "Loader Build Info: %a %a\n", __DATE__, __TIME__));
  DEBUG ((EFI_D_VERBOSE, "LinuxLoader Load Address to debug ABL: 0x%llx\n",
         (UINTN)LinuxLoaderEntry & (~ (0xFFF))));
  DEBUG ((EFI_D_VERBOSE, "LinuxLoaderEntry Address: 0x%llx\n",
         (UINTN)LinuxLoaderEntry));

  Status = AllocateUnSafeStackPtr ();
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Unable to Allocate memory for Unsafe Stack: %r\n",
            Status));
    goto stack_guard_update_default;
  }

  StackGuardChkSetup ();

  BootStatsSetTimeStamp (BS_BL_START);
  
  //bug400055 add board id info to uefi,gouji@wt,20181023,start
  Status = Read_BoardId();
  Status |= Read_HW_BoardId();
  DEBUG((EFI_D_INFO, "Board_id info %d,Read_HW_BoardId 0x%x\n", board_value,board_value2));
  if (Status != EFI_SUCCESS)
  {
    DEBUG((EFI_D_ERROR, "Failed to get tlmm status: %r\n", Status));
  }
  for(i =0;i < sizeof(board_id_arr)/sizeof(board_id_arr[0]);i++)
  {
    if (board_value == board_id_arr[i].resistor_value)
    {
	   //board_index = i;
	   AsciiStrnCpy(boardID_cmdline, " androidboot.board_id=", AsciiStrLen(" andriodboot.board_id="));
	   AsciiStrCat(boardID_cmdline, board_id_arr[i].board_name);
	   //project name
	   AsciiStrnCpy(project_name_cmdline, " androidboot.project_name=", AsciiStrLen(" androidboot.project_name="));
	   AsciiStrCat(project_name_cmdline, project_name_arr[i].board_name);
	   
	   DEBUG((EFI_D_INFO, "Success gain %a\n",boardID_cmdline));
	   DEBUG((EFI_D_INFO, "Success gain %a\n",project_name_cmdline));
	   break;
    }
  }
  //bug400055 add board id info to uefi,gouji@wt,20181023,end

  // Initialize verified boot & Read Device Info
  Status = DeviceInfoInit ();
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Initialize the device info failed: %r\n", Status));
    goto stack_guard_update_default;
  }

  Status = EnumeratePartitions ();

  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "LinuxLoader: Could not enumerate partitions: %r\n",
            Status));
    goto stack_guard_update_default;
  }

  UpdatePartitionEntries ();
  /*Check for multislot boot support*/
  MultiSlotBoot = PartitionHasMultiSlot ((CONST CHAR16 *)L"boot");
  if (MultiSlotBoot) {
    DEBUG ((EFI_D_VERBOSE, "Multi Slot boot is supported\n"));
    FindPtnActiveSlot ();
  }

  //bug847136 add read ssn info,dingxiaobo@wt,20181102,start
  Status = BoardGetSSNPSN(g_SSN,g_PSN);
  if (Status != EFI_SUCCESS)
  {
    DEBUG((EFI_D_ERROR, "Failed to get ssn psn status: %r\n", Status));
  }
  DEBUG((EFI_D_INFO, "SSN:%a  PSN:%a\n",g_SSN,g_PSN));
  //bug847136 add read ssn info,dingxiaobo@wt,20181102,start

  Status = GetKeyPress (&KeyPressed);
  if (Status == EFI_SUCCESS) {
    //+	EXTB871582 volumn down keypress & plugin usb,enter fastboot mode, liuzhigou.wt,20190110
    if (KeyPressed == SCAN_DOWN || KeyPressed == SCAN_DELETE){
	  DEBUG ((EFI_D_INFO, "KeyPress:%u, Enter  Fastboot Mode\n", KeyPressed));
      BootIntoFastboot = TRUE;
    }
    //- EXTB871582 volumn down keypress & plugin usb,enter fastboot mode, liuzhigou.wt,20190110
    if (KeyPressed == SCAN_UP)
      BootIntoRecovery = TRUE;
    if (KeyPressed == SCAN_ESC){
#ifdef WT_SYSTEM_MONITOR
	set_dload_magic_to_log_ptn();
#endif
        RebootDevice (EMERGENCY_DLOAD);
    }
    if(KeyPressed == SCAN_FASTBOOT) {
      DEBUG((EFI_D_INFO, "------Ctrl C - Enter  Fastboot Mode\n"));
      BootIntoFastboot = TRUE;
    }
    else if(KeyPressed == SCAN_POWEROFF) {
      DEBUG((EFI_D_INFO, "------Ctrl D - Enter  Poweroff Mode\n"));
      ShutdownDevice();
    }
    else if(KeyPressed == SCAN_RECOVERY) {
      DEBUG((EFI_D_INFO, "------Ctrl R - Enter  Recovery Mode\n"));
      BootIntoRecovery = TRUE;
    }else if(KeyPressed == SCAN_KERNEL_LOG){
      DEBUG((EFI_D_INFO, "------Ctrl E / S - OPEN SERAIL LOG\n"));
      uart_log_enable = TRUE;
    }
  } else if (Status == EFI_DEVICE_ERROR) {
    DEBUG ((EFI_D_ERROR, "Error reading key status: %r\n", Status));
    goto stack_guard_update_default;
  }

  // check for reboot mode
  Status = GetRebootReason (&BootReason);
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Failed to get Reboot reason: %r\n", Status));
    goto stack_guard_update_default;
  }

  switch (BootReason) {
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
    // write to device info
    Status = EnableEnforcingMode (TRUE);
    if (Status != EFI_SUCCESS)
      goto stack_guard_update_default;
    break;
  case DM_VERITY_LOGGING:
    /* Disable MDTP if it's Enabled through Local Deactivation */
    Status = MdtpDisable ();
    if (EFI_ERROR (Status) && Status != EFI_NOT_FOUND) {
      DEBUG ((EFI_D_ERROR, "MdtpDisable Returned error: %r\n", Status));
      goto stack_guard_update_default;
    }
    // write to device info
    Status = EnableEnforcingMode (FALSE);
    if (Status != EFI_SUCCESS)
      goto stack_guard_update_default;

    break;
  case DM_VERITY_KEYSCLEAR:
    Status = ResetDeviceState ();
    if (Status != EFI_SUCCESS) {
      DEBUG ((EFI_D_ERROR, "VB Reset Device State error: %r\n", Status));
      goto stack_guard_update_default;
    }
    break;
  default:
    if (BootReason != NORMAL_MODE) {
      DEBUG ((EFI_D_ERROR,
             "Boot reason: 0x%x not handled, defaulting to Normal Boot\n",
             BootReason));
    }
    break;
  }

  Status = RecoveryInit (&BootIntoRecovery);
  if (Status != EFI_SUCCESS)
    DEBUG ((EFI_D_VERBOSE, "RecoveryInit failed ignore: %r\n", Status));

  /* Populate board data required for fastboot, dtb selection and cmd line */
  Status = BoardInit ();
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Error finding board information: %r\n", Status));
    return Status;
  }

  DEBUG ((EFI_D_INFO, "KeyPress:%u, BootReason:%u\n", KeyPressed, BootReason));
  DEBUG ((EFI_D_INFO, "Fastboot=%d, Recovery:%d\n",
                                          BootIntoFastboot, BootIntoRecovery));
  if (!GetVmData ()) {
    DEBUG ((EFI_D_ERROR, "VM Hyp calls not present\n"));
  }

  if (!BootIntoFastboot) {
    BootInfo Info = {0};
    Info.MultiSlotBoot = MultiSlotBoot;
    Info.BootIntoRecovery = BootIntoRecovery;
    Info.BootReasonAlarm = BootReasonAlarm;
    Status = LoadImageAndAuth (&Info);
    if (Status != EFI_SUCCESS) {
      DEBUG ((EFI_D_ERROR, "LoadImageAndAuth failed: %r\n", Status));

      //ExtR841497, liulai.wt, ADD, 20190118, Forced to enter recovery mode after boot.img signature verification failed
      RebootDevice (RECOVERY_MODE);
      goto fastboot;
    }

    BootLinux (&Info);
  }

fastboot:
  DEBUG ((EFI_D_INFO, "Launching fastboot\n"));
  Status = FastbootInitialize ();
  if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Failed to Launch Fastboot App: %d\n", Status));
      goto stack_guard_update_default;
  }

stack_guard_update_default:
  /*Update stack check guard with defualt value then return*/
  __stack_chk_guard = DEFAULT_STACK_CHK_GUARD;
  return Status;
}
