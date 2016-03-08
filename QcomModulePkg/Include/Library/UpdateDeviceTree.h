/* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * * Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 *  with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __UPDATEDEVICETREE_H__
#define __UPDATEDEVICETREE_H__

#include <Uefi.h>
#include "libfdt.h"
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Protocol/EFIRamPartition.h>
#include <Library/UefiBootServicesTableLib.h>

#define DTB_MAX_SUBNODE                128
#define ARRAY_SIZE(a)                  sizeof(a)/sizeof(*a)

enum property_type
{
	DEVICE_TYPE = 1,
	STATUS_TYPE,
};

/* Sub node name, property pair */
struct SubNodeList
{
	CONST CHAR8 *SubNode;    /* Subnode name */
	CONST CHAR8 *Property;   /* Property name */
};

/* Look up table for partial goods */
struct PartialGoods
{
	UINT32 Val; /* Value for the defect */
	CONST CHAR8 *ParentNode; /* Parent Node name*/
	struct SubNodeList SubNode[DTB_MAX_SUBNODE]; /* Sub node name list*/
};

INTN dev_tree_add_mem_info(VOID* fdt, UINT32 offset, UINT32 addr, UINT32 size);

INTN dev_tree_add_mem_infoV64(VOID* fdt, UINT32 offset, UINT64 addr, UINT64 size);

EFI_STATUS UpdateDeviceTree(VOID* fdt, CONST CHAR8* cmdline, VOID* ramdisk,	UINT32 ramdisk_size);
EFI_STATUS UpdatePartialGoodsNode(VOID *fdt);
#endif
