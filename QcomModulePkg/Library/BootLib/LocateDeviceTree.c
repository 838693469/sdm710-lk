/* Copyright (c) 2012-2017, The Linux Foundation. All rights reserved.
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

#include "LocateDeviceTree.h"
#include "UpdateDeviceTree.h"
#include <Library/Rtic.h>
#include <Library/PartitionTableUpdate.h>
#include <Library/BootLinux.h>
#include <Library/Board.h>
// Variables to initialize board data

STATIC int platform_dt_absolute_match(struct dt_entry *cur_dt_entry, struct dt_entry_node *dt_list);
STATIC struct dt_entry *platform_dt_match_best(struct dt_entry_node *dt_list);

STATIC UINT32 BestSocDtMatch;
STATIC UINT32 BestBoardDtMatch;
STATIC BOOLEAN DtboNeed = TRUE;

BOOLEAN GetDtboNeeded ()
{
	return DtboNeed;
}
/* Add function to allocate dt entry list, used for recording
 *  the entry which conform to platform_dt_absolute_match()
 */
static struct dt_entry_node *dt_entry_list_init(VOID)
{
	struct dt_entry_node *dt_node_member = NULL;

	dt_node_member = (struct dt_entry_node *)
		AllocatePool(sizeof(struct dt_entry_node));

	if (!dt_node_member) {
		DEBUG((EFI_D_ERROR, "Failed to allocate memory for dt_node_member\n"));
		return NULL;
	}

	list_clear_node(&dt_node_member->node);
	dt_node_member->dt_entry_m = (struct dt_entry *)
		AllocatePool(sizeof(struct dt_entry));
	if (!dt_node_member->dt_entry_m) {
		DEBUG((EFI_D_ERROR, "Failed to allocate memory for dt_node_member->dt_entry_m\n"));
		return NULL;
	}

	memset(dt_node_member->dt_entry_m ,0 ,sizeof(struct dt_entry));
	return dt_node_member;
}

static VOID insert_dt_entry_in_queue(struct dt_entry_node *dt_list, struct dt_entry_node *dt_node_member)
{
	list_add_tail(&dt_list->node, &dt_node_member->node);
}

static VOID dt_entry_list_delete(struct dt_entry_node *dt_node_member)
{
	if (list_in_list(&dt_node_member->node)) {
		list_delete(&dt_node_member->node);
		FreePool(dt_node_member->dt_entry_m);
		FreePool(dt_node_member);
	}
}

static BOOLEAN DeviceTreeCompatible(VOID *dtb, UINT32 dtb_size, struct dt_entry_node *dtb_list)
{
	int root_offset;
	const VOID *prop = NULL;
	const char *plat_prop = NULL;
	const char *board_prop = NULL;
	const char *pmic_prop = NULL;
	char *model = NULL;
	struct dt_entry *dt_entry_array = NULL;
	struct board_id *board_data = NULL;
	struct plat_id *platform_data = NULL;
	struct pmic_id *pmic_data = NULL;
	int len;
	int len_board_id;
	int len_plat_id;
	int min_plat_id_len = 0;
	int len_pmic_id;
	UINT32 dtb_ver;
	UINT32 num_entries = 0;
	UINT32 i, j, k, n;
	UINT32 msm_data_count;
	UINT32 board_data_count;
	UINT32 pmic_data_count;


	root_offset = fdt_path_offset(dtb, "/");
	if (root_offset < 0)
		return FALSE;

	prop = fdt_getprop(dtb, root_offset, "model", &len);
	if (prop && len > 0) {
		model = (char *) AllocatePool(sizeof(char) * len);
		if (!model) {
			DEBUG((EFI_D_ERROR, "Failed to allocate memory for model\n"));
			return FALSE;
		}
		AsciiStrnCpyS(model, (sizeof(CHAR8)* len), prop, len);
	} else {
		DEBUG ((EFI_D_ERROR, "model does not exist in device tree\n"));
	}
	/* Find the pmic-id prop from DTB , if pmic-id is present then
	 * the DTB is version 3, otherwise find the board-id prop from DTB ,
	 * if board-id is present then the DTB is version 2 */
	pmic_prop = (const char *)fdt_getprop(dtb, root_offset, "qcom,pmic-id", &len_pmic_id);
	board_prop = (const char *)fdt_getprop(dtb, root_offset, "qcom,board-id", &len_board_id);
	if (pmic_prop && (len_pmic_id > 0) && board_prop && (len_board_id > 0)) {
		if ((len_pmic_id % PMIC_ID_SIZE) || (len_board_id % BOARD_ID_SIZE))
		{
			DEBUG ((EFI_D_ERROR, "qcom,pmic-id (%d) or qcom,board-id(%d) in device tree is not a multiple of (%d %d)\n", len_pmic_id, len_board_id, PMIC_ID_SIZE, BOARD_ID_SIZE));
			return FALSE;
		}
		dtb_ver = DEV_TREE_VERSION_V3;
		min_plat_id_len = PLAT_ID_SIZE;
	} else if (board_prop && len_board_id > 0) {
		if (len_board_id % BOARD_ID_SIZE)
		{
			DEBUG ((EFI_D_ERROR, "qcom,pmic-id (%d) in device tree is not a multiple of (%d)\n", len_board_id, BOARD_ID_SIZE));
			return FALSE;
		}
		dtb_ver = DEV_TREE_VERSION_V2;
		min_plat_id_len = PLAT_ID_SIZE;
	} else {
		dtb_ver = DEV_TREE_VERSION_V1;
		min_plat_id_len = DT_ENTRY_V1_SIZE;
	}

	/* Get the msm-id prop from DTB */
	plat_prop = (const char *)fdt_getprop(dtb, root_offset, "qcom,msm-id", &len_plat_id);
	if (!plat_prop || len_plat_id <= 0) {
		DEBUG ((EFI_D_VERBOSE, "qcom,msm-id entry not found\n"));
		return FALSE;
	} else if (len_plat_id % min_plat_id_len) {
		DEBUG ((EFI_D_ERROR, "qcom, msm-id in device tree is (%d) not a multiple of (%d)\n",
					len_plat_id, min_plat_id_len));
		return FALSE;
	}
	if (dtb_ver == DEV_TREE_VERSION_V2 || dtb_ver == DEV_TREE_VERSION_V3) {
		board_data_count = (len_board_id / BOARD_ID_SIZE);
		msm_data_count = (len_plat_id / PLAT_ID_SIZE);
		/* If dtb version is v2.0, the pmic_data_count will be <= 0 */
		pmic_data_count = (len_pmic_id / PMIC_ID_SIZE);

		/* If we are using dtb v3.0, then we have split board, msm & pmic data in the DTB
		 *  If we are using dtb v2.0, then we have split board & msmdata in the DTB
		 */
		board_data = (struct board_id *) AllocatePool(sizeof(struct board_id) * (len_board_id / BOARD_ID_SIZE));
		if (!board_data) {
			DEBUG((EFI_D_ERROR, "Failed to allocate memory for board_data\n"));
			return FALSE;
		}

		platform_data = (struct plat_id *) AllocatePool(sizeof(struct plat_id) * (len_plat_id / PLAT_ID_SIZE));
		if (!platform_data) {
			DEBUG((EFI_D_ERROR, "Failed to allocate memory for platform_data\n"));
			return FALSE;
		}
		if (dtb_ver == DEV_TREE_VERSION_V3) {
			pmic_data = (struct pmic_id *) AllocatePool(sizeof(struct pmic_id) * (len_pmic_id / PMIC_ID_SIZE));
			if (!pmic_data) {
				DEBUG((EFI_D_ERROR, "Failed to allocate memory for pmic_data\n"));
				return FALSE;
			}
		}
		i = 0;

		/* Extract board data from DTB */
		for(i = 0 ; i < board_data_count; i++) {
			board_data[i].variant_id = fdt32_to_cpu(((struct board_id *)board_prop)->variant_id);
			board_data[i].platform_subtype = fdt32_to_cpu(((struct board_id *)board_prop)->platform_subtype);
			/* For V2/V3 version of DTBs we have platform version field as part
			 * of variant ID, in such case the subtype will be mentioned as 0x0
			 * As the qcom, board-id = <0xSSPMPmPH, 0x0>
			 * SS -- Subtype
			 * PM -- Platform major version
			 * Pm -- Platform minor version
			 * PH -- Platform hardware CDP/MTP
			 * In such case to make it compatible with LK algorithm move the subtype
			 * from variant_id to subtype field
			 */
			if (board_data[i].platform_subtype == 0)
				board_data[i].platform_subtype =
					fdt32_to_cpu(((struct board_id *)board_prop)->variant_id) >> 0x18;

				len_board_id -= sizeof(struct board_id);
				board_prop += sizeof(struct board_id);
			}

			/* Extract platform data from DTB */
			for(i = 0 ; i < msm_data_count; i++) {
				platform_data[i].platform_id = fdt32_to_cpu(((struct plat_id *)plat_prop)->platform_id);
				platform_data[i].soc_rev = fdt32_to_cpu(((struct plat_id *)plat_prop)->soc_rev);
				len_plat_id -= sizeof(struct plat_id);
				plat_prop += sizeof(struct plat_id);
			}

			if (dtb_ver == DEV_TREE_VERSION_V3 && pmic_prop) {
				/* Extract pmic data from DTB */
				for(i = 0 ; i < pmic_data_count; i++) {
					pmic_data[i].pmic_version[0]= fdt32_to_cpu(((struct pmic_id *)pmic_prop)->pmic_version[0]);
					pmic_data[i].pmic_version[1]= fdt32_to_cpu(((struct pmic_id *)pmic_prop)->pmic_version[1]);
					pmic_data[i].pmic_version[2]= fdt32_to_cpu(((struct pmic_id *)pmic_prop)->pmic_version[2]);
					pmic_data[i].pmic_version[3]= fdt32_to_cpu(((struct pmic_id *)pmic_prop)->pmic_version[3]);
					len_pmic_id -= sizeof(struct pmic_id);
					pmic_prop += sizeof(struct pmic_id);
				}

				/* We need to merge board & platform data into dt entry structure */
				num_entries = msm_data_count * board_data_count * pmic_data_count;
			} else {
				/* We need to merge board & platform data into dt entry structure */
				num_entries = msm_data_count * board_data_count;
			}

			if ((((uint64_t)msm_data_count * (uint64_t)board_data_count * (uint64_t)pmic_data_count) !=
						msm_data_count * board_data_count * pmic_data_count) ||
					(((uint64_t)msm_data_count * (uint64_t)board_data_count) != msm_data_count * board_data_count)) {

				FreePool(board_data);
				FreePool(platform_data);
				if (pmic_data)
					FreePool(pmic_data);
				if (model)
					FreePool(model);
				return FALSE;
			}

			dt_entry_array = (struct dt_entry*) AllocatePool(sizeof(struct dt_entry) * num_entries);
			if (!dt_entry_array) {
				DEBUG((EFI_D_ERROR, "Failed to allocate memory for dt_entry_array\n"));
				return FALSE;
			}

			/* If we have '<X>; <Y>; <Z>' as platform data & '<A>; <B>; <C>' as board data.
			 * Then dt entry should look like
			 * <X ,A >;<X, B>;<X, C>;
			 * <Y ,A >;<Y, B>;<Y, C>;
			 * <Z ,A >;<Z, B>;<Z, C>;
			 */
			i = 0;
			k = 0;
			n = 0;
			for (i = 0; i < msm_data_count; i++) {
				for (j = 0; j < board_data_count; j++) {
					if (dtb_ver == DEV_TREE_VERSION_V3 && pmic_prop) {
						for (n = 0; n < pmic_data_count; n++) {
							dt_entry_array[k].platform_id = platform_data[i].platform_id;
							dt_entry_array[k].soc_rev = platform_data[i].soc_rev;
							dt_entry_array[k].variant_id = board_data[j].variant_id;
							dt_entry_array[k].board_hw_subtype = board_data[j].platform_subtype;
							dt_entry_array[k].pmic_rev[0]= pmic_data[n].pmic_version[0];
							dt_entry_array[k].pmic_rev[1]= pmic_data[n].pmic_version[1];
							dt_entry_array[k].pmic_rev[2]= pmic_data[n].pmic_version[2];
							dt_entry_array[k].pmic_rev[3]= pmic_data[n].pmic_version[3];
							dt_entry_array[k].offset = (UINT64)dtb;
							dt_entry_array[k].size = dtb_size;
							k++;
						}

					} else {
						dt_entry_array[k].platform_id = platform_data[i].platform_id;
						dt_entry_array[k].soc_rev = platform_data[i].soc_rev;
						dt_entry_array[k].variant_id = board_data[j].variant_id;
						dt_entry_array[k].board_hw_subtype = board_data[j].platform_subtype;
						dt_entry_array[k].pmic_rev[0]= BoardPmicTarget(0);
						dt_entry_array[k].pmic_rev[1]= BoardPmicTarget(1);
						dt_entry_array[k].pmic_rev[2]= BoardPmicTarget(2);
						dt_entry_array[k].pmic_rev[3]= BoardPmicTarget(3);
						dt_entry_array[k].offset = (UINT64)dtb;
						dt_entry_array[k].size = dtb_size;
						k++;
					}
				}
			}

			for (i=0 ;i < num_entries; i++) {
				if (platform_dt_absolute_match(&(dt_entry_array[i]), dtb_list)) {
					DEBUG((EFI_D_VERBOSE, "Device tree exact match the board: <0x%x 0x%x 0x%x 0x%x> == <0x%x 0x%x 0x%x 0x%x>\n",
								dt_entry_array[i].platform_id,
								dt_entry_array[i].variant_id,
								dt_entry_array[i].soc_rev,
								dt_entry_array[i].board_hw_subtype,
								BoardPlatformRawChipId(),
								BoardPlatformType(),
								BoardPlatformChipVersion(),
								BoardPlatformSubType()));
				} else {
					DEBUG((EFI_D_VERBOSE, "Device tree's msm_id doesn't match the board: <0x%x 0x%x 0x%x 0x%x> != <0x%x 0x%x 0x%x 0x%x>\n",
								dt_entry_array[i].platform_id,
								dt_entry_array[i].variant_id,
								dt_entry_array[i].soc_rev,
								dt_entry_array[i].board_hw_subtype,
								BoardPlatformRawChipId(),
								BoardPlatformType(),
								BoardPlatformChipVersion(),
								BoardPlatformSubType()));
				}
			}

			FreePool(board_data);
			FreePool(platform_data);
			if (pmic_data)
				FreePool(pmic_data);
			FreePool(dt_entry_array);
		}
	if (model)
		FreePool(model);
	return TRUE;
}

/*
 * Will relocate the DTB to the tags addr if the device tree is found and return
 * its address
 *
 * Arguments:    kernel - Start address of the kernel loaded in RAM
 *               tags - Start address of the tags loaded in RAM
 *               kernel_size - Size of the kernel in bytes
 *
 * Return Value: DTB address : If appended device tree is found
 *               'NULL'         : Otherwise
 */
VOID *DeviceTreeAppended(VOID *kernel, UINT32 kernel_size, UINT32 dtb_offset, VOID *tags)
{
	EFI_STATUS Status;
	uintptr_t kernel_end = (uintptr_t)kernel + kernel_size;
	VOID *dtb = NULL;
	VOID *bestmatch_tag = NULL;
	UINT64 RamdiskLoadAddr;
	UINT64 BaseMemory = 0;
	struct dt_entry *best_match_dt_entry = NULL;
	UINT32 bestmatch_tag_size;
	struct dt_entry_node *dt_entry_queue = NULL;
	struct dt_entry_node *dt_node_tmp1 = NULL;
	struct dt_entry_node *dt_node_tmp2 = NULL;

	/* Initialize the dtb entry node*/
	dt_entry_queue = (struct dt_entry_node *) 
				AllocatePool(sizeof(struct dt_entry_node));
	if (!dt_entry_queue) {
		DEBUG((EFI_D_ERROR, "Out of memory\n"));
		return NULL;
	}

	memset(dt_entry_queue, 0, sizeof(struct dt_entry_node));
	list_initialize(&dt_entry_queue->node);

	if (!dtb_offset){
		DEBUG((EFI_D_ERROR, "DTB offset is NULL\n"));
		goto out;
	}

	if (((uintptr_t)kernel + (uintptr_t)dtb_offset) < (uintptr_t)kernel) {
		goto out;
	}
	dtb = kernel + dtb_offset;
	while (((uintptr_t)dtb + sizeof(struct fdt_header)) < (uintptr_t)kernel_end) {
		struct fdt_header dtb_hdr;
		UINT32 dtb_size;

		/* the DTB could be unaligned, so extract the header,
		 * and operate on it separately */
		gBS->CopyMem(&dtb_hdr, dtb, sizeof(struct fdt_header));
		if (fdt_check_header((const VOID *)&dtb_hdr) != 0 ||
				fdt_check_header_ext((VOID *)&dtb_hdr) != 0 ||
				((uintptr_t)dtb + (uintptr_t)fdt_totalsize((const VOID *)&dtb_hdr) < (uintptr_t)dtb) ||
				((uintptr_t)dtb + (uintptr_t)fdt_totalsize((const VOID *)&dtb_hdr) > (uintptr_t)kernel_end))
			break;
		dtb_size = fdt_totalsize(&dtb_hdr);

		if (!DeviceTreeCompatible(dtb, dtb_size, dt_entry_queue)) {
			DEBUG((EFI_D_VERBOSE, "Error while DTB parse continue with next DTB\n"));
			if (!GetRticDtb(dtb))
				DEBUG((EFI_D_VERBOSE, "Error while DTB parsing RTIC prop continue with next DTB\n"));

		}

		/* goto the next device tree if any */
		dtb += dtb_size;
	}
	best_match_dt_entry = platform_dt_match_best(dt_entry_queue);
	if (best_match_dt_entry){
		bestmatch_tag = (VOID *)best_match_dt_entry->offset;
		bestmatch_tag_size = best_match_dt_entry->size;
		DEBUG((EFI_D_INFO,"Best match DTB tags %u/%08x/0x%08x/%x/%x/%x/%x/%x/(offset)0x%08x/(size)0x%08x\n",
				best_match_dt_entry->platform_id, best_match_dt_entry->variant_id,
				best_match_dt_entry->board_hw_subtype, best_match_dt_entry->soc_rev,
				best_match_dt_entry->pmic_rev[0], best_match_dt_entry->pmic_rev[1],
				best_match_dt_entry->pmic_rev[2], best_match_dt_entry->pmic_rev[3],
				best_match_dt_entry->offset, best_match_dt_entry->size));
		DEBUG((EFI_D_INFO, "Using pmic info 0x%0x/0x%x/0x%x/0x%0x for device 0x%0x/0x%x/0x%x/0x%0x\n",
				best_match_dt_entry->pmic_rev[0], best_match_dt_entry->pmic_rev[1],
				best_match_dt_entry->pmic_rev[2], best_match_dt_entry->pmic_rev[3],
				BoardPmicTarget(0), BoardPmicTarget(1),
				BoardPmicTarget(2), BoardPmicTarget(3)));
	}
	/* free queue's memory */
	list_for_every_entry(&dt_entry_queue->node, dt_node_tmp1, dt_node, node) {
		dt_node_tmp2 = (struct dt_entry_node *) dt_node_tmp1->node.prev;
		dt_entry_list_delete(dt_node_tmp1);
		dt_node_tmp1 = dt_node_tmp2;
	}

	if(bestmatch_tag) {
		Status = BaseMem(&BaseMemory);
		if (Status != EFI_SUCCESS)
		{
			DEBUG((EFI_D_ERROR,"Unable to find Base memory for DDR %r\n", Status));
			FreePool(dt_entry_queue);
			goto out;
		}

		RamdiskLoadAddr = (EFI_PHYSICAL_ADDRESS)(BaseMemory | PcdGet32(RamdiskLoadAddress));
		if((RamdiskLoadAddr - (UINT64)tags) > RamdiskLoadAddr){
			DEBUG((EFI_D_ERROR, "Tags address is not valid\n"));
			goto out;
		}
		if((RamdiskLoadAddr - (UINT64)tags) < bestmatch_tag_size){
			DEBUG((EFI_D_ERROR, "Tag size is over the limit\n"));
			goto out;
		}
		gBS->CopyMem(tags, bestmatch_tag, bestmatch_tag_size);
		/* clear out the old DTB magic so kernel doesn't find it */
		*((UINT32 *)(kernel + dtb_offset)) = 0;
		return tags;
	}

	DEBUG((EFI_D_ERROR, "DTB offset is incorrect, kernel image does not have appended DTB\n"));

	/*DEBUG((EFI_D_ERROR, "Device info 0x%08x/%08x/0x%08x/%u, pmic 0x%0x/0x%x/0x%x/0x%0x\n",
	  board_platform_id(), board_soc_version(),
	  board_target_id(), board_hardware_subtype(),
	  BoardPmicTarget(0), BoardPmicTarget(1),
	  BoardPmicTarget(2), BoardPmicTarget(3)));*/
out:
	if (dt_entry_queue)
		FreePool(dt_entry_queue);
	return NULL;
}

STATIC BOOLEAN CheckAllBitsSet(UINT32 DtMatchVal)
{
	return (DtMatchVal & ALL_BITS_SET) == (ALL_BITS_SET);
}

STATIC BOOLEAN ReadDtbFindMatch(VOID* Dtb, UINT32* MatchVal)
{
	const char *PlatProp = NULL;
	const char *BoardProp = NULL;
	const char *PmicProp = NULL;
	INT32 LenBoardId;
	INT32 LenPlatId;
	INT32 LenPmicId;
	INT32 MinPlatIdLen = PLAT_ID_SIZE;
	UINT32 RootOffset = 0;
	/*DT parameters*/
	UINT32 DtPlatformId;
	UINT32 DtVariantId;
	UINT32 DtPlatformSubtype;
	UINT32 DtSocRev;
	UINT32 DtPmicTarget[MAX_PMIC_IDX];
	UINT32 DtFoundryId;

	/*Ensure MatchVal to 0 initially*/
	*MatchVal = 0;
	RootOffset = fdt_path_offset(Dtb, "/");
	if (RootOffset < 0) {
		DEBUG ((EFI_D_ERROR, "Unable to locate root node\n"));
		return FALSE;
	}

	/* Get the msm-id prop from DTB */
	PlatProp = (const char *)fdt_getprop(Dtb, RootOffset, "qcom,msm-id", &LenPlatId);
	if (PlatProp && (LenPlatId > 0) && (!(LenPlatId % MinPlatIdLen))) {
		/*Compare msm-id of the dtb vs Board*/
		DtPlatformId = fdt32_to_cpu(((struct plat_id *)PlatProp)->platform_id);
		DEBUG ((EFI_D_VERBOSE, "Boardsocid = %x, Dtsocid = %x\n", (BoardPlatformRawChipId() & 0xffff),(DtPlatformId & 0xffff)));
		if ((BoardPlatformRawChipId() & 0xffff) == (DtPlatformId & 0xffff)) {
			*MatchVal |= SOC_MATCH;
		} else {
			DEBUG ((EFI_D_VERBOSE, "qcom,msm-id doesnot match\n"));
			return FALSE;
		}
		/*Compare soc rev of the dtb vs Board*/
		DtSocRev = fdt32_to_cpu(((struct plat_id *)PlatProp)->soc_rev);
		DEBUG ((EFI_D_VERBOSE, "BoardSocRev = %x, DtSocRev =%x\n", BoardPlatformChipVersion(), DtSocRev));
		if (DtSocRev == BoardPlatformChipVersion()) {
			*MatchVal |= VERSION_MATCH;
		} else if (DtSocRev) {
			DEBUG ((EFI_D_VERBOSE, "soc version doesnot match\n"));
			return FALSE;
		}
		/*Compare Foundry Id of the dtb vs Board*/
		DtFoundryId = fdt32_to_cpu(((struct plat_id *)PlatProp)->platform_id) & 0x00ff0000;
		DEBUG ((EFI_D_VERBOSE, "BoardFoundry = %x, DtFoundry = %x\n", (BoardPlatformFoundryId() << PLATFORM_FOUNDRY_SHIFT), DtFoundryId));
		if (DtFoundryId == (BoardPlatformFoundryId() << PLATFORM_FOUNDRY_SHIFT)) {
			*MatchVal |= FOUNDRYID_MATCH;
		} else {
			DEBUG ((EFI_D_VERBOSE, "soc foundry doesnot match\n"));
		}
	} else {
		DEBUG ((EFI_D_VERBOSE, "qcom, msm-id does not exist (or) is (%d) not a multiple of (%d)\n", LenPlatId, MinPlatIdLen));
	}

	/*Get the properties like variant id, subtype from Dtb then compare the dtb vs Board*/
	BoardProp = (const char *)fdt_getprop(Dtb, RootOffset, "qcom,board-id", &LenBoardId);
	if (BoardProp && (LenBoardId > 0) && (!(LenBoardId % BOARD_ID_SIZE))) {
		DtVariantId = fdt32_to_cpu(((struct board_id *)BoardProp)->variant_id);
		DtPlatformSubtype = fdt32_to_cpu(((struct board_id *)BoardProp)->platform_subtype);
		if (DtPlatformSubtype == 0)
			DtPlatformSubtype = fdt32_to_cpu(((struct board_id *)BoardProp)->variant_id) >> 0x18;

		DEBUG ((EFI_D_VERBOSE, "BoardVariant = %x, DtVariant = %x\n", BoardPlatformType(), DtVariantId));
		if (DtVariantId == BoardPlatformType()) {
			*MatchVal |= VARIANT_MATCH;
		} else if (DtVariantId) {
			DEBUG ((EFI_D_VERBOSE, "qcom,board-id doesnot match\n"));
			return FALSE;
		}

		DEBUG ((EFI_D_VERBOSE, "BoardSubtype = %x, DtSubType = %x\n",BoardPlatformSubType(), DtPlatformSubtype));
		if (DtPlatformSubtype == BoardPlatformSubType()) {
			*MatchVal |= SUBTYPE_MATCH;
		} else if (DtPlatformSubtype) {
			DEBUG ((EFI_D_VERBOSE, "subtype-id doesnot match\n"));
			return FALSE;
		}
	} else {
		DEBUG ((EFI_D_VERBOSE, "qcom,board-id does not exist (or)(%d) is not a multiple of (%d)\n", LenBoardId,BOARD_ID_SIZE));
	}

	/*Get the pmic property from Dtb then compare the dtb vs Board*/
        PmicProp = (const char *)fdt_getprop(Dtb, RootOffset, "qcom,pmic-id", &LenPmicId);
	if (PmicProp && (LenPmicId > 0) && (!(LenPmicId % PMIC_ID_SIZE))) {
		DtPmicTarget[PMIC_IDX0]= fdt32_to_cpu(((struct pmic_id *)PmicProp)->pmic_version[PMIC_IDX0]);
		DtPmicTarget[PMIC_IDX1]= fdt32_to_cpu(((struct pmic_id *)PmicProp)->pmic_version[PMIC_IDX1]);
		DtPmicTarget[PMIC_IDX2]= fdt32_to_cpu(((struct pmic_id *)PmicProp)->pmic_version[PMIC_IDX2]);
		DtPmicTarget[PMIC_IDX3]= fdt32_to_cpu(((struct pmic_id *)PmicProp)->pmic_version[PMIC_IDX3]);

		DEBUG ((EFI_D_VERBOSE, "BoardPmicids = %x/%x/%x/%x DtPmicids = %x/%x/%x/%x\n", BoardPmicTarget(0), BoardPmicTarget(1),BoardPmicTarget(2),BoardPmicTarget(4), DtPmicTarget[0], DtPmicTarget[1], DtPmicTarget[2], DtPmicTarget[3]));
		if ((DtPmicTarget[PMIC_IDX0] == BoardPmicTarget(PMIC_IDX0))
				&& (DtPmicTarget[PMIC_IDX1] == BoardPmicTarget(PMIC_IDX1))
				&& (DtPmicTarget[PMIC_IDX2] == BoardPmicTarget(PMIC_IDX2))
				&& (DtPmicTarget[PMIC_IDX3] == BoardPmicTarget(PMIC_IDX3))) {
			*MatchVal |= PMIC_MATCH;
		} else if (DtPmicTarget[PMIC_IDX0] || DtPmicTarget[PMIC_IDX1] || DtPmicTarget[PMIC_IDX2] || DtPmicTarget[PMIC_IDX3]) {
			DEBUG ((EFI_D_VERBOSE, "Pmic version doesnot match\n"));
			return FALSE;
		}
	} else {
		DEBUG ((EFI_D_VERBOSE, "qcom,pmic-id does not exit (or) is (%d) not a multiple of (%d)\n", LenPmicId, PMIC_ID_SIZE));
	}
	return TRUE;
}

VOID* GetSocDtb (VOID *Kernel, UINT32 KernelSize, UINT32 DtbOffset, VOID *DtbLoadAddr)
{
	uintptr_t KernelEnd = (uintptr_t)Kernel + KernelSize;
	VOID *Dtb = NULL;
	struct fdt_header DtbHdr;
	UINT32 LocalSocDtMatch = 0;
	BOOLEAN Match = FALSE;
	VOID* BestMatchDt = NULL;
	UINT32 DtbSize = 0;

	if (!DtbOffset){
		DEBUG((EFI_D_ERROR, "DTB offset is NULL\n"));
		return NULL;
	}

	if (((uintptr_t)Kernel + (uintptr_t)DtbOffset) < (uintptr_t)Kernel) {
		return NULL;
	}
	Dtb = Kernel + DtbOffset;
	while (((uintptr_t)Dtb + sizeof(struct fdt_header)) < (uintptr_t)KernelEnd) {
		/* the DTB could be unaligned, so extract the header,
		 * and operate on it separately */
		gBS->CopyMem(&DtbHdr, Dtb, sizeof(struct fdt_header));
		DtbSize = fdt_totalsize((const VOID *)&DtbHdr);
		if (fdt_check_header((const VOID *)&DtbHdr) != 0 ||
				fdt_check_header_ext((VOID *)&DtbHdr) != 0 ||
				((uintptr_t)Dtb + DtbSize < (uintptr_t)Dtb) ||
				((uintptr_t)Dtb + DtbSize > (uintptr_t)KernelEnd))
			break;

		Match = ReadDtbFindMatch(Dtb, &LocalSocDtMatch);
		if (Match) {
			if (CheckAllBitsSet(LocalSocDtMatch)) {
				DEBUG ((EFI_D_VERBOSE, "Exact DTB match found. DTBO search is not required\n"));
				DtboNeed = FALSE;
			}

			if (BestSocDtMatch < LocalSocDtMatch) {
				BestSocDtMatch = LocalSocDtMatch;
				BestMatchDt = Dtb;
			}
		} else {
			if (!GetRticDtb(Dtb))
				DEBUG((EFI_D_VERBOSE, "Error while DTB parsing RTIC prop continue with next DTB\n"));

		}

		DEBUG ((EFI_D_VERBOSE, "Match = %x Bestmatch = %x\n", Match, BestSocDtMatch));
		Dtb += DtbSize;
	}

	if (!BestMatchDt) {
		DEBUG ((EFI_D_ERROR, "No match found for Soc Dtb type\n"));
		return NULL;
	}

	return BestMatchDt;
}

VOID* GetBoardDtb (BootInfo *Info, VOID* DtboImgBuffer)
{
	struct DtboTableHdr* DtboTableHdr = DtboImgBuffer;
	struct DtboTableEntry *DtboTableEntry = NULL;
	UINT32 DtboCount = 0;
	UINT32 LocalBoardDtMatch = 0;
	VOID* BestMatchDt = NULL;
	VOID *BoardDtb = NULL;
	BOOLEAN Match = FALSE;
	UINT32 DtboTableEntriesCount = 0;
	UINT32 FirstDtboTableEntryOffset = 0;

	if (!DtboImgBuffer) {
		DEBUG((EFI_D_ERROR, "Dtbo Img buffer is NULL\n"));
		return NULL;
	}

	FirstDtboTableEntryOffset = fdt32_to_cpu(DtboTableHdr->DtEntryOffset);
	if (CHECK_ADD64((UINT64)DtboImgBuffer, FirstDtboTableEntryOffset)) {
		DEBUG((EFI_D_ERROR, "Integer overflow deteced with Dtbo address\n"));
		return NULL;
	}

	DtboTableEntry = (struct DtboTableEntry *)(DtboImgBuffer + FirstDtboTableEntryOffset);
	if (!DtboTableEntry) {
		DEBUG((EFI_D_ERROR, "No proper DtTable\n"));
		return NULL;
	}

	DtboTableEntriesCount = fdt32_to_cpu(DtboTableHdr->DtEntryCount);
	for (DtboCount = 0; DtboCount < DtboTableEntriesCount; DtboCount++) {
		if (CHECK_ADD64((UINT64)DtboImgBuffer, fdt32_to_cpu(DtboTableEntry->DtOffset))) {
			DEBUG((EFI_D_ERROR, "Integer overflow deteced with Dtbo address\n"));
			return NULL;
		}
		BoardDtb = DtboImgBuffer + fdt32_to_cpu(DtboTableEntry->DtOffset);
		if (fdt_check_header(BoardDtb) || fdt_check_header_ext(BoardDtb)) {
			DEBUG ((EFI_D_ERROR, "No Valid Dtb\n"));
			break;
		}
		Match = ReadDtbFindMatch(BoardDtb, &LocalBoardDtMatch);
		if (Match) {
			if (BestBoardDtMatch < LocalBoardDtMatch) {
				BestBoardDtMatch = LocalBoardDtMatch;
				BestMatchDt = BoardDtb;
			}
		}
		DEBUG ((EFI_D_VERBOSE, "Dtbo count = %u LocalBoardDtMatch =%x\n",DtboCount, LocalBoardDtMatch));
		DtboTableEntry++;
	}

	if (!BestMatchDt) {
		DEBUG ((EFI_D_ERROR, "Unable to find the Board Dtb\n"));
                return NULL;
	}
	return BestMatchDt;
}

/* Returns 0 if the device tree is valid. */
int DeviceTreeValidate (UINT8* DeviceTreeBuff, UINT32 PageSize, UINT32* DeviceTreeSize)
{
	int dt_entry_size;
	UINT64 hdr_size;
	struct dt_table*             table;
	if (DeviceTreeSize)
	{
		table = (struct dt_table*) DeviceTreeBuff;
		if(table->magic != DEV_TREE_MAGIC) {
			// bad magic in device tree table
			return -1;
		}
		if (table->version == DEV_TREE_VERSION_V1) {
			dt_entry_size = sizeof(struct dt_entry_v1);
		} else if (table->version == DEV_TREE_VERSION_V2) {
			dt_entry_size = sizeof(struct dt_entry_v2);
		} else if (table-> version == DEV_TREE_VERSION_V3){
			dt_entry_size = sizeof(struct dt_entry);
		} else {
			// unsupported dt version
			return -1;
		}
		hdr_size = ((UINT64)table->num_entries * dt_entry_size) + DEV_TREE_HEADER_SIZE;
		//hdr_size = ROUNDUP(hdr_size, PageSize);
		hdr_size = EFI_SIZE_TO_PAGES(hdr_size);
		if (hdr_size > MAX_UINT64)
			return -1;
		else
			*DeviceTreeSize = hdr_size & MAX_UINT64;
		//dt_entry_ptr = (struct dt_entry *)((CHAR8 *)table + DEV_TREE_HEADER_SIZE);
		//table_ptr = dt_entry_ptr;

		DEBUG ((EFI_D_ERROR, "DT Total number of entries: %d, DTB version: %d\n", table->num_entries, table->version));
	}
	return 0;
}

STATIC int platform_dt_absolute_match(struct dt_entry *cur_dt_entry, struct dt_entry_node *dt_list)
{
	UINT32 cur_dt_hw_platform;
	UINT32 cur_dt_hw_subtype;
	UINT32 cur_dt_msm_id;
	dt_node *dt_node_tmp = NULL;

	/* Platform-id
	 * bit no |31	 24|23	16|15	0|
	 *        |reserved|foundry-id|msm-id|
	 */
	cur_dt_msm_id = (cur_dt_entry->platform_id & 0x0000ffff);
	cur_dt_hw_platform = (cur_dt_entry->variant_id & 0x000000ff);
	cur_dt_hw_subtype = (cur_dt_entry->board_hw_subtype & 0xff);

	/* 1. must match the msm_id, platform_hw_id, platform_subtype and DDR size
	 *  soc, board major/minor, pmic major/minor must less than board info
	 *  2. find the matched DTB then return 1
	 *  3. otherwise return 0
	 */

	if((cur_dt_msm_id == (BoardPlatformRawChipId() & 0x0000ffff)) &&
			(cur_dt_hw_platform == BoardPlatformType()) &&
			(cur_dt_hw_subtype == BoardPlatformSubType()) &&
			(cur_dt_entry->soc_rev <= BoardPlatformChipVersion()) &&
			((cur_dt_entry->variant_id & 0x00ffff00) <= (BoardTargetId() & 0x00ffff00)) &&
			(cur_dt_entry->pmic_rev[0] <= BoardPmicTarget(0)) &&
			(cur_dt_entry->pmic_rev[1] <= BoardPmicTarget(1)) &&
			(cur_dt_entry->pmic_rev[2] <= BoardPmicTarget(2)) &&
			(cur_dt_entry->pmic_rev[3] <= BoardPmicTarget(3))) {

		dt_node_tmp = dt_entry_list_init();
		if (!dt_node_tmp) {
			DEBUG((EFI_D_ERROR, "dt_node_tmp is NULL\n"));
			return 0;
		}

		gBS->CopyMem((VOID *)dt_node_tmp->dt_entry_m,(VOID *)cur_dt_entry, sizeof(struct dt_entry));

		DEBUG((EFI_D_VERBOSE, "Add DTB entry 0x%x/%08x/0x%08x/0x%x/0x%x/0x%x/0x%x/0x%x/0x%x/0x%x\n",
					dt_node_tmp->dt_entry_m->platform_id, dt_node_tmp->dt_entry_m->variant_id,
					dt_node_tmp->dt_entry_m->board_hw_subtype, dt_node_tmp->dt_entry_m->soc_rev,
					dt_node_tmp->dt_entry_m->pmic_rev[0], dt_node_tmp->dt_entry_m->pmic_rev[1],
					dt_node_tmp->dt_entry_m->pmic_rev[2], dt_node_tmp->dt_entry_m->pmic_rev[3],
					dt_node_tmp->dt_entry_m->offset, dt_node_tmp->dt_entry_m->size));

		insert_dt_entry_in_queue(dt_list, dt_node_tmp);
		return 1;
	}
	return 0;
}

int platform_dt_absolute_compat_match(struct dt_entry_node *dt_list, UINT32 dtb_info) {
	struct dt_entry_node *dt_node_tmp1 = NULL;
	struct dt_entry_node *dt_node_tmp2 = NULL;
	UINT32 current_info = 0;
	UINT32 board_info = 0;
	UINT32 best_info = 0;
	UINT32 current_pmic_model[4] = {0, 0, 0, 0};
	UINT32 board_pmic_model[4] = {0, 0, 0, 0};
	UINT32 best_pmic_model[4] = {0, 0, 0, 0};
	UINT32 delete_current_dt = 0;
	UINT32 i;

	/* start to select the exact entry
	 * default to exact match 0, if find current DTB entry info is the same as board info,
	 * then exact match board info.
	 */
	list_for_every_entry(&dt_list->node, dt_node_tmp1, dt_node, node) {
		if (!dt_node_tmp1){
			DEBUG((EFI_D_ERROR, "Current node is the end\n"));
			break;
		}
		switch(dtb_info) {
			case DTB_FOUNDRY:
				current_info = ((dt_node_tmp1->dt_entry_m->platform_id) & 0x00ff0000);
				board_info = BoardPlatformFoundryId() << 16;
				break;
			case DTB_PMIC_MODEL:
				for (i = 0; i < 4; i++) {
					current_pmic_model[i] = (dt_node_tmp1->dt_entry_m->pmic_rev[i] & 0xff);
					board_pmic_model[i] = BoardPmicModel(i);
				}
				break;
			default:
				DEBUG((EFI_D_ERROR, "ERROR: Unsupported version (%d) in dt node check \n",
							dtb_info));
				return 0;
		}

		if (dtb_info == DTB_PMIC_MODEL) {
			if ((current_pmic_model[0] == board_pmic_model[0]) &&
					(current_pmic_model[1] == board_pmic_model[1]) &&
					(current_pmic_model[2] == board_pmic_model[2]) &&
					(current_pmic_model[3] == board_pmic_model[3])) {

				for (i = 0; i < 4; i++) {
					best_pmic_model[i] = current_pmic_model[i];
				}
				break;
			}
		} else {
			if (current_info == board_info) {
				best_info = current_info;
				break;
			}
		}
	}

	list_for_every_entry(&dt_list->node, dt_node_tmp1, dt_node, node) {
		if (!dt_node_tmp1){
			DEBUG((EFI_D_ERROR, "Current node is the end\n"));
			break;
		}
		switch(dtb_info) {
			case DTB_FOUNDRY:
				current_info = ((dt_node_tmp1->dt_entry_m->platform_id) & 0x00ff0000);
				break;
			case DTB_PMIC_MODEL:
				for (i = 0; i < 4; i++) {
					current_pmic_model[i] = (dt_node_tmp1->dt_entry_m->pmic_rev[i] & 0xff);
				}
				break;
			default:
				DEBUG((EFI_D_ERROR, "ERROR: Unsupported version (%d) in dt node check \n",
							dtb_info));
				return 0;
		}

		if (dtb_info == DTB_PMIC_MODEL) {
			if ((current_pmic_model[0] != best_pmic_model[0]) ||
					(current_pmic_model[1] != best_pmic_model[1]) ||
					(current_pmic_model[2] != best_pmic_model[2]) ||
					(current_pmic_model[3] != best_pmic_model[3])) {

				delete_current_dt = 1;
			}
		} else {
			if (current_info != best_info) {
				delete_current_dt = 1;
			}
		}

		if (delete_current_dt) {
			DEBUG((EFI_D_VERBOSE, "Delete don't fit DTB entry %u/%08x/0x%08x/%x/%x/%x/%x/%x/%x/%x\n",
						dt_node_tmp1->dt_entry_m->platform_id, dt_node_tmp1->dt_entry_m->variant_id,
						dt_node_tmp1->dt_entry_m->board_hw_subtype, dt_node_tmp1->dt_entry_m->soc_rev,
						dt_node_tmp1->dt_entry_m->pmic_rev[0], dt_node_tmp1->dt_entry_m->pmic_rev[1],
						dt_node_tmp1->dt_entry_m->pmic_rev[2], dt_node_tmp1->dt_entry_m->pmic_rev[3],
						dt_node_tmp1->dt_entry_m->offset, dt_node_tmp1->dt_entry_m->size));

			dt_node_tmp2 = (struct dt_entry_node *) dt_node_tmp1->node.prev;
			dt_entry_list_delete(dt_node_tmp1);
			dt_node_tmp1 = dt_node_tmp2;
			delete_current_dt = 0;
		}
	}

	return 1;
}

int update_dtb_entry_node(struct dt_entry_node *dt_list, UINT32 dtb_info) {
	struct dt_entry_node *dt_node_tmp1 = NULL;
	struct dt_entry_node *dt_node_tmp2 = NULL;
	UINT32 current_info = 0;
	UINT32 board_info = 0;
	UINT32 best_info = 0;

	/* start to select the best entry*/
	list_for_every_entry(&dt_list->node, dt_node_tmp1, dt_node, node) {
		if (!dt_node_tmp1){
			DEBUG((EFI_D_ERROR, "Current node is the end\n"));
			break;
		}
		switch(dtb_info) {
			case DTB_SOC:
				current_info = dt_node_tmp1->dt_entry_m->soc_rev;
				board_info = BoardPlatformChipVersion();
				break;
			case DTB_MAJOR_MINOR:
				current_info = ((dt_node_tmp1->dt_entry_m->variant_id) & 0x00ffff00);
				board_info = BoardTargetId() & 0x00ffff00;
				break;
			case DTB_PMIC0:
				current_info =dt_node_tmp1->dt_entry_m->pmic_rev[0];
				board_info = BoardPmicTarget(0);
				break;
			case DTB_PMIC1:
				current_info = dt_node_tmp1->dt_entry_m->pmic_rev[1];
				board_info = BoardPmicTarget(1);
				break;
			case DTB_PMIC2:
				current_info = dt_node_tmp1->dt_entry_m->pmic_rev[2];
				board_info = BoardPmicTarget(2);
				break;
			case DTB_PMIC3:
				current_info = dt_node_tmp1->dt_entry_m->pmic_rev[3];
				board_info = BoardPmicTarget(3);
				break;
			default:
				DEBUG((EFI_D_ERROR, "ERROR: Unsupported version (%d) in dt node check \n",
							dtb_info));
				return 0;
		}

		if (current_info == board_info) {
			best_info = current_info;
			break;
		}
		if ((current_info < board_info) && (current_info > best_info)) {
			best_info = current_info;
		}
		if (current_info < best_info) {
			DEBUG((EFI_D_ERROR, "Delete don't fit DTB entry %u/%08x/0x%08x/%x/%x/%x/%x/%x/%x/%x\n",
						dt_node_tmp1->dt_entry_m->platform_id, dt_node_tmp1->dt_entry_m->variant_id,
						dt_node_tmp1->dt_entry_m->board_hw_subtype, dt_node_tmp1->dt_entry_m->soc_rev,
						dt_node_tmp1->dt_entry_m->pmic_rev[0], dt_node_tmp1->dt_entry_m->pmic_rev[1],
						dt_node_tmp1->dt_entry_m->pmic_rev[2], dt_node_tmp1->dt_entry_m->pmic_rev[3],
						dt_node_tmp1->dt_entry_m->offset, dt_node_tmp1->dt_entry_m->size));

			dt_node_tmp2 = (struct dt_entry_node *) dt_node_tmp1->node.prev;
			dt_entry_list_delete(dt_node_tmp1);
			dt_node_tmp1 = dt_node_tmp2;
		}
	}

	list_for_every_entry(&dt_list->node, dt_node_tmp1, dt_node, node) {
		if (!dt_node_tmp1){
			DEBUG((EFI_D_ERROR, "Current node is the end\n"));
			break;
		}
		switch(dtb_info) {
			case DTB_SOC:
				current_info = dt_node_tmp1->dt_entry_m->soc_rev;
				break;
			case DTB_MAJOR_MINOR:
				current_info = ((dt_node_tmp1->dt_entry_m->variant_id) & 0x00ffff00);
				break;
			case DTB_PMIC0:
				current_info = dt_node_tmp1->dt_entry_m->pmic_rev[0];
				break;
			case DTB_PMIC1:
				current_info = dt_node_tmp1->dt_entry_m->pmic_rev[1];
				break;
			case DTB_PMIC2:
				current_info = dt_node_tmp1->dt_entry_m->pmic_rev[2];
				break;
			case DTB_PMIC3:
				current_info = dt_node_tmp1->dt_entry_m->pmic_rev[3];
				break;
			default:
				DEBUG((EFI_D_ERROR, "ERROR: Unsupported version (%d) in dt node check \n",
							dtb_info));
				return 0;
		}

		if (current_info != best_info) {
			DEBUG((EFI_D_VERBOSE, "Delete don't fit DTB entry %u/%08x/0x%08x/%x/%x/%x/%x/%x/%x/%x\n",
						dt_node_tmp1->dt_entry_m->platform_id, dt_node_tmp1->dt_entry_m->variant_id,
						dt_node_tmp1->dt_entry_m->board_hw_subtype, dt_node_tmp1->dt_entry_m->soc_rev,
						dt_node_tmp1->dt_entry_m->pmic_rev[0], dt_node_tmp1->dt_entry_m->pmic_rev[1],
						dt_node_tmp1->dt_entry_m->pmic_rev[2], dt_node_tmp1->dt_entry_m->pmic_rev[3],
						dt_node_tmp1->dt_entry_m->offset, dt_node_tmp1->dt_entry_m->size));

			dt_node_tmp2 = (struct dt_entry_node *) dt_node_tmp1->node.prev;
			dt_entry_list_delete(dt_node_tmp1);
			dt_node_tmp1 = dt_node_tmp2;
		}
	}
	return 1;
}

STATIC struct dt_entry *platform_dt_match_best(struct dt_entry_node *dt_list)
{
	struct dt_entry_node *dt_node_tmp1 = NULL;

	/* check Foundry id
	 * the foundry id must exact match board founddry id, this is compatibility check,
	 * if couldn't find the exact match from DTB, will exact match 0x0.
	 */
	if (!platform_dt_absolute_compat_match(dt_list, DTB_FOUNDRY))
		return NULL;

	/* check PMIC model
	 * the PMIC model must exact match board PMIC model, this is compatibility check,
	 * if couldn't find the exact match from DTB, will exact match 0x0.
	 */
	if (!platform_dt_absolute_compat_match(dt_list, DTB_PMIC_MODEL))
		return NULL;

	/* check soc version
	 * the suitable soc version must less than or equal to board soc version
	 */
	if (!update_dtb_entry_node(dt_list, DTB_SOC))
		return NULL;

	/*check major and minor version
	 * the suitable major&minor version must less than or equal to board major&minor version
	 */
	if (!update_dtb_entry_node(dt_list, DTB_MAJOR_MINOR))
		return NULL;

	/*check pmic info
	 * the suitable pmic major&minor info must less than or equal to board pmic major&minor version
	 */
	if (!update_dtb_entry_node(dt_list, DTB_PMIC0))
		return NULL;
	if (!update_dtb_entry_node(dt_list, DTB_PMIC1))
		return NULL;
	if (!update_dtb_entry_node(dt_list, DTB_PMIC2))
		return NULL;
	if (!update_dtb_entry_node(dt_list, DTB_PMIC3))
		return NULL;

	list_for_every_entry(&dt_list->node, dt_node_tmp1, dt_node, node) {
		if (!dt_node_tmp1) {
			DEBUG((EFI_D_ERROR, "ERROR: Couldn't find the suitable DTB!\n"));
			return NULL;
		}
		if (dt_node_tmp1->dt_entry_m)
			return dt_node_tmp1->dt_entry_m;
	}

	return NULL;
}
