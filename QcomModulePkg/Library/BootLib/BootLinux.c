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

#include <Library/DeviceInfo.h>
#include <Library/DrawUI.h>
#include <Library/PartialGoods.h>
#include <Library/PartitionTableUpdate.h>
#include <Library/ShutdownServices.h>
#include <Library/VerifiedBootMenu.h>
#include <Protocol/EFIMdtp.h>
#include <Protocol/EFIScmModeSwitch.h>
#include <libufdt_sysdeps.h>

#include "AutoGen.h"
#include "BootImage.h"
#include "BootLinux.h"
#include "BootStats.h"
#include "UpdateDeviceTree.h"
#include "libfdt.h"
#include <ufdt_overlay.h>

STATIC QCOM_SCM_MODE_SWITCH_PROTOCOL *pQcomScmModeSwitchProtocol = NULL;
STATIC BOOLEAN BootDevImage;

STATIC EFI_STATUS
SwitchTo32bitModeBooting (UINT64 KernelLoadAddr, UINT64 DeviceTreeLoadAddr)
{
  EFI_STATUS Status;
  EFI_HLOS_BOOT_ARGS HlosBootArgs;

  SetMem ((VOID *)&HlosBootArgs, sizeof (HlosBootArgs), 0);
  HlosBootArgs.el1_x2 = DeviceTreeLoadAddr;
  /* Write 0 into el1_x4 to switch to 32bit mode */
  HlosBootArgs.el1_x4 = 0;
  HlosBootArgs.el1_elr = KernelLoadAddr;
  Status = pQcomScmModeSwitchProtocol->SwitchTo32bitMode (HlosBootArgs);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "ERROR: Failed to switch to 32 bit mode.Status= %r\n",
            Status));
    return Status;
  }
  /*Return Unsupported if the execution ever reaches here*/
  return EFI_NOT_STARTED;
}

STATIC EFI_STATUS
CheckMDTPStatus (CHAR16 *PartitionName, BootInfo *Info)
{
  EFI_STATUS Status = EFI_SUCCESS;
  BOOLEAN MdtpActive = FALSE;
  CHAR8 StrPartition[MAX_GPT_NAME_SIZE];
  CHAR8 PartitionNameAscii[MAX_GPT_NAME_SIZE];
  UINT32 PartitionNameLen;
  QCOM_MDTP_PROTOCOL *MdtpProtocol;
  MDTP_VB_EXTERNAL_PARTITION ExternalPartition;

  SetMem ((VOID *)StrPartition, MAX_GPT_NAME_SIZE, 0);
  SetMem ((VOID *)PartitionNameAscii, MAX_GPT_NAME_SIZE, 0);

  if (FixedPcdGetBool (EnableMdtpSupport)) {
    Status = IsMdtpActive (&MdtpActive);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Failed to get activation state for MDTP, "
                           "Status=%r. Considering MDTP as active and "
                           "continuing \n",
              Status));

    if (Status != EFI_NOT_FOUND) {
      MdtpActive = TRUE;
    }
  }

    if (MdtpActive) {
      /* If MDTP is Active and Dm-Verity Mode is not Enforcing, Block */
      if (!IsEnforcing ()) {
        DEBUG ((EFI_D_ERROR,
                "ERROR: MDTP is active and verity mode is not enforcing \n"));
        return EFI_NOT_STARTED;
      }
      /* If MDTP is Active and Device is in unlocked State, Block */
      if (IsUnlocked ()) {
        DEBUG ((EFI_D_ERROR,
                "ERROR: MDTP is active and DEVICE is unlocked \n"));
        return EFI_NOT_STARTED;
      }
    }
  }

  UnicodeStrToAsciiStr (PartitionName, PartitionNameAscii);
  PartitionNameLen = AsciiStrLen (PartitionNameAscii);
  if (Info->MultiSlotBoot)
    PartitionNameLen -= (MAX_SLOT_SUFFIX_SZ - 1);
  AsciiStrnCpyS (StrPartition, MAX_GPT_NAME_SIZE, "/", AsciiStrLen ("/"));
  AsciiStrnCatS (StrPartition, MAX_GPT_NAME_SIZE, PartitionNameAscii,
                 PartitionNameLen);

  if (FixedPcdGetBool (EnableMdtpSupport)) {
    Status = gBS->LocateProtocol (&gQcomMdtpProtocolGuid, NULL,
                                  (VOID **)&MdtpProtocol);

    if (Status != EFI_NOT_FOUND) {
      if (EFI_ERROR (Status)) {
        DEBUG ((EFI_D_ERROR, "Failed in locating MDTP protocol, Status=%r\n",
                Status));
        return Status;
      }

      AsciiStrnCpyS (ExternalPartition.PartitionName, MAX_PARTITION_NAME_LEN,
                     StrPartition, AsciiStrLen (StrPartition));
      Status = MdtpProtocol->MdtpBootState (MdtpProtocol, &ExternalPartition);

      if (EFI_ERROR (Status)) {
        /* MdtpVerify should always handle errors internally, so when returned
         * back to the caller,
         * the return value is expected to be success only.
         * Therfore, we don't expect any error status here. */
        DEBUG ((EFI_D_ERROR, "MDTP verification failed, Status=%r\n", Status));
        return Status;
      }
    }

    else
      DEBUG (
          (EFI_D_ERROR, "Failed to locate MDTP protocol, Status=%r\n", Status));
  }

  return Status;
}

STATIC EFI_STATUS
DTBImgCheckAndAppendDT (BootInfo *Info,
                        BootParamlist *BootParamlistPtr,
                        UINT32 DtbOffset)
{
  VOID *SingleDtHdr = NULL;
  VOID *NextDtHdr = NULL;
  VOID *FinalDtbHdr = NULL;
  VOID *BoardDtb = NULL;
  VOID *SocDtb = NULL;
  VOID *Dtb;
  VOID *SocDtbHdr = NULL;
  BOOLEAN DtboCheckNeeded = FALSE;
  BOOLEAN DtboImgInvalid = FALSE;

  if (Info == NULL ||
      BootParamlistPtr == NULL) {
    DEBUG ((EFI_D_ERROR, "Invalid input parameters\n"));
    return EFI_INVALID_PARAMETER;
  }

  DtboImgInvalid = LoadAndValidateDtboImg (Info,
                                           &(BootParamlistPtr->DtboImgBuffer));

if (!DtboImgInvalid) {
    // appended device tree
    Dtb = DeviceTreeAppended ((VOID *)(BootParamlistPtr->ImageBuffer +
                             BootParamlistPtr->PageSize +
                             BootParamlistPtr->PatchedKernelHdrSize),
                             BootParamlistPtr->KernelSize,
                             DtbOffset,
                             (VOID *)BootParamlistPtr->DeviceTreeLoadAddr);
    if (!Dtb) {
      if (DtbOffset >= BootParamlistPtr->KernelSize) {
        DEBUG ((EFI_D_ERROR, "Dtb offset goes beyond the kernel size\n"));
        return EFI_BAD_BUFFER_SIZE;
      }
      SingleDtHdr = (BootParamlistPtr->ImageBuffer +
                     BootParamlistPtr->PageSize +
                     DtbOffset);

      if (!fdt_check_header (SingleDtHdr)) {
        if ((BootParamlistPtr->KernelSize - DtbOffset) <
            fdt_totalsize (SingleDtHdr)) {
          DEBUG ((EFI_D_ERROR, "Dtb offset goes beyond the kernel size\n"));
          return EFI_BAD_BUFFER_SIZE;
        }

        NextDtHdr =
          (VOID *)((uintptr_t)SingleDtHdr + fdt_totalsize (SingleDtHdr));
        if (!fdt_check_header (NextDtHdr)) {
          DEBUG ((EFI_D_VERBOSE, "Not the single appended DTB\n"));
          return EFI_NOT_FOUND;
        }

         DEBUG ((EFI_D_VERBOSE, "Single appended DTB found\n"));
         if (CHECK_ADD64 (BootParamlistPtr->DeviceTreeLoadAddr,
                                 fdt_totalsize (SingleDtHdr))) {
           DEBUG ((EFI_D_ERROR,
             "Integer Overflow: in single dtb header addition\n"));
         return EFI_BAD_BUFFER_SIZE;
         }

         gBS->CopyMem ((VOID *)BootParamlistPtr->DeviceTreeLoadAddr,
                       SingleDtHdr, fdt_totalsize (SingleDtHdr));
       } else {
         DEBUG ((EFI_D_ERROR, "Error: Appended Device Tree blob not found\n"));
         return EFI_NOT_FOUND;
       }
     }
   } else {
     /*It is the case of DTB overlay Get the Soc specific dtb */
      FinalDtbHdr = SocDtb =
      GetSocDtb ((VOID *)(BootParamlistPtr->ImageBuffer +
                 BootParamlistPtr->PageSize +
                 BootParamlistPtr->PatchedKernelHdrSize),
                 BootParamlistPtr->KernelSize,
                 DtbOffset,
                 (VOID *)BootParamlistPtr->DeviceTreeLoadAddr);
      if (!SocDtb) {
        DEBUG ((EFI_D_ERROR,
                    "Error: Appended Soc Device Tree blob not found\n"));
        return EFI_NOT_FOUND;
      }

      /*Check do we really need to gothrough DTBO or not*/
      DtboCheckNeeded = GetDtboNeeded ();
      if (DtboCheckNeeded == TRUE) {
        BoardDtb = GetBoardDtb (Info, BootParamlistPtr->DtboImgBuffer);
        if (!BoardDtb) {
          DEBUG ((EFI_D_ERROR, "Error: Board Dtbo blob not found\n"));
          return EFI_NOT_FOUND;
        }
        if (!pre_overlay_malloc ()) {
          DEBUG ((EFI_D_ERROR,
                  "Error: Unable to Allocate Pre Buffer for Overlay\n"));
          return EFI_OUT_OF_RESOURCES;
        }

        SocDtbHdr = ufdt_install_blob (SocDtb, fdt_totalsize (SocDtb));
        if (!SocDtbHdr) {
          DEBUG ((EFI_D_ERROR, "Error: Install blob failed\n"));
          return EFI_NOT_FOUND;
        }

        FinalDtbHdr = ufdt_apply_overlay (SocDtbHdr,
                                          fdt_totalsize (SocDtbHdr),
                                          BoardDtb,
                                          fdt_totalsize (BoardDtb));
        if (!FinalDtbHdr) {
          DEBUG ((EFI_D_ERROR, "ufdt apply overlay failed\n"));
          return EFI_NOT_FOUND;
        }
      }
      gBS->CopyMem ((VOID *)BootParamlistPtr->DeviceTreeLoadAddr, FinalDtbHdr,
                     fdt_totalsize (FinalDtbHdr));
      post_overlay_free ();
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
GZipPkgCheck (BootParamlist *BootParamlistPtr,
              UINT32 *DtbOffset,
              UINT64 *KernelLoadAddr,
              BOOLEAN *BootingWith32BitKernel)
{
  UINT32 OutLen = 0;
  UINT64 OutAvaiLen = 0;
  struct kernel64_hdr *Kptr = NULL;

  if (BootParamlistPtr == NULL ||
      DtbOffset == NULL ||
     BootingWith32BitKernel == NULL ||
      KernelLoadAddr  == NULL) {

    DEBUG ((EFI_D_ERROR, "Invalid input parameters\n"));
    return EFI_INVALID_PARAMETER;
  }

  if (is_gzip_package ((BootParamlistPtr->ImageBuffer +
                        BootParamlistPtr->PageSize),
                        BootParamlistPtr->KernelSize)) {

    OutAvaiLen = BootParamlistPtr->DeviceTreeLoadAddr -
                                     *KernelLoadAddr;

    if (OutAvaiLen > MAX_UINT32) {
      DEBUG ((EFI_D_ERROR,
              "Integer Oveflow: the length of decompressed data = %u\n",
      OutAvaiLen));
      return EFI_BAD_BUFFER_SIZE;
    }

    DEBUG ((EFI_D_INFO, "Decompressing kernel image start: %lu ms\n",
                         GetTimerCountms ()));
    if (decompress (
        (UINT8 *)(BootParamlistPtr->ImageBuffer +
        BootParamlistPtr->PageSize),      // Read blob using BlockIo
        BootParamlistPtr->KernelSize,    // Blob size
        (UINT8 *)*KernelLoadAddr,       // Load address, allocated
        (UINT32)OutAvaiLen,   // Allocated Size
        DtbOffset, &OutLen)) {
          DEBUG ((EFI_D_ERROR, "Decompressing kernel image failed!!!\n"));
          return RETURN_OUT_OF_RESOURCES;
    }

    if (OutLen <= sizeof (struct kernel64_hdr *)) {
      DEBUG ((EFI_D_ERROR,
              "Decompress kernel size is smaller than image header size\n"));
      return RETURN_OUT_OF_RESOURCES;
    }

    DEBUG ((EFI_D_INFO, "Decompressing kernel image done: %lu ms\n",
                         GetTimerCountms ()));

    Kptr = (struct kernel64_hdr *)*KernelLoadAddr;
  } else {
    Kptr = (struct kernel64_hdr *)(BootParamlistPtr->ImageBuffer
                         + BootParamlistPtr->PageSize);
    DEBUG ((EFI_D_INFO, "Uncompressed kernel in use\n"));
    /* Patch kernel support only for 64-bit */
    if (!AsciiStrnCmp ((char*)(BootParamlistPtr->ImageBuffer
                 + BootParamlistPtr->PageSize), PATCHED_KERNEL_MAGIC,
                 sizeof (PATCHED_KERNEL_MAGIC) - 1)) {
      DEBUG ((EFI_D_VERBOSE, "Patched kernel detected\n"));

      /* The size of the kernel is stored at start of kernel image + 16
       * The dtb would start just after the kernel */
      gBS->CopyMem ((VOID *)DtbOffset, (VOID *) (BootParamlistPtr->ImageBuffer
                 + BootParamlistPtr->PageSize + sizeof (PATCHED_KERNEL_MAGIC)
                 - 1), sizeof (*DtbOffset));

      BootParamlistPtr->PatchedKernelHdrSize = PATCHED_KERNEL_HEADER_SIZE;
      Kptr = (struct kernel64_hdr *)((VOID *)Kptr +
                 BootParamlistPtr->PatchedKernelHdrSize);
      gBS->CopyMem ((VOID *)*KernelLoadAddr, (VOID *)Kptr,
                 BootParamlistPtr->KernelSize);
    }

    if (Kptr->magic_64 != KERNEL64_HDR_MAGIC) {
      *KernelLoadAddr =
      (EFI_PHYSICAL_ADDRESS) (BootParamlistPtr->BaseMemory |
      PcdGet32 (KernelLoadAddress32));
      if (BootParamlistPtr->KernelSize <=
          DTB_OFFSET_LOCATION_IN_ARCH32_KERNEL_HDR) {
        DEBUG ((EFI_D_ERROR, "DTB offset goes beyond kernel size.\n"));
        return EFI_BAD_BUFFER_SIZE;
      }
      gBS->CopyMem ((VOID *)DtbOffset,
           ((VOID *)Kptr + DTB_OFFSET_LOCATION_IN_ARCH32_KERNEL_HDR),
           sizeof (DtbOffset));
    }
  }

  if (Kptr->magic_64 != KERNEL64_HDR_MAGIC) {
    *BootingWith32BitKernel = TRUE;
  } else {
    if ((*KernelLoadAddr + Kptr->ImageSize) >=
         BootParamlistPtr->DeviceTreeLoadAddr) {
      DEBUG ((EFI_D_ERROR,
        "Dtb header can get corrupted due to runtime kernel size\n"));
      return EFI_BAD_BUFFER_SIZE;
    }
  }

  return EFI_SUCCESS;
}

STATIC EFI_STATUS
LoadAddrAndDTUpdate (BootParamlist *BootParamlistPtr)
{
  EFI_STATUS Status;
  UINT64 RamdiskEndAddr = 0;

  if (BootParamlistPtr == NULL) {
    DEBUG ((EFI_D_ERROR, "Invalid input parameters\n"));
    return EFI_INVALID_PARAMETER;

  }

  RamdiskEndAddr =
    (EFI_PHYSICAL_ADDRESS) (BootParamlistPtr->BaseMemory |
                              PcdGet32 (RamdiskEndAddress));
  if (RamdiskEndAddr - BootParamlistPtr->RamdiskLoadAddr <
                       BootParamlistPtr->RamdiskSize) {
    DEBUG ((EFI_D_ERROR, "Error: Ramdisk size is over the limit\n"));
    return EFI_BAD_BUFFER_SIZE;
  }

  if (CHECK_ADD64 ((UINT64)BootParamlistPtr->ImageBuffer,
      BootParamlistPtr->RamdiskOffset)) {
    DEBUG ((EFI_D_ERROR, "Integer Oveflow: ImageBuffer=%u, "
                         "RamdiskOffset=%u\n",
                         BootParamlistPtr->ImageBuffer,
                         BootParamlistPtr->RamdiskOffset));
    return EFI_BAD_BUFFER_SIZE;
  }

  Status = UpdateDeviceTree ((VOID *)BootParamlistPtr->DeviceTreeLoadAddr,
                             BootParamlistPtr->FinalCmdLine,
                             (VOID *)BootParamlistPtr->RamdiskLoadAddr,
                             BootParamlistPtr->RamdiskSize,
                             BootParamlistPtr->BootingWith32BitKernel);
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Device Tree update failed Status:%r\n", Status));
    return Status;
  }

  gBS->CopyMem ((CHAR8 *)BootParamlistPtr->RamdiskLoadAddr,
                BootParamlistPtr->ImageBuffer +
                BootParamlistPtr->RamdiskOffset,
                BootParamlistPtr->RamdiskSize);

  if (BootParamlistPtr->BootingWith32BitKernel) {
    if (CHECK_ADD64 (BootParamlistPtr->KernelLoadAddr,
        BootParamlistPtr->KernelSizeActual)) {
      DEBUG ((EFI_D_ERROR, "Integer Overflow: while Kernel image copy\n"));
      return EFI_BAD_BUFFER_SIZE;
    }
    if (BootParamlistPtr->KernelLoadAddr +
        BootParamlistPtr->KernelSizeActual >
        BootParamlistPtr->DeviceTreeLoadAddr) {
      DEBUG ((EFI_D_ERROR, "Kernel size is over the limit\n"));
      return EFI_INVALID_PARAMETER;
    }
    gBS->CopyMem ((CHAR8 *)BootParamlistPtr->KernelLoadAddr,
                  BootParamlistPtr->ImageBuffer +
                  BootParamlistPtr->PageSize,
                  BootParamlistPtr->KernelSizeActual);
  }

  if (FixedPcdGetBool (EnablePartialGoods)) {
    Status = UpdatePartialGoodsNode
                ((VOID *)BootParamlistPtr->DeviceTreeLoadAddr);
    if (Status != EFI_SUCCESS) {
      DEBUG ((EFI_D_ERROR,
        "Failed to update device tree for partial goods, Status=%r\n",
           Status));
      return Status;
    }
  }
  return EFI_SUCCESS;
}

EFI_STATUS
BootLinux (BootInfo *Info)
{

  EFI_STATUS Status;
  UINTN ImageSize = 0;
  CHAR16 *PartitionName = NULL;
  BOOLEAN Recovery = FALSE;
  BOOLEAN AlarmBoot = FALSE;

  LINUX_KERNEL LinuxKernel;
  LINUX_KERNEL32 LinuxKernel32;
  UINT32 RamdiskSizeActual = 0;
  UINT32 SecondSizeActual = 0;

  /*Boot Image header information variables*/
  UINT32 DtbOffset = 0;
  CHAR8 FfbmStr[FFBM_MODE_BUF_SIZE] = {'\0'};
  BOOLEAN IsModeSwitch = FALSE;

  BootParamlist BootParamlistPtr = {0};

  if (Info == NULL) {
    DEBUG ((EFI_D_ERROR, "BootLinux: invalid parameter Info\n"));
    return EFI_INVALID_PARAMETER;
  }

  PartitionName = Info->Pname;
  Recovery = Info->BootIntoRecovery;
  AlarmBoot = Info->BootReasonAlarm;

  if (!StrnCmp (PartitionName, (CONST CHAR16 *)L"boot",
                StrLen ((CONST CHAR16 *)L"boot"))) {
    Status = GetFfbmCommand (FfbmStr, FFBM_MODE_BUF_SIZE);
    if (Status != EFI_SUCCESS) {
      DEBUG ((EFI_D_INFO, "No Ffbm cookie found, ignore: %r\n", Status));
      FfbmStr[0] = '\0';
    }
  }

  Status = GetImage (Info,
                     &BootParamlistPtr.ImageBuffer,
                     &ImageSize,
                     (!Info->MultiSlotBoot &&
                      Recovery)? "recovery" : "boot");
  if (Status != EFI_SUCCESS ||
      BootParamlistPtr.ImageBuffer == NULL ||
      ImageSize <= 0) {
    DEBUG ((EFI_D_ERROR, "BootLinux: Get%aImage failed!\n",
            (!Info->MultiSlotBoot &&
             Recovery)? "Recovery" : "Boot"));
    return EFI_NOT_STARTED;
  }
  /* Find if MDTP is enabled and Active */

  Status = CheckMDTPStatus (PartitionName, Info);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  BootParamlistPtr.KernelSize =
               ((boot_img_hdr *)(BootParamlistPtr.ImageBuffer))->kernel_size;
  BootParamlistPtr.RamdiskSize =
               ((boot_img_hdr *)(BootParamlistPtr.ImageBuffer))->ramdisk_size;
  BootParamlistPtr.SecondSize =
               ((boot_img_hdr *)(BootParamlistPtr.ImageBuffer))->second_size;
  BootParamlistPtr.PageSize =
               ((boot_img_hdr *)(BootParamlistPtr.ImageBuffer))->page_size;
  BootParamlistPtr.CmdLine = (CHAR8 *)&(((boot_img_hdr *)
                             (BootParamlistPtr.ImageBuffer))->cmdline[0]);

  // Retrive Base Memory Address from Ram Partition Table
  Status = BaseMem (&BootParamlistPtr.BaseMemory);
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Base memory not found!!! Status:%r\n", Status));
    return Status;
  }

  // These three regions should be reserved in memory map.
  BootParamlistPtr.KernelLoadAddr =
      (EFI_PHYSICAL_ADDRESS) (BootParamlistPtr.BaseMemory |
                              PcdGet32 (KernelLoadAddress));
  BootParamlistPtr.RamdiskLoadAddr =
      (EFI_PHYSICAL_ADDRESS) (BootParamlistPtr.BaseMemory |
                              PcdGet32 (RamdiskLoadAddress));
  BootParamlistPtr.DeviceTreeLoadAddr =
      (EFI_PHYSICAL_ADDRESS) (BootParamlistPtr.BaseMemory |
                              PcdGet32 (TagsAddress));

  Status = GZipPkgCheck (&BootParamlistPtr, &DtbOffset,
                         &BootParamlistPtr.KernelLoadAddr,
                         &BootParamlistPtr.BootingWith32BitKernel);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  /*Finds out the location of device tree image and ramdisk image within the
   *boot image
   *Kernel, Ramdisk and Second sizes all rounded to page
   *The offset and the LOCAL_ROUND_TO_PAGE function is written in a way that it
   *is done the same in LK*/
  BootParamlistPtr.KernelSizeActual = LOCAL_ROUND_TO_PAGE (
                                          BootParamlistPtr.KernelSize,
                                          BootParamlistPtr.PageSize);
  RamdiskSizeActual = LOCAL_ROUND_TO_PAGE (BootParamlistPtr.RamdiskSize,
                                           BootParamlistPtr.PageSize);
  SecondSizeActual = LOCAL_ROUND_TO_PAGE (BootParamlistPtr.SecondSize,
                                          BootParamlistPtr.PageSize);

  /*Offsets are the location of the images within the boot image*/

  BootParamlistPtr.RamdiskOffset = ADD_OF (BootParamlistPtr.PageSize,
                           BootParamlistPtr.KernelSizeActual);
  if (!BootParamlistPtr.RamdiskOffset) {
    DEBUG ((EFI_D_ERROR, "Integer Oveflow: PageSize=%u, KernelSizeActual=%u\n",
           BootParamlistPtr.PageSize, BootParamlistPtr.KernelSizeActual));
    return EFI_BAD_BUFFER_SIZE;
  }

  DEBUG ((EFI_D_VERBOSE, "Kernel Load Address: 0x%x\n",
                                        BootParamlistPtr.KernelLoadAddr));
  DEBUG ((EFI_D_VERBOSE, "Kernel Size Actual: 0x%x\n",
                                      BootParamlistPtr.KernelSizeActual));
  DEBUG ((EFI_D_VERBOSE, "Second Size Actual: 0x%x\n", SecondSizeActual));
  DEBUG ((EFI_D_VERBOSE, "Ramdisk Load Address: 0x%x\n",
                                       BootParamlistPtr.RamdiskLoadAddr));
  DEBUG ((EFI_D_VERBOSE, "Ramdisk Size Actual: 0x%x\n", RamdiskSizeActual));
  DEBUG ((EFI_D_VERBOSE, "Ramdisk Offset: 0x%x\n",
                                       BootParamlistPtr.RamdiskOffset));
  DEBUG (
      (EFI_D_VERBOSE, "Device Tree Load Address: 0x%x\n",
                             BootParamlistPtr.DeviceTreeLoadAddr));

  /*Updates the command line from boot image, appends device serial no.,
   *baseband information, etc
   *Called before ShutdownUefiBootServices as it uses some boot service
   *functions*/
  BootParamlistPtr.CmdLine[BOOT_ARGS_SIZE - 1] = '\0';

  if (AsciiStrStr (BootParamlistPtr.CmdLine, "root=")) {
    BootDevImage = TRUE;
  }

  Status = UpdateCmdLine (BootParamlistPtr.CmdLine, FfbmStr, Recovery,
                   AlarmBoot, Info->VBCmdLine, &BootParamlistPtr.FinalCmdLine);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Error updating cmdline. Device Error %r\n", Status));
    return Status;
  }

  Status = DTBImgCheckAndAppendDT (Info, &BootParamlistPtr,
                                   DtbOffset);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  Status = LoadAddrAndDTUpdate (&BootParamlistPtr);
  if (Status != EFI_SUCCESS) {
       return Status;
  }

  FreeVerifiedBootResource (Info);

  /* Free the boot logo blt buffer before starting kernel */
  FreeBootLogoBltBuffer ();
  if (BootParamlistPtr.BootingWith32BitKernel) {
    Status = gBS->LocateProtocol (&gQcomScmModeSwithProtocolGuid, NULL,
                                  (VOID **)&pQcomScmModeSwitchProtocol);
    if (!EFI_ERROR (Status))
      IsModeSwitch = TRUE;
  }

  DEBUG ((EFI_D_INFO, "\nShutting Down UEFI Boot Services: %lu ms\n",
          GetTimerCountms ()));
  /*Shut down UEFI boot services*/
  Status = ShutdownUefiBootServices ();
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR,
            "ERROR: Can not shutdown UEFI boot services. Status=0x%X\n",
            Status));
    goto Exit;
  }

  PreparePlatformHardware ();

  BootStatsSetTimeStamp (BS_KERNEL_ENTRY);
  //
  // Start the Linux Kernel
  //

  if (BootParamlistPtr.BootingWith32BitKernel) {
    if (IsModeSwitch) {
      Status = SwitchTo32bitModeBooting (
                     (UINT64)BootParamlistPtr.KernelLoadAddr,
                     (UINT64)BootParamlistPtr.DeviceTreeLoadAddr);
      return Status;
    }

    // Booting into 32 bit kernel.
    LinuxKernel32 = (LINUX_KERNEL32) (UINT64)BootParamlistPtr.KernelLoadAddr;
    LinuxKernel32 (0, 0, (UINTN)BootParamlistPtr.DeviceTreeLoadAddr);

    // Should never reach here. After life support is not available
    DEBUG ((EFI_D_ERROR, "After Life support not available\n"));
    goto Exit;
  }

  LinuxKernel = (LINUX_KERNEL) (UINT64)BootParamlistPtr.KernelLoadAddr;
  LinuxKernel ((UINT64)BootParamlistPtr.DeviceTreeLoadAddr, 0, 0, 0);

// Kernel should never exit
// After Life services are not provided

Exit:
  // Only be here if we fail to start Linux
  return EFI_NOT_STARTED;
}

/**
  Check image header
  @param[in]  ImageHdrBuffer  Supplies the address where a pointer to the image
header buffer.
  @param[in]  ImageHdrSize    Supplies the address where a pointer to the image
header size.
  @param[out] ImageSizeActual The Pointer for image actual size.
  @param[out] PageSize        The Pointer for page size..
  @retval     EFI_SUCCESS     Check image header successfully.
  @retval     other           Failed to check image header.
**/
EFI_STATUS
CheckImageHeader (VOID *ImageHdrBuffer,
                  UINT32 ImageHdrSize,
                  UINT32 *ImageSizeActual,
                  UINT32 *PageSize)
{
  EFI_STATUS Status = EFI_SUCCESS;
  UINT32 KernelSizeActual = 0;
  UINT32 DtSizeActual = 0;
  UINT32 RamdiskSizeActual = 0;

  // Boot Image header information variables
  UINT32 KernelSize = 0;
  UINT32 RamdiskSize = 0;
  UINT32 SecondSize = 0;
  UINT32 DeviceTreeSize = 0;
  UINT32 tempImgSize = 0;

  if (CompareMem ((void *)((boot_img_hdr *)(ImageHdrBuffer))->magic, BOOT_MAGIC,
                  BOOT_MAGIC_SIZE)) {
    DEBUG ((EFI_D_ERROR, "Invalid boot image header\n"));
    return EFI_NO_MEDIA;
  }

  KernelSize = ((boot_img_hdr *)(ImageHdrBuffer))->kernel_size;
  RamdiskSize = ((boot_img_hdr *)(ImageHdrBuffer))->ramdisk_size;
  SecondSize = ((boot_img_hdr *)(ImageHdrBuffer))->second_size;
  *PageSize = ((boot_img_hdr *)(ImageHdrBuffer))->page_size;
  DeviceTreeSize = ((boot_img_hdr *)(ImageHdrBuffer))->dt_size;

  if (!KernelSize || !*PageSize) {
    DEBUG ((EFI_D_ERROR, "Invalid image Sizes\n"));
    DEBUG (
        (EFI_D_ERROR, "KernelSize: %u, PageSize=%u\n", KernelSize, *PageSize));
    return EFI_BAD_BUFFER_SIZE;
  }

  if ((*PageSize != ImageHdrSize) && (*PageSize > BOOT_IMG_MAX_PAGE_SIZE)) {
    DEBUG ((EFI_D_ERROR, "Invalid image pagesize\n"));
    DEBUG ((EFI_D_ERROR, "MAX: %u. PageSize: %u and ImageHdrSize: %u\n",
            BOOT_IMG_MAX_PAGE_SIZE, *PageSize, ImageHdrSize));
    return EFI_BAD_BUFFER_SIZE;
  }

  KernelSizeActual = ROUND_TO_PAGE (KernelSize, *PageSize - 1);
  if (!KernelSizeActual) {
    DEBUG ((EFI_D_ERROR, "Integer Oveflow: Kernel Size = %u\n", KernelSize));
    return EFI_BAD_BUFFER_SIZE;
  }

  RamdiskSizeActual = ROUND_TO_PAGE (RamdiskSize, *PageSize - 1);
  if (RamdiskSize && !RamdiskSizeActual) {
    DEBUG ((EFI_D_ERROR, "Integer Oveflow: Ramdisk Size = %u\n", RamdiskSize));
    return EFI_BAD_BUFFER_SIZE;
  }

  DtSizeActual = ROUND_TO_PAGE (DeviceTreeSize, *PageSize - 1);
  if (DeviceTreeSize && !(DtSizeActual)) {
    DEBUG (
        (EFI_D_ERROR, "Integer Oveflow: Device Tree = %u\n", DeviceTreeSize));
    return EFI_BAD_BUFFER_SIZE;
  }

  *ImageSizeActual = ADD_OF (*PageSize, KernelSizeActual);
  if (!*ImageSizeActual) {
    DEBUG ((EFI_D_ERROR, "Integer Oveflow: Actual Kernel size = %u\n",
            KernelSizeActual));
    return EFI_BAD_BUFFER_SIZE;
  }

  tempImgSize = *ImageSizeActual;
  *ImageSizeActual = ADD_OF (*ImageSizeActual, RamdiskSizeActual);
  if (!*ImageSizeActual) {
    DEBUG ((EFI_D_ERROR,
            "Integer Oveflow: ImgSizeActual=%u, RamdiskActual=%u\n",
            tempImgSize, RamdiskSizeActual));
    return EFI_BAD_BUFFER_SIZE;
  }

  tempImgSize = *ImageSizeActual;
  *ImageSizeActual = ADD_OF (*ImageSizeActual, DtSizeActual);
  if (!*ImageSizeActual) {
    DEBUG ((EFI_D_ERROR, "Integer Oveflow: ImgSizeActual=%u, DtSizeActual=%u\n",
            tempImgSize, DtSizeActual));
    return EFI_BAD_BUFFER_SIZE;
  }

  DEBUG ((EFI_D_VERBOSE, "Boot Image Header Info...\n"));
  DEBUG ((EFI_D_VERBOSE, "Kernel Size 1            : 0x%x\n", KernelSize));
  DEBUG ((EFI_D_VERBOSE, "Kernel Size 2            : 0x%x\n", SecondSize));
  DEBUG ((EFI_D_VERBOSE, "Device Tree Size         : 0x%x\n", DeviceTreeSize));
  DEBUG ((EFI_D_VERBOSE, "Ramdisk Size             : 0x%x\n", RamdiskSize));
  DEBUG ((EFI_D_VERBOSE, "Device Tree Size         : 0x%x\n", DeviceTreeSize));

  return Status;
}

/**
  Load image from partition
  @param[in]  Pname           Partition name.
  @param[out] ImageBuffer     Supplies the address where a pointer to the image
buffer.
  @param[out] ImageSizeActual The Pointer for image actual size.
  @retval     EFI_SUCCESS     Load image from partition successfully.
  @retval     other           Failed to Load image from partition.
**/
EFI_STATUS
LoadImage (CHAR16 *Pname, VOID **ImageBuffer, UINT32 *ImageSizeActual)
{
  EFI_STATUS Status = EFI_SUCCESS;
  VOID *ImageHdrBuffer;
  UINT32 ImageHdrSize = 0;
  UINT32 ImageSize = 0;
  UINT32 PageSize = 0;
  UINT32 tempImgSize = 0;

  // Check for invalid ImageBuffer
  if (ImageBuffer == NULL)
    return EFI_INVALID_PARAMETER;
  else
    *ImageBuffer = NULL;

  // Setup page size information for nv storage
  GetPageSize (&ImageHdrSize);

  if (!ADD_OF (ImageHdrSize, ALIGNMENT_MASK_4KB - 1)) {
    DEBUG ((EFI_D_ERROR, "Integer Oveflow: in ALIGNMENT_MASK_4KB addition\n"));
    return EFI_BAD_BUFFER_SIZE;
  }

  ImageHdrBuffer =
      AllocatePages (ALIGN_PAGES (ImageHdrSize, ALIGNMENT_MASK_4KB));
  if (!ImageHdrBuffer) {
    DEBUG ((EFI_D_ERROR, "Failed to allocate for Boot image Hdr\n"));
    return EFI_BAD_BUFFER_SIZE;
  }

  Status = LoadImageFromPartition (ImageHdrBuffer, &ImageHdrSize, Pname);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  // Add check for boot image header and kernel page size
  // ensure kernel command line is terminated
  Status = CheckImageHeader (ImageHdrBuffer, ImageHdrSize, ImageSizeActual,
                             &PageSize);
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Invalid boot image header:%r\n", Status));
    return Status;
  }

  tempImgSize = *ImageSizeActual;
  ImageSize =
      ADD_OF (ROUND_TO_PAGE (*ImageSizeActual, (PageSize - 1)), PageSize);
  if (!ImageSize) {
    DEBUG ((EFI_D_ERROR, "Integer Oveflow: ImgSize=%u\n", tempImgSize));
    return EFI_BAD_BUFFER_SIZE;
  }

  if (!ADD_OF (ImageSize, ALIGNMENT_MASK_4KB - 1)) {
    DEBUG ((EFI_D_ERROR, "Integer Oveflow: in ALIGNMENT_MASK_4KB addition\n"));
    return EFI_BAD_BUFFER_SIZE;
  }

  *ImageBuffer = AllocatePages (ALIGN_PAGES (ImageSize, ALIGNMENT_MASK_4KB));
  if (!*ImageBuffer) {
    DEBUG ((EFI_D_ERROR, "No resources available for ImageBuffer\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  BootStatsSetTimeStamp (BS_KERNEL_LOAD_START);
  Status = LoadImageFromPartition (*ImageBuffer, &ImageSize, Pname);
  BootStatsSetTimeStamp (BS_KERNEL_LOAD_DONE);

  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Failed Kernel Size   : 0x%x\n", ImageSize));
    return Status;
  }

  return Status;
}

EFI_STATUS
GetImage (CONST BootInfo *Info,
          VOID **ImageBuffer,
          UINTN *ImageSize,
          CHAR8 *ImageName)
{
  if (Info == NULL || ImageBuffer == NULL || ImageSize == NULL ||
      ImageName == NULL) {
    DEBUG ((EFI_D_ERROR, "GetImage: invalid parameters\n"));
    return EFI_INVALID_PARAMETER;
  }

  for (UINTN LoadedIndex = 0; LoadedIndex < Info->NumLoadedImages;
       LoadedIndex++) {
    if (!AsciiStrnCmp (Info->Images[LoadedIndex].Name, ImageName,
                       AsciiStrLen (ImageName))) {
      *ImageBuffer = Info->Images[LoadedIndex].ImageBuffer;
      *ImageSize = Info->Images[LoadedIndex].ImageSize;
      return EFI_SUCCESS;
    }
  }
  return EFI_NOT_FOUND;
}

/* Return Build variant */
#ifdef USER_BUILD_VARIANT
BOOLEAN TargetBuildVariantUser (VOID)
{
  return TRUE;
}
#else
BOOLEAN TargetBuildVariantUser (VOID)
{
  return FALSE;
}
#endif

#ifdef ENABLE_LE_VARIANT
BOOLEAN IsLEVariant (VOID)
{
  return TRUE;
}
#else
BOOLEAN IsLEVariant (VOID)
{
  return FALSE;
}
#endif

VOID
ResetBootDevImage (VOID)
{
  BootDevImage = FALSE;
}

VOID
SetBootDevImage (VOID)
{
  BootDevImage = TRUE;
}

BOOLEAN IsBootDevImage (VOID)
{
  return BootDevImage;
}
