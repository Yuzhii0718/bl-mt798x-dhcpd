/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 *
 * Failsafe upgrade module
 * Handles firmware upload, flash result, and MTD layout selection
 */

#include <env.h>
#include <errno.h>
#include <malloc.h>
#include <net/mtk_httpd.h>
#include <net/mtk_tcp.h>
#include <rand.h>
#include <u-boot/md5.h>
#include <linux/string.h>
#include <dm/ofnode.h>
#include <vsprintf.h>

#ifdef CONFIG_MTD
#include "../../board/mediatek/common/mtd_helper.h"
#endif
#ifdef CONFIG_MTK_BOOTMENU_MMC
#include "../../board/mediatek/common/mmc_helper.h"
#endif

#include <failsafe/error.h>
#include <failsafe/internal.h>

/* ------------------------------------------------------------------ */
/*  Core upgrade state                                                 */
/* ------------------------------------------------------------------ */

/* Externally referenced by the main entry (failsafe.c) */
u32 upload_data_id;
const void *upload_data;
size_t upload_size;
bool upgrade_success;
bool auto_action_pending;
failsafe_fw_t fw_type;

/* ------------------------------------------------------------------ */
/*  MTD layout (conditionally compiled)                                */
/* ------------------------------------------------------------------ */

#define MTD_LAYOUTS_MAXLEN	128
#define MTD_LAYOUT_CUSTOM_LABEL	"custom"
#define MTD_LAYOUT_CUSTOM_ENV	"mtd_layout_custom"

#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
static char mtd_layout_label[MTD_LAYOUTS_MAXLEN];
static bool mtd_layout_save_pending;
const char *get_mtd_layout_label(void);
#endif

#ifdef CONFIG_MTD_LAYOUT_SPI_NAND
extern const char *mtd_layout_spi_nand_replace(const char *str, char *buf,
					       size_t bufsz);
#endif

/* ------------------------------------------------------------------ */
/*  Auto-reboot helper                                                 */
/* ------------------------------------------------------------------ */

static bool failsafe_auto_reboot_enabled(void)
{
	const char *val = env_get("failsafe_auto_reboot");

	if (!val || !val[0])
		return IS_ENABLED(CONFIG_WEBUI_FAILSAFE_UI_GL) ||
		       IS_ENABLED(CONFIG_WEBUI_FAILSAFE_UI_MTK);

	if (!strcmp(val, "1") || !strcasecmp(val, "true") ||
	    !strcasecmp(val, "yes") || !strcasecmp(val, "on"))
		return true;

	return false;
}

/* ------------------------------------------------------------------ */
/*  Failure reporting                                                  */
/* ------------------------------------------------------------------ */

/*
 * Body of a response that reports a failed upgrade operation: the status
 * word the Web UI reacts to on the first line (protocol unchanged) followed
 * by the code and the message of the recorded error, so the page can tell
 * the user what went wrong instead of pointing at the serial console
 * (see <failsafe/error.h>).
 */
static const char *failsafe_upgrade_failure(char *buf, size_t size,
					    const char *status)
{
	/* An operation that failed without recording a reason still gets a
	 * code, so a report always carries one.
	 */
	if (!failsafe_error_code())
		failsafe_error(-EIO, "upgrade failed");

	snprintf(buf, size, "%s\ncode:%d\nerror:%s", status,
		 failsafe_error_code(), failsafe_error_msg());

	return buf;
}

/* ------------------------------------------------------------------ */
/*  MTD layout helper functions                                        */
/* ------------------------------------------------------------------ */

#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
static void failsafe_prepare_mtd_layout(void)
{
	const char *cur_layout, *env_layout;

	if (!mtd_layout_label[0])
		return;

	cur_layout = get_mtd_layout_label();
	env_layout = env_get("mtd_layout");

	if (!cur_layout || strcmp(cur_layout, mtd_layout_label) ||
	    !env_layout || strcmp(env_layout, mtd_layout_label)) {
		printf("httpd: switching mtd layout: %s\n", mtd_layout_label);
#ifdef CONFIG_MTD
		mtd_layout_switch_begin();
#endif
		env_set("mtd_layout", mtd_layout_label);
		env_set("mtd_layout_label", mtd_layout_label);
	}

	env_set("mtdids", NULL);
	env_set("mtdparts", NULL);
}

static void failsafe_save_mtd_layout(void)
{
	const char *env_layout, *legacy_layout;
	bool need_save = false;

	if (!mtd_layout_save_pending)
		return;

	env_layout = env_get("mtd_layout");
	legacy_layout = env_get("mtd_layout_label");

	if (!env_layout || strcmp(env_layout, mtd_layout_label)) {
		env_set("mtd_layout", mtd_layout_label);
		need_save = true;
	}

	if (!legacy_layout || strcmp(legacy_layout, mtd_layout_label)) {
		env_set("mtd_layout_label", mtd_layout_label);
		need_save = true;
	}

	if (env_get("mtdids")) {
		env_set("mtdids", NULL);
		need_save = true;
	}

	if (env_get("mtdparts")) {
		env_set("mtdparts", NULL);
		need_save = true;
	}

	if (!need_save) {
		mtd_layout_save_pending = false;
		return;
	}

	if (!env_save())
		printf("httpd: saved mtd layout: %s\n", mtd_layout_label);
	else
		printf("Warning: failed to save mtd layout env\n");

	mtd_layout_save_pending = false;
}

static void append_mtdlayout_label(char *buf, size_t size, const char *label)
{
	size_t len;

	if (!label || !label[0])
		return;

	len = strlen(buf);
	if (len >= size - 1)
		return;

	snprintf(buf + len, size - len, "%s;", label);
}

static const char *get_mtdlayout_str(void)
{
	static char mtd_layout_str[MTD_LAYOUTS_MAXLEN];
	const char *custom_parts = env_get(MTD_LAYOUT_CUSTOM_ENV);
	ofnode node, layout;
	bool custom_seen = false;

	snprintf(mtd_layout_str, sizeof(mtd_layout_str), "%s;",
		 get_mtd_layout_label());

	node = ofnode_path("/mtd-layout");
	if (ofnode_valid(node) && ofnode_get_child_count(node)) {
		ofnode_for_each_subnode(layout, node) {
			const char *label = ofnode_read_string(layout, "label");

			if (label && !strcmp(label, MTD_LAYOUT_CUSTOM_LABEL))
				custom_seen = true;
			append_mtdlayout_label(mtd_layout_str,
					       sizeof(mtd_layout_str), label);
		}
	}

	if (custom_parts && custom_parts[0] && !custom_seen)
		append_mtdlayout_label(mtd_layout_str, sizeof(mtd_layout_str),
				       MTD_LAYOUT_CUSTOM_LABEL);

	return mtd_layout_str;
}
#endif

/* ------------------------------------------------------------------ */
/*  Upload handler                                                     */
/* ------------------------------------------------------------------ */

void upload_handler(enum httpd_uri_handler_status status,
			  struct httpd_request *request,
			  struct httpd_response *response)
{
	static char md5_str[33] = "";
	static char resp[128];
	static char fail_resp[288];
	struct httpd_form_value *fw;
#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
	struct httpd_form_value *mtd = NULL;
#endif
	u8 md5_sum[16];
	int i;

	static char hexchars[] = "0123456789abcdef";

	if (status != HTTP_CB_NEW)
		return;

	failsafe_error_reset();

	response->status = HTTP_RESP_STD;
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";

#ifdef CONFIG_MTK_BOOTMENU_MMC
	fw = httpd_request_find_value(request, "gpt");
	if (fw) {
		fw_type = FW_TYPE_GPT;
		goto done;
	}
#endif

	fw = httpd_request_find_value(request, "fip");
	if (fw) {
		fw_type = FW_TYPE_FIP;
		if (failsafe_validate_image(fw->data, fw->size, fw_type))
			goto fail;
		goto done;
	}

	fw = httpd_request_find_value(request, "bl2");
	if (fw) {
		fw_type = FW_TYPE_BL2;
		if (failsafe_validate_image(fw->data, fw->size, fw_type))
			goto fail;
		goto done;
	}

	fw = httpd_request_find_value(request, "firmware");
	if (fw) {
		fw_type = FW_TYPE_FW;
		if (failsafe_validate_image(fw->data, fw->size, fw_type))
			goto fail;
#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
		mtd = httpd_request_find_value(request, "mtd_layout");
#endif
		goto done;
	}

#ifdef CONFIG_WEBUI_FAILSAFE_SIMG
	fw = httpd_request_find_value(request, "simg");
	if (fw) {
		fw_type = FW_TYPE_SIMG;
		if (failsafe_validate_image(fw->data, fw->size, fw_type))
			goto fail;
		goto done;
	}
#endif

#ifdef CONFIG_WEBUI_FAILSAFE_FACTORY
	fw = httpd_request_find_value(request, "factory");
	if (fw) {
		fw_type = FW_TYPE_FACTORY;
		if (failsafe_validate_image(fw->data, fw->size, fw_type))
			goto fail;
		goto done;
	}
#endif

	fw = httpd_request_find_value(request, "initramfs");
	if (fw) {
		/*
		 * Memory-boot image.  Its format (FIT / legacy -> bootm,
		 * raw binary -> go) is detected later by
		 * boot_image_from_mem(), so accept any file here
		 * instead of requiring an FDT header.
		 */
		fw_type = FW_TYPE_INITRD;
		goto done;
	}

fail:
	response->data = failsafe_upgrade_failure(fail_resp,
						  sizeof(fail_resp), "fail");
	response->size = strlen(response->data);
	return;

done:
	/* Nothing to report for this upload (see GET /last-error). */
	failsafe_error_reset();

	upload_data_id = upload_id;
	upload_data = fw->data;
	upload_size = fw->size;
#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
	mtd_layout_label[0] = '\0';
	mtd_layout_save_pending = false;
#endif

	md5_wd((u8 *)fw->data, fw->size, md5_sum, 0);
	for (i = 0; i < 16; i++) {
		u8 hex = (md5_sum[i] >> 4) & 0xf;
		md5_str[i * 2] = hexchars[hex];
		hex = md5_sum[i] & 0xf;
		md5_str[i * 2 + 1] = hexchars[hex];
	}

#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
	if (mtd) {
		snprintf(mtd_layout_label, sizeof(mtd_layout_label),
			 "%s", mtd->data);
		snprintf(resp, sizeof(resp), "%zu %s %s", fw->size, md5_str,
			 mtd_layout_label);
	} else {
		snprintf(resp, sizeof(resp), "%zu %s", fw->size, md5_str);
	}
#else
	snprintf(resp, sizeof(resp), "%zu %s", fw->size, md5_str);
#endif

	response->data = resp;
	response->size = strlen(response->data);

	return;
}

/* ------------------------------------------------------------------ */
/*  Result handler (flashing)                                          */
/* ------------------------------------------------------------------ */

struct flashing_status {
	char buf[4096];
	int ret;
	int body_sent;
};

void result_handler(enum httpd_uri_handler_status status,
			  struct httpd_request *request,
			  struct httpd_response *response)
{
	struct flashing_status *st;
	u32 size;
	static char err_body[288];

	if (status == HTTP_CB_NEW) {
		st = calloc(1, sizeof(*st));
		if (!st) {
			response->info.code = 500;
			return;
		}

		st->ret = -1;

		failsafe_error_reset();

		response->session_data = st;

		response->status = HTTP_RESP_CUSTOM;

		response->info.http_1_0 = 1;
		response->info.content_length = -1;
		response->info.connection_close = 1;
		response->info.content_type = "text/html";
		response->info.code = 200;

		size = http_make_response_header(&response->info,
			st->buf, sizeof(st->buf));

		response->data = st->buf;
		response->size = size;

		return;
	}

	if (status == HTTP_CB_RESPONDING) {
		st = response->session_data;

		if (st->body_sent) {
			response->status = HTTP_RESP_NONE;
			return;
		}

		if (upload_data_id == upload_id) {
#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
			failsafe_prepare_mtd_layout();
			mtd_layout_save_pending = mtd_layout_label[0] != '\0';
#endif
			if (fw_type == FW_TYPE_INITRD)
				st->ret = 0;
			else
				st->ret = failsafe_write_image(upload_data,
							       upload_size, fw_type);
#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
#ifdef CONFIG_MTD
			mtd_layout_switch_end();
#endif
			if (st->ret)
				mtd_layout_save_pending = false;
#endif
		}

		/* invalidate upload identifier */
		upload_data_id = rand();

		if (!st->ret) {
			failsafe_error_reset();
			response->data = "success";
		} else {
			response->data = failsafe_upgrade_failure(err_body,
				sizeof(err_body), "failed");
		}

		response->size = strlen(response->data);

		st->body_sent = 1;

		return;
	}

	if (status == HTTP_CB_CLOSED) {
		st = response->session_data;

		upgrade_success = !st->ret;
		auto_action_pending = upgrade_success &&
			(fw_type == FW_TYPE_INITRD || failsafe_auto_reboot_enabled());

#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
		if (upgrade_success)
			failsafe_save_mtd_layout();
#endif

		free(response->session_data);
	}
}

/* ------------------------------------------------------------------ */
/*  MTD layout handler                                                 */
/* ------------------------------------------------------------------ */

void mtd_layout_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;

#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
	response->data = get_mtdlayout_str();
#else
	{
		const char *custom = env_get(MTD_LAYOUT_CUSTOM_ENV);
		bool mtd_unavailable = false;

#ifdef CONFIG_MTK_BOOTMENU_MMC
		/* When MMC is present, only show MTD layout if there
		 * is genuine MTD hardware or runtime evidence:
		 *  - /mtd-layout OF node exists
		 *  - mtd_layout_custom env is set (user-configured)
		 *  - mtdparts / mtdids env is set (MTD was probed)
		 * Otherwise return an empty body so the frontend
		 * hides the MTD section on MMC-only devices.
		 */
		if (failsafe_mmc_present()) {
			bool has_mtd = false;
			ofnode node = ofnode_path("/mtd-layout");

			if (ofnode_valid(node)) {
				has_mtd = true;
			} else if (custom && custom[0]) {
				has_mtd = true;
			} else {
				const char *mtdparts = env_get("mtdparts");
				const char *mtdids = env_get("mtdids");

				if ((mtdparts && mtdparts[0]) ||
				    (mtdids && mtdids[0]))
					has_mtd = true;
			}

			if (!has_mtd)
				mtd_unavailable = true;
		}
#endif

		if (mtd_unavailable) {
			response->data = "";
		} else if (custom && custom[0]) {
			static char single_str[64];
			const char *cur = env_get("mtd_layout");

			if (!cur || !cur[0])
				cur = env_get("mtd_layout_label");
			if (!cur || strcmp(cur, MTD_LAYOUT_CUSTOM_LABEL))
				cur = "default";

			snprintf(single_str, sizeof(single_str),
				 "%s;default;%s;", cur, MTD_LAYOUT_CUSTOM_LABEL);
			response->data = single_str;
		} else {
			response->data = ";";
		}
	}
#endif

	response->size = strlen(response->data);

	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
}

/* ------------------------------------------------------------------ */
/*  Last-error handler                                                  */
/* ------------------------------------------------------------------ */

/**
 * last_error_handler - GET /last-error
 *
 * Reports the failure of the last upgrade operation as
 * {"code":-19,"error":"..."} - the same information a failed /upload or
 * /result response carries, kept around so that a page (the fail page) can
 * fetch it after the redirect, and so that it can be read with curl
 * instead of attaching a serial console.
 */
void last_error_handler(enum httpd_uri_handler_status status,
			struct httpd_request *request,
			struct httpd_response *response)
{
	char *buf;
	char esc[FAILSAFE_ERROR_MSG_SIZE * 2];

	if (status != HTTP_CB_NEW)
		return;

	buf = malloc(FAILSAFE_ERROR_MSG_SIZE + 64);
	if (!buf) {
		failsafe_http_reply_text(response, 500, "out of memory");
		return;
	}

	json_escape(esc, sizeof(esc), failsafe_error_msg());

	snprintf(buf, FAILSAFE_ERROR_MSG_SIZE + 64,
		 "{\"code\":%d,\"error\":\"%s\"}",
		 failsafe_error_code(), esc);

	failsafe_http_reply_json_alloc(response, 200, buf, buf);
}

/* ------------------------------------------------------------------ */
/*  Public registration function                                       */
/* ------------------------------------------------------------------ */

void upgrade_register_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/upload", &upload_handler, NULL);
	httpd_register_uri_handler(inst, "/last-error", &last_error_handler,
				   NULL);
	httpd_register_uri_handler(inst, "/result", &result_handler, NULL);
	httpd_register_uri_handler(inst, "/getmtdlayout", &mtd_layout_handler,
				   NULL);
}
