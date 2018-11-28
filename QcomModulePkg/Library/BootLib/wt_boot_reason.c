#include <wt_system_monitor.h>

#ifdef WT_BOOT_REASON
#include "UpdateCmdLine.h"
#include <Protocol/BlockIo.h>
#include <Protocol/EFIUsbDevice.h>
#include <Library/PrintLib.h>
#include <Library/BootLinux.h>
#include <Protocol/EFIPmicPon.h>
#include <LinuxLoaderLib.h>
#include <Protocol/BlockIo.h>
#include <libfdt_env.h>
#include <wt_boot_reason.h>

#define WT_PANIC_KEY_LOG_SIZE (WT_BOOTLOADER_LOG_ADDR + WT_BOOTLOADER_LOG_SIZE - WT_PANIC_KEY_LOG_ADDR)
#define LOG_HEAD_BUF_SIZE           (BLOCK_SIZE_4096)
#define PANIC_LOG_BUF_SIZE          (BLOCK_SIZE_4096)
#define DLOAD_HEAD_BUF_SIZE         (BLOCK_SIZE_4096)

char wt_bootreason[32] = {0};

static EFI_BLOCK_IO_PROTOCOL *log_BlockIo = NULL;
static EFI_HANDLE *log_Handle = NULL;
static uint32_t ptn_block_size, lba_count;

struct pon_reason ponr[] = {
	{PON_KPDPWR,              "powerkey"},
	{PON_CBLPWR,              "cbl"},
	{PON_USB_CHG,             "usb-charge"},
	{PON_DC_CHG,              "dc-charge"},
	{PON_RTC,                 "rtc"},
	{PON_SMPL,                "smpl"},
	{PON_HARD_RST,            "hard-reset"},
	{PON_PON1,                "charge"},
};

/* please must align in 8 bytes */
struct crash_log_head {
	uint32_t    magic;
	uint32_t    ptn_block_count;
	uint32_t    ptn_block_size;
	uint32_t    start_lba;
	uint32_t    lba_count;
	uint32_t    bootloader_started;
	uint32_t    kernel_started;
	uint32_t    next_log_flag;
	uint32_t    log1_exist_flag;
	uint32_t    log2_exist_flag;
	uint8_t     boot_reason_str[32];
	uint32_t    boot_reason_copies;
	uint32_t    crash_times;
	uint32_t    checksum;
	uint8_t     reserved[44];
}__attribute__((aligned(8)));

struct crash_info_item {
	uint32_t    magic;
	uint8_t     type[20];
	uint32_t    is_reset_reason_data;
	uint32_t    reset_reason_data;
	uint32_t    lba;
	uint32_t    size;
	uint32_t    padding;
	uint32_t    checksum;
	uint8_t     reserved[16];
}__attribute__((aligned(8)));

static uint32_t cal_checksum(void *buf, int len)
{
	uint32_t *_buf = buf;
	int _len = len / sizeof(uint32_t);
	uint32_t checksum = 0x55aa55aa;
	int i;

	for (i = 0; i < (_len); i++) {
		checksum += *_buf++;
	}
	return checksum;
}

static void clear_panic_key_log(void)
{
	struct wt_panic_key_log *head = (struct wt_panic_key_log *)(WT_PANIC_KEY_LOG_ADDR);
	if (head->magic != WT_PANIC_KEY_LOG_MAGIC) {
		DEBUG((EFI_D_ERROR, "wt_panic_key_log magic not match..\n"));
		return;
	}
	memset((void *)head + sizeof(struct wt_panic_key_log), 0, WT_PANIC_KEY_LOG_SIZE - sizeof(struct wt_panic_key_log));
	memset(head->reserved, 0, sizeof(head->reserved));
	head->panic_key_log_size = 0;
}

extern EFI_STATUS
PartitionGetInfo (
    IN CHAR16  *PartitionName,
    OUT EFI_BLOCK_IO_PROTOCOL **BlockIo,
    OUT EFI_HANDLE **Handle
    );

static inline EFI_STATUS get_log_partition_info()
{
	EFI_STATUS Status;
	Status = PartitionGetInfo(L"log", &log_BlockIo, &log_Handle);
	if (Status != EFI_SUCCESS)
		return Status;
	if (!log_BlockIo) {
		DEBUG((EFI_D_ERROR, "BlockIo for %s is corrupted\n", "log"));
		return EFI_VOLUME_CORRUPTED;
	}
	if (!log_Handle) {
		DEBUG((EFI_D_ERROR, "EFI handle for %s is corrupted\n", "log"));
		return EFI_VOLUME_CORRUPTED;
	}
	ptn_block_size = log_BlockIo->Media->BlockSize;

	if (ptn_block_size == BLOCK_SIZE_4096) {
		lba_count = 1;
	} else if (ptn_block_size == BLOCK_SIZE_512) {
		lba_count = 8;
	} else {
		DEBUG((EFI_D_ERROR, "get partition block size error\n"));
		return EFI_LOAD_ERROR;
	}
	return Status;
}

static inline EFI_STATUS read_log_partition(UINT64 block_offset, uint32_t buf_size, uint8_t *buf)
{
	EFI_STATUS Status;
	Status = log_BlockIo->ReadBlocks(log_BlockIo,
			log_BlockIo->Media->MediaId,
			block_offset,
			buf_size,
			buf);

	return Status;
}

static inline EFI_STATUS write_log_partition(UINT64 block_offset, uint32_t buf_size, uint8_t *buf)
{
	EFI_STATUS Status;
	Status = log_BlockIo->WriteBlocks(log_BlockIo,
			log_BlockIo->Media->MediaId,
			block_offset,
			buf_size,
			buf);

	return Status;
}

#define WT_SYSTEM_MONITOR_MESSAGE_MAGIC  0x4D4D5357  /* W S M M */
#define WT_EMERGENCY_DLOAD_MESSAGE_MAGIC 0x4D444557  /* W E D M */
struct wt_system_monitor_message_save {
	uint32_t magic;
	uint32_t len;
	uint8_t  reserved[8];
};

struct wt_emergency_dload_message_save {
	struct wt_system_monitor_message_save parent_message_save;
	uint32_t dload_magic;
	uint32_t dload_set;
	uint32_t dload_times;
	uint32_t dload_crc;
	uint8_t  reserved[16];
};

void init_wt_dload_message(struct wt_emergency_dload_message_save *wt_dload_ms)
{
	wt_dload_ms->parent_message_save.magic = WT_SYSTEM_MONITOR_MESSAGE_MAGIC;
	wt_dload_ms->parent_message_save.len = sizeof(struct wt_emergency_dload_message_save);
	wt_dload_ms->dload_magic = WT_EMERGENCY_DLOAD_MESSAGE_MAGIC;
	wt_dload_ms->dload_times = 0;
	wt_dload_ms->dload_set = 0;
	wt_dload_ms->dload_crc = wt_dload_ms->dload_times;
	memset(wt_dload_ms->reserved, 0, sizeof(wt_dload_ms->reserved));
}

void set_dload_magic_to_log_ptn()
{
	EFI_STATUS Status;
	UINT64 block_offset = 0;
	uint8_t *dload_head_buf = NULL;
	struct wt_emergency_dload_message_save *wt_dload_ms;
	uint32_t wt_dload_set[4] = {0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D};  // just for check crc

	DEBUG((EFI_D_ERROR, "set_dload_magic_to_log_ptn start.\n"));
	Status = get_log_partition_info();
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "get partition info failed\n"));
		return;
	}

	block_offset = log_BlockIo->Media->LastBlock - (LOG_OFFSET_8M * lba_count) + 1 - lba_count; // it reserved 4096 bytes
	dload_head_buf = (uint8_t *)AllocatePool(DLOAD_HEAD_BUF_SIZE);
	if (!dload_head_buf) {
		DEBUG((EFI_D_ERROR, "malloc head buffer error\n"));
		return;
	}
	memset(dload_head_buf, 0, DLOAD_HEAD_BUF_SIZE);
	Status = read_log_partition(block_offset, DLOAD_HEAD_BUF_SIZE, dload_head_buf);
	if(Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR,"cannot write log partition\n"));
		goto _err;
	}

	wt_dload_ms = (struct wt_emergency_dload_message_save *)dload_head_buf;
	if (wt_dload_ms->parent_message_save.magic != WT_SYSTEM_MONITOR_MESSAGE_MAGIC) {
		DEBUG((EFI_D_ERROR,"wt_dload_ms magic not match.\n"));
		init_wt_dload_message(wt_dload_ms);
	}

	if (wt_dload_ms->dload_magic == WT_EMERGENCY_DLOAD_MESSAGE_MAGIC &&
				wt_dload_ms->dload_crc == ((wt_dload_ms->dload_magic & wt_dload_ms->dload_set) | wt_dload_ms->dload_times)) {
		wt_dload_ms->dload_times += 1;
		wt_dload_ms->dload_set = wt_dload_set[wt_dload_ms->dload_times & 3];
		wt_dload_ms->dload_crc = ((wt_dload_ms->dload_magic & wt_dload_ms->dload_set) | wt_dload_ms->dload_times);
	} else {
		DEBUG((EFI_D_ERROR,"wt_dload_ms check magic and crc error.\n"));
		goto _err;
	}
	Status = write_log_partition(block_offset, DLOAD_HEAD_BUF_SIZE, dload_head_buf);
	if(Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR,"cannot write log partition\n"));
		goto _err;
	}
	DEBUG((EFI_D_ERROR, "set_dload_magic_to_log_ptn end.\n"));
	return;
_err:
	FreePool(dload_head_buf);
	DEBUG((EFI_D_ERROR, "set_dload_magic_to_log_ptn error.\n"));
	return;
}

static char *check_log_partition_save_bootreason()
{
	EFI_STATUS Status;
	UINT64 block_offset = 0;
	uint8_t *head_buf = NULL;
	struct crash_log_head *head = NULL;
#ifdef WT_PANIC_KEY_LOG_DISPLAY
	uint8_t *panic_key_log_buf = NULL;
	struct crash_info_item *item = NULL;
	struct wt_panic_key_log *panic_key_log_head = NULL;
	uint8_t *panic_key_log = NULL;
#endif

	Status = get_log_partition_info();
	if (Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR, "crash_log: read log partition error\n"));
		return NULL;
	}

	block_offset = log_BlockIo->Media->LastBlock - (LOG_OFFSET_8M * lba_count) + 1;
	head_buf = (uint8_t *)AllocatePool(LOG_HEAD_BUF_SIZE);
	if (!head_buf) {
		DEBUG((EFI_D_ERROR, "malloc head buffer error\n"));
		return NULL;
	}

	Status = read_log_partition(block_offset, LOG_HEAD_BUF_SIZE, head_buf);
	if(Status != EFI_SUCCESS) {
		DEBUG((EFI_D_ERROR,"cannot read log partition\n"));
		goto err;
	}

	head = (struct crash_log_head *)head_buf;
	if (head->magic != CRASH_LOG_HEAD_MAGIC) {
		DEBUG((EFI_D_ERROR,"crash_log_head magic not match.\n"));
		goto err;
	}
	if (!(head->log1_exist_flag || head->log2_exist_flag)) {
		DEBUG((EFI_D_ERROR, "crash log not exist, skip\n"));
		goto err;
	}
	if (head->checksum != cal_checksum(head, sizeof(struct crash_log_head) - sizeof(uint32_t) - sizeof(head->reserved))) {
		DEBUG((EFI_D_ERROR, "crash log head checksum error\n"));
		goto err;
	}
#ifdef WT_PANIC_KEY_LOG_DISPLAY
	item = (void *)((void *)head + sizeof(struct crash_log_head));
	/* if have 2 logs, pass last log item */
	while (item->magic == CRASH_INFO_ITEM_MAGIC) {
		++item;
	}
	--item;
	while (item->magic == CRASH_INFO_ITEM_MAGIC) {
		/* just read boot.bin only one */
		if (!CompareMem(item->type, "bootlog.bin", AsciiStrLen("bootlog.bin"))) {
			if (item->checksum != cal_checksum(item, sizeof(struct crash_info_item) - sizeof(uint32_t) - sizeof(item->reserved))) {
				DEBUG((EFI_D_ERROR, "bootlog.bin checksum error\n"));
				break;
			}
			panic_key_log_buf = (uint8_t *)AllocatePool(PANIC_LOG_BUF_SIZE);

			Status = read_log_partition(item->lba + ((WT_PANIC_KEY_LOG_ADDR - WT_BOOTLOADER_LOG_ADDR) / ptn_block_size),
							PANIC_LOG_BUF_SIZE, (void *)panic_key_log_buf);
			if(Status != EFI_SUCCESS) {
				DEBUG((EFI_D_ERROR,"cannot read log partition\n"));
				FreePool(panic_key_log_buf);
				break;
			}
			panic_key_log_head = (struct wt_panic_key_log *)panic_key_log_buf;
			if (panic_key_log_head->magic != WT_PANIC_KEY_LOG_MAGIC) {
				DEBUG((EFI_D_ERROR, "panic key log magic error\n"));
				FreePool(panic_key_log_buf);
				break;
			}
			panic_key_log = (uint8_t *)WT_PANIC_KEY_LOG_ADDR;
			memset(panic_key_log, '\0', WT_PANIC_KEY_LOG_SIZE);
			CopyMem(panic_key_log, panic_key_log_buf, panic_key_log_head->panic_key_log_size < PANIC_LOG_BUF_SIZE ? \
							panic_key_log_head->panic_key_log_size : PANIC_LOG_BUF_SIZE);
			FreePool(panic_key_log_buf);
			break;
		}
		--item;
	}
#endif
	CopyMem(wt_bootreason, head->boot_reason_str, AsciiStrnLenS((char *)head->boot_reason_str, sizeof(wt_bootreason)));
	FreePool(head_buf);

	return wt_bootreason;
err:
	FreePool(head_buf);
	return NULL;
}

static BOOLEAN pm_get_is_cold_boot(void)
{
	EFI_STATUS Status;
	EFI_QCOM_PMIC_PON_PROTOCOL *PmicPonProtocol;
	BOOLEAN WarmRtStatus;
	BOOLEAN IsColdBoot;

	Status = gBS->LocateProtocol(&gQcomPmicPonProtocolGuid, NULL,
			(VOID **) &PmicPonProtocol);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "Error locating pmic pon protocol: %r\n", Status));
		return FALSE;
	}
	Status = PmicPonProtocol->WarmResetStatus(0, &WarmRtStatus);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "Error getting warm reset status: %r\n", Status));
		return FALSE;
	}
	IsColdBoot = !WarmRtStatus;
	DEBUG((EFI_D_ERROR, "pmic_get_is_cold_boot is %d\n", IsColdBoot));
	return IsColdBoot;
}

static EFI_PM_PON_REASON_TYPE pm_get_pon_reason(void)
{
	EFI_STATUS Status;
	EFI_QCOM_PMIC_PON_PROTOCOL *PmicPonProtocol;
	EFI_PM_PON_REASON_TYPE PONReason = {0};

	Status = gBS->LocateProtocol(&gQcomPmicPonProtocolGuid, NULL,
			(VOID **) &PmicPonProtocol);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "Error locating pmic pon protocol: %r\n", Status));
		return PONReason;
	}

	Status = PmicPonProtocol->GetPonReason(0, &PONReason);
	if (EFI_ERROR(Status)) {
		DEBUG((EFI_D_ERROR, "Error getting pon reason: %r\n", Status));
		return PONReason;
	}
	return PONReason;
}

static void wt_get_rst_reason()
{
	struct wt_logbuf_info *head = (struct wt_logbuf_info *)WT_BOOTLOADER_LOG_ADDR;
	if (head->magic == WT_BOOTLOADER_LOG_MAGIC) {
		if (head->boot_reason_copies == RESET_MAGIC_INIT || head->boot_reason_copies == 0)
			CopyMem(wt_bootreason, WARM_RESET, AsciiStrLen(WARM_RESET));
		else
			CopyMem(wt_bootreason, head->boot_reason_str, AsciiStrnLenS((char *)head->boot_reason_str, sizeof(wt_bootreason)));
	} else {
		CopyMem(wt_bootreason, WARM_RESET, AsciiStrLen(WARM_RESET));
	}
}

char *wt_get_exception_reset_reason()
{
	if (pm_get_is_cold_boot() == FALSE) {
		wt_get_rst_reason();
		return wt_bootreason;
	}
	/*
	if (check_log_partition_save_bootreason())
		return wt_bootreason;
	*/	
	return NULL;
}

char *wt_boot_reason(void)
{
	uint8_t i = 0;
	EFI_PM_PON_REASON_TYPE pon = {0};
	uint8_t *pon_ptr = (uint8_t *)&pon;

	pon = pm_get_pon_reason();

	if (wt_get_exception_reset_reason()) {
		goto end;
	}
	clear_panic_key_log();
	for (i = 0; i < ARRAY_SIZE(ponr); i++) {
		if (*pon_ptr & ponr[i].mask) {
			return ponr[i].str;
		}
	}
	return UNKNWON_RESET;
end:
	clear_panic_key_log();
	return wt_bootreason;
}

#endif
