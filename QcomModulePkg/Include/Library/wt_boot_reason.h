#ifndef _WT_BOOT_REASON_H
#define _WT_BOOT_REASON_H

#include <libfdt_env.h>

#define   TZBSP_RESET_REASON_ADDR    0x146aa7a4
#define   WT_RESET_REASON_ADDR       0x146aafe4
#define   RESET_MAGIC_INIT           0x54494E49     /* I N I T */
#define   WARM_RESET                 "WARM-RESET"
#define   UNKNWON_RESET              "UNKNWON-RESET"

struct reset_reason_map {
	uint32_t reason_num;
	char *reason_str;
};

#define PON_KPDPWR            128
#define PON_CBLPWR            64
#define PON_PON1              32
#define PON_USB_CHG           16
#define PON_DC_CHG            8
#define PON_RTC               4
#define PON_SMPL              2
#define PON_HARD_RST          1

struct pon_reason {
	uint8_t mask;
	char *str;
};

#define CRASH_LOG_HEAD_MAGIC        0x4D484C43    // "CLHM"
#define CRASH_INFO_ITEM_MAGIC       0x53415243    // "CRAS"
#define BLOCK_SIZE_512              512
#define BLOCK_SIZE_4096             4096
#define LOG_OFFSET_8M               2048    // 4096 * 2048
#define LOG_OFFSET_4M               1024    // 4096 * 1024

extern struct pon_reason ponr[];
extern char wt_bootreason[];


void set_dload_magic_to_log_ptn(void);
char *wt_get_exception_reset_reason(void);
char *wt_boot_reason(void);

#endif

