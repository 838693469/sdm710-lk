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

#include <Library/VerifiedBootMenu.h>
#include <Library/DrawUI.h>
#include <Protocol/EFIScmModeSwitch.h>

#include "BootLinux.h"
#include "BootStats.h"
#include "BootImage.h"
#include "UpdateDeviceTree.h"

STATIC BOOLEAN VerifiedBootEnbled();
STATIC QCOM_SCM_MODE_SWITCH_PROTOCOL *pQcomScmModeSwitchProtocol = NULL;

STATIC EFI_STATUS SwitchTo32bitModeBooting(UINT64 KernelLoadAddr, UINT64 DeviceTreeLoadAddr) {
	EFI_STATUS Status;
	EFI_HLOS_BOOT_ARGS HlosBootArgs;

	SetMem((VOID*)&HlosBootArgs, sizeof(HlosBootArgs), 0);
	HlosBootArgs.el1_x2 = DeviceTreeLoadAddr;
	/* Write 0 into el1_x4 to switch to 32bit mode */
	HlosBootArgs.el1_x4 = 0;
	HlosBootArgs.el1_elr = KernelLoadAddr;
	Status = pQcomScmModeSwitchProtocol->SwitchTo32bitMode(HlosBootArgs);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "ERROR: Failed to switch to 32 bit mode.Status= %r\n",Status));
		return Status;
	}
	/*Return Unsupported if the execution ever reaches here*/
	return EFI_NOT_STARTED;
}

EFI_STATUS BootLinux (VOID *ImageBuffer, UINT32 ImageSize, DeviceInfo *DevInfo, CHAR8 *pname, BOOLEAN Recovery)
{

	EFI_STATUS Status;

	LINUX_KERNEL LinuxKernel;
	STATIC UINT32 DeviceTreeOffset;
	STATIC UINT32 RamdiskOffset;
	STATIC UINT32 KernelSizeActual;
	STATIC UINT32 RamdiskSizeActual;
	STATIC UINT32 SecondSizeActual;
	struct kernel64_hdr* Kptr = NULL;

	/*Boot Image header information variables*/
	STATIC UINT32 KernelSize;
	STATIC VOID* KernelLoadAddr;
	STATIC UINT32 RamdiskSize;
	STATIC VOID* RamdiskLoadAddr;
	STATIC VOID* RamdiskEndAddr;
	STATIC UINT32 SecondSize;
	STATIC VOID* DeviceTreeLoadAddr = 0;
	STATIC UINT32 PageSize = 0;
	STATIC UINT32 DtbOffset = 0;
	UINT8* Final_CmdLine;

	STATIC UINT32 out_len = 0;
	STATIC UINT32 out_avai_len = 0;
	STATIC UINT32* CmdLine;
	STATIC UINTN BaseMemory;
	UINT64 Time;
	boot_state_t BootState = BOOT_STATE_MAX;
	QCOM_VERIFIEDBOOT_PROTOCOL *VbIntf;
	device_info_vb_t DevInfo_vb;
	STATIC CHAR8 StrPartition[MAX_PNAME_LENGTH];
	BOOLEAN BootingWith32BitKernel = FALSE;

	if (VerifiedBootEnbled())
	{
		Status = gBS->LocateProtocol(&gEfiQcomVerifiedBootProtocolGuid, NULL, (VOID **) &VbIntf);
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "Unable to locate VB protocol: %r\n", Status));
			return Status;
		}
		DevInfo_vb.is_unlocked = DevInfo->is_unlocked;
		DevInfo_vb.is_unlock_critical = DevInfo->is_unlock_critical;
		Status = VbIntf->VBDeviceInit(VbIntf, (device_info_vb_t *)&DevInfo_vb);
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "Error during VBDeviceInit: %r\n", Status));
			return Status;
		}

		AsciiStrnCpy(StrPartition, "/", MAX_PNAME_LENGTH);
		AsciiStrnCat(StrPartition, pname, MAX_PNAME_LENGTH);

		Status = VbIntf->VBVerifyImage(VbIntf, StrPartition, (UINT8 *) ImageBuffer, ImageSize, &BootState);
		if (Status != EFI_SUCCESS && BootState == BOOT_STATE_MAX)
		{
			DEBUG((EFI_D_ERROR, "VBVerifyImage failed with: %r\n", Status));
			return Status;
		}

		DEBUG((EFI_D_VERBOSE, "Boot State is : %d\n", BootState));
		switch (BootState)
		{
			case RED:
				DisplayVerifiedBootMenu(DISPLAY_MENU_RED);
				MicroSecondDelay(5000000);
				ShutdownDevice();
				break;
			case YELLOW:
				DisplayVerifiedBootMenu(DISPLAY_MENU_YELLOW);
				MicroSecondDelay(5000000);
				break;
			case ORANGE:
				DisplayVerifiedBootMenu(DISPLAY_MENU_ORANGE);
				MicroSecondDelay(5000000);
				break;
			default:
				break;
		}

		Status = VbIntf->VBSendRot(VbIntf);
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "Error sending Rot : %r\n", Status));
			return Status;
		}
	}

	KernelSize = ((boot_img_hdr*)(ImageBuffer))->kernel_size;
	RamdiskSize = ((boot_img_hdr*)(ImageBuffer))->ramdisk_size;
	SecondSize = ((boot_img_hdr*)(ImageBuffer))->second_size;
	PageSize = ((boot_img_hdr*)(ImageBuffer))->page_size;
	CmdLine = (UINT32*)&(((boot_img_hdr*)(ImageBuffer))->cmdline[0]);
	KernelSizeActual = ROUND_TO_PAGE(KernelSize, PageSize - 1);
	RamdiskSizeActual = ROUND_TO_PAGE(RamdiskSize, PageSize - 1);

	// Retrive Base Memory Address from Ram Partition Table
	Status = BaseMem(&BaseMemory);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Base memory not found!!! Status:%r\n", Status));
		return Status;
	}

	// These three regions should be reserved in memory map.
	KernelLoadAddr = (VOID *)(EFI_PHYSICAL_ADDRESS)(BaseMemory | PcdGet32(KernelLoadAddress));
	RamdiskLoadAddr = (VOID *)(EFI_PHYSICAL_ADDRESS)(BaseMemory | PcdGet32(RamdiskLoadAddress));
	DeviceTreeLoadAddr = (VOID *)(EFI_PHYSICAL_ADDRESS)(BaseMemory | PcdGet32(TagsAddress));

	if (is_gzip_package((ImageBuffer + PageSize), KernelSize))
	{
		// compressed kernel
		out_avai_len = DeviceTreeLoadAddr - KernelLoadAddr;

		DEBUG((EFI_D_INFO, "Decompressing kernel image start: %u ms\n", GetTimerCountms()));
		Status = decompress(
				(unsigned char *)(ImageBuffer + PageSize), //Read blob using BlockIo
				KernelSize,                                 //Blob size
				(unsigned char *)KernelLoadAddr,                             //Load address, allocated
				out_avai_len,                               //Allocated Size
				&DtbOffset, &out_len);

		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "Decompressing kernel image failed!!! Status=%r\n", Status));
			return Status;
		}

		DEBUG((EFI_D_INFO, "Decompressing kernel image done: %u ms\n", GetTimerCountms()));
		Kptr = KernelLoadAddr;
	} else {
		if (CHECK_ADD64(ImageBuffer, PageSize)) {
			DEBUG((EFI_D_ERROR, "Integer Overflow: in Kernel header fields addition\n"));
			return EFI_BAD_BUFFER_SIZE;
		}
		Kptr = ImageBuffer + PageSize;
	}
	if (Kptr->magic_64 != KERNEL64_HDR_MAGIC) {
		BootingWith32BitKernel = TRUE;
		KernelLoadAddr = (EFI_PHYSICAL_ADDRESS)(BaseMemory | PcdGet32(KernelLoadAddress32));
		if (CHECK_ADD64((VOID*)Kptr, DTB_OFFSET_LOCATION_IN_ARCH32_KERNEL_HDR)) {
			DEBUG((EFI_D_ERROR, "Integer Overflow: in DTB offset addition\n"));
			return EFI_BAD_BUFFER_SIZE;
		}
		CopyMem((VOID*)&DtbOffset, ((VOID*)Kptr + DTB_OFFSET_LOCATION_IN_ARCH32_KERNEL_HDR), sizeof(DtbOffset));
	}

	/*Finds out the location of device tree image and ramdisk image within the boot image
	 *Kernel, Ramdisk and Second sizes all rounded to page
	 *The offset and the LOCAL_ROUND_TO_PAGE function is written in a way that it is done the same in LK*/
	KernelSizeActual = LOCAL_ROUND_TO_PAGE (KernelSize, PageSize);
	RamdiskSizeActual = LOCAL_ROUND_TO_PAGE (RamdiskSize, PageSize);
	SecondSizeActual = LOCAL_ROUND_TO_PAGE (SecondSize, PageSize);

	/*Offsets are the location of the images within the boot image*/
	RamdiskOffset = PageSize + KernelSizeActual;
	DeviceTreeOffset = PageSize + KernelSizeActual + RamdiskSizeActual + SecondSizeActual;

	DEBUG((EFI_D_VERBOSE, "Kernel Size Actual: 0x%x\n", KernelSizeActual));
	DEBUG((EFI_D_VERBOSE, "Second Size Actual: 0x%x\n", SecondSizeActual));
	DEBUG((EFI_D_VERBOSE, "Ramdisk Size Actual: 0x%x\n", RamdiskSizeActual));
	DEBUG((EFI_D_VERBOSE, "Ramdisk Offset: 0x%x\n", RamdiskOffset));
	DEBUG((EFI_D_VERBOSE, "Device TreeOffset: 0x%x\n", DeviceTreeOffset));

	/* Populate board data required for dtb selection and command line */
	Status = BoardInit();
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Error finding board information: %r\n", Status));
		return Status;
	}

	/*Updates the command line from boot image, appends device serial no., baseband information, etc
	 *Called before ShutdownUefiBootServices as it uses some boot service functions*/
	CmdLine[BOOT_ARGS_SIZE-1] = '\0';

	Final_CmdLine = update_cmdline ((CHAR8*)CmdLine, pname, DevInfo, Recovery);
	if (!Final_CmdLine)
	{
		DEBUG((EFI_D_ERROR, "Error updating cmdline. Device Error\n"));
		return EFI_DEVICE_ERROR;
	}

	// appended device tree
	void *dtb;
	dtb = DeviceTreeAppended((void *) (ImageBuffer + PageSize), KernelSize, DtbOffset, (void *)DeviceTreeLoadAddr);
	if (!dtb) {
		DEBUG((EFI_D_ERROR, "Error: Appended Device Tree blob not found\n"));
		return EFI_NOT_FOUND;
	}

	Status = UpdateDeviceTree((VOID*)DeviceTreeLoadAddr , (CHAR8*)Final_CmdLine, (VOID *)RamdiskLoadAddr, RamdiskSize);
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR, "Device Tree update failed Status:%r\n", Status));
		return Status;
	}

	RamdiskEndAddr = BaseMemory | PcdGet32(RamdiskEndAddress);
	if (RamdiskEndAddr - RamdiskLoadAddr < RamdiskSize){
		DEBUG((EFI_D_ERROR, "Error: Ramdisk size is over the limit\n"));
		return EFI_BAD_BUFFER_SIZE;
	}
	CopyMem (RamdiskLoadAddr, ImageBuffer + RamdiskOffset, RamdiskSize);

	if (BootingWith32BitKernel) {
		if (CHECK_ADD64(KernelLoadAddr, KernelSizeActual)) {
			DEBUG((EFI_D_ERROR, "Integer Overflow: while Kernel image copy\n"));
			return EFI_BAD_BUFFER_SIZE;
		}
		if (KernelLoadAddr + KernelSizeActual > DeviceTreeLoadAddr) {
			DEBUG((EFI_D_ERROR, "Kernel size is over the limit\n"));
			return EFI_INVALID_PARAMETER;
		}
		CopyMem(KernelLoadAddr, ImageBuffer + PageSize, KernelSizeActual);
	}

	if (FixedPcdGetBool(EnablePartialGoods))
	{
		Status = UpdatePartialGoodsNode((VOID*)DeviceTreeLoadAddr);
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR, "Failed to update device tree for partial goods, Status=%r\n", Status));
			return Status;
		}
	}

	if (VerifiedBootEnbled()){
		DEBUG((EFI_D_INFO, "Sending Milestone Call\n"));
		Status = VbIntf->VBSendMilestone(VbIntf);
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_INFO, "Error sending milestone call to TZ\n"));
			return Status;
		}
	}

	/* Free the boot logo blt buffer before starting kernel */
	FreeBootLogoBltBuffer();
	if (BootingWith32BitKernel) {
		Status = gBS->LocateProtocol(&gQcomScmModeSwithProtocolGuid, NULL, (VOID**)&pQcomScmModeSwitchProtocol);
		if(EFI_ERROR(Status)) {
			DEBUG((EFI_D_ERROR,"ERROR: Unable to Locate Protocol handle for ScmModeSwicthProtocol Status=%r\n", Status));
			return Status;
		}
	}

	DEBUG((EFI_D_INFO, "\nShutting Down UEFI Boot Services: %u ms\n", GetTimerCountms()));
	/*Shut down UEFI boot services*/
	Status = ShutdownUefiBootServices ();
	if(EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR,"ERROR: Can not shutdown UEFI boot services. Status=0x%X\n", Status));
		goto Exit;
	}

	Status = PreparePlatformHardware ();
	if (Status != EFI_SUCCESS)
	{
		DEBUG((EFI_D_ERROR,"ERROR: Prepare Hardware Failed. Status:%r\n", Status));
		goto Exit;
	}

	BootStatsSetTimeStamp(BS_KERNEL_ENTRY);
	//
	// Start the Linux Kernel
	//
	if (BootingWith32BitKernel) {
		Status = SwitchTo32bitModeBooting((UINT64)KernelLoadAddr, (UINT64)DeviceTreeLoadAddr);
		return Status;
	}

	LinuxKernel = (LINUX_KERNEL)(UINTN)KernelLoadAddr;
	LinuxKernel ((UINTN)DeviceTreeLoadAddr, 0, 0, 0);

	// Kernel should never exit
	// After Life services are not provided

Exit:
	// Only be here if we fail to start Linux
	return EFI_NOT_STARTED;
}

STATIC BOOLEAN VerifiedBootEnbled()
{
#ifdef VERIFIED_BOOT
	return TRUE;
#endif
	return FALSE;
}
