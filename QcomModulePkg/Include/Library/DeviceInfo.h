/*
 * * Copyright (c) 2011,2014-2015, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *  * Neither the name of The Linux Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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

#ifndef _DEVINFO_H_
#define _DEVINFO_H_

typedef struct device_info device_info;

#define DEVICE_MAGIC "ANDROID-BOOT!"
#define DEVICE_MAGIC_SIZE 13
#define MAX_PANEL_ID_LEN 64
#define MAX_VERSION_LEN  64

enum unlock_type {
	UNLOCK = 0,
	UNLOCK_CRITICAL,
};

struct device_info
{
	CHAR8 magic[DEVICE_MAGIC_SIZE];
	BOOLEAN is_unlocked;
	BOOLEAN is_tampered;
	BOOLEAN is_unlock_critical;
	BOOLEAN CHAR8ger_screen_enabled;
	CHAR8 display_panel[MAX_PANEL_ID_LEN];
	CHAR8 bootloader_version[MAX_VERSION_LEN];
	CHAR8 radio_version[MAX_VERSION_LEN];
	BOOLEAN verity_mode; // TRUE = enforcing, FALSE = logging
};

enum boot_state
{
	GREEN,
	ORANGE,
	YELLOW,
	RED,
};

struct verified_boot_verity_mode
{
	BOOLEAN verity_mode_enforcing;
	CHAR8 *name;
};

struct verified_boot_state_name
{
	UINT32 boot_state;
	CHAR8 *name;
};

enum boot_verfiy_event
{
	BOOT_INIT,
	DEV_UNLOCK,
	BOOTIMG_EMBEDDED_CERT_VERIFICATION_PASS,
	BOOTIMG_KEYSTORE_VERIFICATION_PASS,
	BOOTIMG_VERIFICATION_FAIL,
	USER_DENIES,
};

#endif
