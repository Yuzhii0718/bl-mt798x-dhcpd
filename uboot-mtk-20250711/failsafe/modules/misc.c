/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 *
 * Failsafe misc module
 * Handles version, sysinfo, reboot, and basic UI (index, not_found, css, js, html)
 */

#include <command.h>
#include <env.h>
#include <malloc.h>
#include <net/mtk_httpd.h>
#include <net/mtk_tcp.h>
#include <linux/string.h>
#include <dm/ofnode.h>
#include <vsprintf.h>
#include <version_string.h>

#ifdef CONFIG_MTD
#include "../../board/mediatek/common/mtd_helper.h"
#endif
#ifdef CONFIG_MTK_BOOTMENU_MMC
#include "../../board/mediatek/common/mmc_helper.h"
#endif
#ifdef CONFIG_PARTITIONS
#include <part.h>
#endif

#ifdef CONFIG_MTD_LAYOUT_SPI_NAND
extern const char *mtd_layout_spi_nand_replace(const char *str, char *buf,
					       size_t bufsz);
#endif

#include <failsafe/fs.h>
#include <failsafe/internal.h>

/* ------------------------------------------------------------------ */
/*  Version handler                                                    */
/* ------------------------------------------------------------------ */

#ifndef WEBUI_FAILSAFE_GIT_HASH
#define WEBUI_FAILSAFE_GIT_HASH "unknown"
#endif

#ifndef WEBUI_FAILSAFE_GIT_DIRTY
#define WEBUI_FAILSAFE_GIT_DIRTY 0
#endif

void version_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	const char *build_variant;
	const char *git_hash = WEBUI_FAILSAFE_GIT_HASH;
	static char version_buf[512];
	bool dirty = !!WEBUI_FAILSAFE_GIT_DIRTY;

	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;

	build_variant = CONFIG_WEBUI_FAILSAFE_BUILD_VARIANT;
	if (!git_hash || !git_hash[0])
		git_hash = "unknown";

	if (build_variant && build_variant[0]) {
		snprintf(version_buf, sizeof(version_buf),
			 "%s %s%s %s",
			 version_string, git_hash, dirty ? "-dirty" : "",
			 build_variant);
		response->data = version_buf;
	} else {
		snprintf(version_buf, sizeof(version_buf),
			 "%s %s%s",
			 version_string, git_hash, dirty ? "-dirty" : "");
		response->data = version_buf;
	}
	response->size = strlen(response->data);

	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
}

/* ------------------------------------------------------------------ */
/*  sysinfo sub-functions                                              */
/* ------------------------------------------------------------------ */

#define MTD_LAYOUT_CUSTOM_LABEL	"custom"
#define MTD_LAYOUT_CUSTOM_ENV	"mtd_layout_custom"

static int sysinfo_json_append_board(char *buf, int len, int left)
{
	ofnode root;
	const char *board_model = NULL;
	const char *board_compat = NULL;
	const char *build_variant = NULL;
	off_t ram_size = 0;
	char esc_board_model[256], esc_board_compat[256], esc_build_variant[256];

	root = ofnode_path("/");
	if (ofnode_valid(root)) {
		board_model = ofnode_read_string(root, "model");
		board_compat = ofnode_read_string(root, "compatible");
	}

	if (!board_model || !board_model[0]) {
		board_model = env_get("model");
		if (!board_model || !board_model[0])
			board_model = env_get("board_name");
		if (!board_model || !board_model[0])
			board_model = env_get("board");
	}

	if (gd)
		ram_size = (off_t)gd->ram_size;

	build_variant = CONFIG_WEBUI_FAILSAFE_BUILD_VARIANT;
	if (build_variant && !build_variant[0])
		build_variant = NULL;

	json_escape(esc_board_model, sizeof(esc_board_model), board_model ? board_model : "");
	json_escape(esc_board_compat, sizeof(esc_board_compat), board_compat ? board_compat : "");
	json_escape(esc_build_variant, sizeof(esc_build_variant), build_variant ? build_variant : "");

	len = buf_appendf(buf, left, len,
		"\"board\":{\"model\":\"%s\",\"compatible\":\"%s\"},",
		esc_board_model, esc_board_compat);
	len = buf_appendf(buf, left, len,
		"\"ram\":{\"size\":%llu},",
		(unsigned long long)ram_size);
	len = buf_appendf(buf, left, len,
		"\"build_variant\":\"%s\",",
		esc_build_variant);

	return len;
}

#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
extern const char *get_mtd_layout_label(void);

static int sysinfo_json_append_mtd_layout(char *buf, int len, int left)
{
	const char *cur = get_mtd_layout_label();
	const char *custom_parts = env_get(MTD_LAYOUT_CUSTOM_ENV);
	char esc_cur[128];
	const char *cur_parts = NULL;
	char esc_cur_parts[512];
	ofnode node, layout;
	bool first = true;
	bool custom_seen = false;

	json_escape(esc_cur, sizeof(esc_cur), cur ? cur : "");
	len = buf_appendf(buf, left, len,
			"\"mtd_layout\":{\"current\":\"%s\",",
			esc_cur);

	node = ofnode_path("/mtd-layout");
	if (ofnode_valid(node) && ofnode_get_child_count(node)) {
		len = buf_appendf(buf, left, len, "\"layouts\":[");
		ofnode_for_each_subnode(layout, node) {
			const char *label = ofnode_read_string(layout, "label");
			const char *parts = ofnode_read_string(layout, "mtdparts");
#ifdef CONFIG_MTD_LAYOUT_SPI_NAND
			{
				char parts_buf[512];
				parts = mtd_layout_spi_nand_replace(parts, parts_buf,
								   sizeof(parts_buf));
			}
#endif
			char esc_label[128];
			char esc_parts[512];

			if (!label)
				continue;
			if (!strcmp(label, MTD_LAYOUT_CUSTOM_LABEL)) {
				custom_seen = true;
				if (custom_parts && custom_parts[0])
					parts = custom_parts;
			}
			json_escape(esc_label, sizeof(esc_label), label);
			json_escape(esc_parts, sizeof(esc_parts), parts ? parts : "");
			if (cur && !strcmp(label, cur))
				cur_parts = parts;
			len = buf_appendf(buf, left, len,
				"%s{\"label\":\"%s\",\"parts\":\"%s\"}",
				first ? "" : ",", esc_label, esc_parts);
			first = false;
		}
		if (custom_parts && custom_parts[0] && !custom_seen) {
			char esc_parts[512];

			json_escape(esc_parts, sizeof(esc_parts), custom_parts);
			if (cur && !strcmp(cur, MTD_LAYOUT_CUSTOM_LABEL))
				cur_parts = custom_parts;
			len = buf_appendf(buf, left, len,
				"%s{\"label\":\"%s\",\"parts\":\"%s\"}",
				first ? "" : ",", MTD_LAYOUT_CUSTOM_LABEL,
				esc_parts);
		}
		len = buf_appendf(buf, left, len, "],");
	} else {
		if (custom_parts && custom_parts[0]) {
			char esc_parts[512];

			json_escape(esc_parts, sizeof(esc_parts), custom_parts);
			if (cur && !strcmp(cur, MTD_LAYOUT_CUSTOM_LABEL))
				cur_parts = custom_parts;
			len = buf_appendf(buf, left, len,
				"\"layouts\":[{\"label\":\"%s\",\"parts\":\"%s\"}],",
				MTD_LAYOUT_CUSTOM_LABEL, esc_parts);
		} else {
			len = buf_appendf(buf, left, len, "\"layouts\":[],");
		}
	}

	json_escape(esc_cur_parts, sizeof(esc_cur_parts), cur_parts ? cur_parts : "");
	len = buf_appendf(buf, left, len,
			"\"current_parts\":\"%s\"},",
			esc_cur_parts);

	return len;
}
#endif

static int sysinfo_json_append_mmc(char *buf, int len, int left)
{
	len = buf_appendf(buf, left, len, "\"mmc\":{");
#ifdef CONFIG_MTK_BOOTMENU_MMC
	{
		struct mmc *mmc;
		struct blk_desc *bd;
		bool present;
		char esc_vendor[128], esc_product[128];

		mmc = _mmc_get_dev(CONFIG_MTK_BOOTMENU_MMC_DEV_INDEX, 0, false);
		bd = mmc ? mmc_get_blk_desc(mmc) : NULL;
		present = mmc && bd && bd->type != DEV_TYPE_UNKNOWN;

		if (present) {
			char pretty_vendor[256];

			failsafe_mmc_vendor_pretty(bd->vendor ? bd->vendor : "",
						   pretty_vendor, sizeof(pretty_vendor));
			json_escape(esc_vendor, sizeof(esc_vendor), pretty_vendor);
			json_escape(esc_product, sizeof(esc_product), bd->product ? bd->product : "");
			len = buf_appendf(buf, left, len,
				"\"present\":true,\"vendor\":\"%s\",\"product\":\"%s\",\"blksz\":%lu,\"size\":%llu,",
				esc_vendor, esc_product, (unsigned long)bd->blksz,
				(unsigned long long)mmc->capacity_user);
		} else {
			len = buf_appendf(buf, left, len, "\"present\":false,");
		}

		len = buf_appendf(buf, left, len, "\"parts\":[");
#ifdef CONFIG_PARTITIONS
		if (present) {
			struct disk_partition dpart;
			u32 i = 1;
			bool first = true;

			part_init(bd);
			while (len < left - 128) {
				if (part_get_info(bd, i, &dpart))
					break;

				if (!dpart.name[0]) {
					i++;
					continue;
				}

				len = buf_appendf(buf, left, len,
					"%s{\"name\":\"%s\",\"start\":%llu,\"size\":%llu}",
					first ? "" : ",",
					dpart.name,
					(unsigned long long)dpart.start * dpart.blksz,
					(unsigned long long)dpart.size * dpart.blksz);

				first = false;
				i++;
			}
		}
#endif
		len = buf_appendf(buf, left, len, "]");
	}
#else
	len = buf_appendf(buf, left, len, "\"present\":false,\"parts\":[]");
#endif
	len = buf_appendf(buf, left, len, "}");

	return len;
}

void sysinfo_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *buf;
	int len = 0;
	int left = 8192;

	(void)request;

	if (status == HTTP_CB_CLOSED) {
		free(response->session_data);
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	buf = malloc(left);
	if (!buf) {
		failsafe_http_reply_json(response, 500, "{}");
		return;
	}

	len = buf_appendf(buf, left, len, "{");

	/* board + RAM + build_variant */
	len = sysinfo_json_append_board(buf, len, left);

	/* storage section */
	len = buf_appendf(buf, left, len, "\"storage\":{");

#ifdef CONFIG_MEDIATEK_MULTI_MTD_LAYOUT
	len = sysinfo_json_append_mtd_layout(buf, len, left);
#else
	len = buf_appendf(buf, left, len, "\"mtd_layout\":null,");
#endif

	/* MMC info */
	len = sysinfo_json_append_mmc(buf, len, left);

	len = buf_appendf(buf, left, len, "}");
	len = buf_appendf(buf, left, len, "}");

	failsafe_http_reply_json_alloc(response, 200, buf, buf);
}

/* ------------------------------------------------------------------ */
/*  Reboot handlers                                                    */
/* ------------------------------------------------------------------ */

struct reboot_session {
	bool do_reboot;
};

/*
 * Set once a /reboot or /reboot-failsafe request has been answered and its
 * connection has been fully closed.  The reset itself is performed by
 * do_httpd() after the poll loop has exited.
 *
 * Calling do_reset() here would run it -- and mtk_tcp_close_all_conn() --
 * from inside the TCP callback, i.e. inside the eth_rx() → connection
 * teardown chain.  The teardown of the very connection running this
 * callback then re-entered the callback, which recursed until the stack
 * blew up, printing its "Rebooting now" line over and over without ever
 * reaching do_reset().
 */
bool reboot_pending;

/*
 * Consume the reboot session of a closed connection.
 *
 * session_data is detached before it is freed: the connection teardown
 * path can deliver HTTP_CB_CLOSED more than once for the same session,
 * and reading or freeing an already released session turned the reboot
 * into an unbounded recursion.
 */
static bool reboot_session_take(struct httpd_response *response)
{
	struct reboot_session *st = response->session_data;
	bool do_reboot = false;

	if (st) {
		do_reboot = st->do_reboot;
		response->session_data = NULL;
		free(st);
	}

	return do_reboot;
}

void reboot_handler(enum httpd_uri_handler_status status,
			   struct httpd_request *request,
			   struct httpd_response *response)
{
	struct reboot_session *st;

	if (status == HTTP_CB_NEW) {
		st = calloc(1, sizeof(*st));
		if (!st) {
			response->info.code = 500;
			return;
		}

		st->do_reboot = true;

		response->session_data = st;
		response->status = HTTP_RESP_STD;
		response->data = "rebooting";
		response->size = strlen(response->data);
		response->info.code = 200;
		response->info.connection_close = 1;
		response->info.content_type = "text/plain";
		return;
	}

	if (status == HTTP_CB_CLOSED) {
		/*
		 * Only record the request.  do_httpd() performs the reset
		 * after the poll loop has exited and the network has been
		 * halted, i.e. safely outside the TCP callback chain.
		 */
		if (reboot_session_take(response))
			reboot_pending = true;
	}
}

void reboot_failsafe_handler(enum httpd_uri_handler_status status,
				   struct httpd_request *request,
				   struct httpd_response *response)
{
	struct reboot_session *st;
	int ret;

	if (status == HTTP_CB_NEW) {
		ret = env_set("failsafe", "1");
		if (!ret)
			ret = env_save();

		if (ret) {
			response->status = HTTP_RESP_STD;
			response->data = "failsafe env set failed";
			response->size = strlen(response->data);
			response->info.code = 500;
			response->info.connection_close = 1;
			response->info.content_type = "text/plain";
			return;
		}

		st = calloc(1, sizeof(*st));
		if (!st) {
			response->info.code = 500;
			return;
		}

		st->do_reboot = true;
		response->session_data = st;
		response->status = HTTP_RESP_STD;
		response->data = "rebooting to failsafe";
		response->size = strlen(response->data);
		response->info.code = 200;
		response->info.connection_close = 1;
		response->info.content_type = "text/plain";
		return;
	}

	if (status == HTTP_CB_CLOSED) {
		if (reboot_session_take(response))
			reboot_pending = true;
	}
}

/* ------------------------------------------------------------------ */
/*  UI handlers (index, not_found, style, js, html)                    */
/* ------------------------------------------------------------------ */

void not_found_handler(enum httpd_uri_handler_status status,
			      struct httpd_request *request,
			      struct httpd_response *response)
{
	if (status == HTTP_CB_NEW) {
		failsafe_output_file(response, "404.html", "text/html");
		response->info.code = 404;
	}
}

void index_handler(enum httpd_uri_handler_status status,
			  struct httpd_request *request,
			  struct httpd_response *response)
{
	if (status == HTTP_CB_NEW) {
		if (failsafe_output_file(response, "index.html", "text/html"))
			not_found_handler(status, request, response);
	}
}

void style_handler(enum httpd_uri_handler_status status,
			  struct httpd_request *request,
			  struct httpd_response *response)
{
	if (status == HTTP_CB_NEW) {
		if (failsafe_output_file(response, "style.css", "text/css")) {
			not_found_handler(status, request, response);
			return;
		}
	}
}

/*
 * Select JS file name from request URI. If the basename matches a known
 * JavaScript filename, return it; otherwise fall back to "main.js".
 */
static const char *select_js_file(const char *uri)
{
	static const char *allowed[] = {
		"main.js",
		"i18n.js",
		"theme.js",
		"backup_js.js",
		"console_js.js",
		"env_js.js",
		"flash_js.js",
		"settings_js.js",
		"ubi_js.js",
		NULL
	};
	const char *basename;
	const char *slash_ptr;
	size_t basename_len;
	int allowed_index;

	if (!uri || !uri[0])
		return "main.js";

	slash_ptr = strrchr(uri, '/');
	basename = slash_ptr ? slash_ptr + 1 : uri;

	/* strip query/hash if present */
	{
		const char *query_ptr = strchr(basename, '?');
		const char *hash_ptr = strchr(basename, '#');
		const char *end_ptr = basename + strlen(basename);

		if (query_ptr && query_ptr < end_ptr)
			end_ptr = query_ptr;
		if (hash_ptr && hash_ptr < end_ptr)
			end_ptr = hash_ptr;

		basename_len = end_ptr - basename;
	}
	if (basename_len == 0)
		return "main.js";

	for (allowed_index = 0; allowed[allowed_index]; allowed_index++) {
		if (strlen(allowed[allowed_index]) == basename_len &&
			strncmp(allowed[allowed_index], basename, basename_len) == 0)
			return allowed[allowed_index];
	}

	return "main.js";
}

void js_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	if (status == HTTP_CB_NEW) {
		const char *uri = request && request->urih ? request->urih->uri : NULL;
		const char *file = select_js_file(uri);

		if (failsafe_output_file(response, file, "text/javascript")) {
			/* requested JS not embedded: serve 404 page instead of
			 * a plain-text error masquerading as gzip-encoded JS */
			not_found_handler(status, request, response);
			return;
		}
	}
}

void html_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	if (status != HTTP_CB_NEW)
		return;

	if (failsafe_output_file(response, request->urih->uri + 1, "text/html"))
		not_found_handler(status, request, response);
}

/* ------------------------------------------------------------------ */
/*  Public registration function                                       */
/* ------------------------------------------------------------------ */

void misc_register_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/", &index_handler, NULL);
	httpd_register_uri_handler(inst, "/bl2.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/booting.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/cgi-bin/luci", &index_handler, NULL);
	httpd_register_uri_handler(inst, "/cgi-bin/luci/", &index_handler, NULL);
	httpd_register_uri_handler(inst, "/fail.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/flashing.html", &html_handler, NULL);
#ifdef CONFIG_WEBUI_FAILSAFE_GPT
	httpd_register_uri_handler(inst, "/gpt.html", &html_handler, NULL);
#endif
	httpd_register_uri_handler(inst, "/initramfs.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/main.js", &js_handler, NULL);
	httpd_register_uri_handler(inst, "/style.css", &style_handler, NULL);
	httpd_register_uri_handler(inst, "/uboot.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/version", &version_handler, NULL);
	httpd_register_uri_handler(inst, "", &not_found_handler, NULL);
	httpd_register_uri_handler(inst, "/reboot", &reboot_handler, NULL);
	httpd_register_uri_handler(inst, "/reboot-failsafe", &reboot_failsafe_handler, NULL);
	httpd_register_uri_handler(inst, "/reboot.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/sysinfo", &sysinfo_handler, NULL);
#ifdef CONFIG_WEBUI_FAILSAFE_I18N
	httpd_register_uri_handler(inst, "/i18n.js", &js_handler, NULL);
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_SIMG
	httpd_register_uri_handler(inst, "/simg.html", &html_handler, NULL);
#endif
#ifdef CONFIG_WEBUI_FAILSAFE_FACTORY
	httpd_register_uri_handler(inst, "/factory.html", &html_handler, NULL);
#endif
}
