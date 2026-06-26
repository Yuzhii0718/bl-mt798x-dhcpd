// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 */

#include "bootmenu_common.h"
#include "autoboot_helper.h"
#include "mtd_helper.h"
#include "mmc_helper.h"

#include <env.h>
#include <errno.h>

static int write_factory(void *priv, const struct data_part_entry *dpe,
			 const void *data, size_t size)
{
	const char *partname = get_rf_part_name();
	int ret;

	ret = write_mtd_part(partname, data, size, true);
	if (!ret)
		return 0;

	if (ret != -ENODEV)
		return ret;

	return write_mmc_part(partname, data, size, true);
}

static const struct data_part_entry snor_emmc_parts[] = {
	{
		.name = "ATF BL2",
		.abbr = "bl2",
		.env_name = "bootfile.bl2",
		.validate = generic_validate_bl2,
		.write = generic_mtd_write_bl2,
	},
	{
		.name = "ATF FIP",
		.abbr = "fip",
		.env_name = "bootfile.fip",
		.validate = generic_validate_fip,
#if defined(CONFIG_FIP_IN_SPI_NOR)
		.write = generic_mtd_write_fip,
#elif defined(CONFIG_FIP_IN_EMMC)
		.write = generic_mmc_write_fip,
#endif
		.post_action = UPGRADE_ACTION_CUSTOM,
		//.do_post_action = generic_invalidate_env,
	},
#ifdef CONFIG_MTK_FIP_SUPPORT
	{
		.name = "BL31 of ATF FIP",
		.abbr = "bl31",
		.env_name = "bootfile.bl31",
		.validate = generic_validate_bl31,
#if defined(CONFIG_FIP_IN_SPI_NOR)
		.write = generic_mtd_update_bl31,
#elif defined(CONFIG_FIP_IN_EMMC)
		.write = generic_mmc_update_bl31,
#endif
		.post_action = UPGRADE_ACTION_CUSTOM,
	},
	{
		.name = "BL33 of ATF FIP",
		.abbr = "bl33",
		.env_name = "bootfile.bl33",
		.validate = generic_validate_bl33,
#if defined(CONFIG_FIP_IN_SPI_NOR)
		.write = generic_mtd_update_bl33,
#elif defined(CONFIG_FIP_IN_EMMC)
		.write = generic_mmc_update_bl33,
#endif
		.post_action = UPGRADE_ACTION_CUSTOM,
		//.do_post_action = generic_invalidate_env,
	},
#endif
#ifdef CONFIG_MTK_CHAINLOAD_BL
	{
		.name = "Next stage bootloader",
		.abbr = "nextbl",
		.env_name = "bootfile.nextbl",
		.validate = generic_validate_next_bl,
#ifdef CONFIG_MTK_NEXT_BL_IN_EMMC
		.write = generic_mmc_write_next_bl,
#else
		.write = generic_mtd_write_next_bl,
#endif
	},
#endif
	{
		.name = "Firmware",
		.abbr = "fw",
		.env_name = "bootfile",
		.post_action = UPGRADE_ACTION_BOOT,
#ifdef CONFIG_MTK_SNOR_MMC_FALLBACK_BOOT
		.validate = generic_mtd_validate_fw,
		.write = generic_mtd_write_fw,
#else
		.validate = generic_mmc_validate_fw,
		.write = generic_mmc_write_fw,
#endif
	},
#ifdef CONFIG_MTK_SNOR_MMC_FALLBACK_BOOT
	{
		.name = "Firmware (MMC)",
		.abbr = "fw-mmc",
		.env_name = "bootfile.mmc",
		.post_action = UPGRADE_ACTION_BOOT,
		.validate = generic_mmc_validate_fw,
		.write = generic_mmc_write_fw,
	},
#endif
	{
		.name = "Factory",
		.abbr = "factory",
		.env_name = "bootfile.factory",
		.write = write_factory,
	},
	{
		.name = "Single image (SPI-NOR)",
		.abbr = "simg-snor",
		.env_name = "bootfile.simg-snor",
		.write = generic_mtd_write_simg,
	},
	{
		.name = "Single image (eMMC)",
		.abbr = "simg-emmc",
		.env_name = "bootfile.simg-emmc",
		.write = generic_mmc_write_simg,
	},
	{
		.name = "Partition table",
		.abbr = "gpt",
		.env_name = "bootfile.gpt",
		.write = generic_mmc_write_gpt,
	}
};

void board_upgrade_data_parts(const struct data_part_entry **dpes, u32 *count)
{
	*dpes = snor_emmc_parts;
	*count = ARRAY_SIZE(snor_emmc_parts);
}

int board_boot_default(bool do_boot)
{
	if (IS_ENABLED(CONFIG_MTK_SNOR_MMC_FALLBACK_BOOT)) {
		const char *boot_from_sd_str = env_get("boot_from_sd");
		u64 fit_offset = CONFIG_MTK_SNOR_MMC_FALLBACK_FIT_OFFSET;
		int ret;

		printf("SNOR+MMC: boot_from_sd=%s, trying raw MMC FIT @ 0x%llx first\n",
		       boot_from_sd_str ? boot_from_sd_str : "<unset>",
		       fit_offset);

		ret = boot_from_mmc_offset(0, 0, fit_offset, do_boot);
		if (!ret)
			return 0;

		ret = boot_from_mmc_offset(0, 0, fit_offset, do_boot);
		if (!ret)
			return 0;

		printf("Failed to boot from SD(%d), try to boot from flash...\n", ret);

		return boot_from_mtd_partition("firmware", do_boot);
	}

	return generic_mmc_boot_image(do_boot);
}

#ifdef CONFIG_MTK_CHAINLOAD_BL
int board_chainload_default(bool do_boot)
{
#ifdef CONFIG_MTK_NEXT_BL_IN_EMMC
	return generic_mmc_boot_next_bl(do_boot);
#else
	return generic_mtd_boot_next_bl(do_boot);
#endif
}
#endif

static const struct bootmenu_entry snor_emmc_bootmenu_entries[] = {
#ifdef CONFIG_MTK_AUTO_CHAINLOAD_BL
	{
		.desc = "Chainload next-stage bootloader (Default)",
		.cmd = "mtkchainload"
	},
	{
		.desc = "Startup system",
		.cmd = "mtkboardboot"
	},
#else
	{
		.desc = "Startup system (Default)",
		.cmd = "mtkboardboot"
	},
#endif
	{
		.desc = "Upgrade firmware",
		.cmd = "mtkupgrade fw"
	},
#ifdef CONFIG_MTK_SNOR_MMC_FALLBACK_BOOT
	{
		.desc = "Upgrade firmware (MMC)",
		.cmd = "mtkupgrade fw-mmc"
	},
#endif
	{
		.desc = "Upgrade ATF BL2",
		.cmd = "mtkupgrade bl2"
	},
	{
		.desc = "Upgrade ATF FIP",
		.cmd = "mtkupgrade fip"
	},
#ifdef CONFIG_MTK_FIP_SUPPORT
	{
		.desc = "  Upgrade ATF BL31 only",
		.cmd = "mtkupgrade bl31"
	},
	{
		.desc = "  Upgrade bootloader only",
		.cmd = "mtkupgrade bl33"
	},
#endif
	{
		.desc = "Upgrade partition table",
		.cmd = "mtkupgrade gpt"
	},
	{
		.desc = "Upgrade single image (SPI-NOR)",
		.cmd = "mtkupgrade simg-snor"
	},
	{
		.desc = "Upgrade single image (eMMC)",
		.cmd = "mtkupgrade simg-emmc"
	},
#ifdef CONFIG_MTK_CHAINLOAD_BL
	{
		.desc = "Upgrade next-stage bootloader",
		.cmd = "mtkupgrade nextbl"
	},
#ifndef CONFIG_MTK_AUTO_CHAINLOAD_BL
	{
		.desc = "Chainload next-stage bootloader",
		.cmd = "mtkchainload"
	},
#endif
#endif
	{
		.desc = "Load image",
		.cmd = "mtkload"
	},
#ifdef CONFIG_MTK_WEB_FAILSAFE
	{
		.desc = "Start Web failsafe",
		.cmd = "httpd"
	},
#endif
	{
		.desc = "Change boot configuration",
		.cmd = "mtkbootconf"
	},
};

void board_bootmenu_entries(const struct bootmenu_entry **menu, u32 *count)
{
	*menu = snor_emmc_bootmenu_entries;
	*count = ARRAY_SIZE(snor_emmc_bootmenu_entries);
}

void default_boot_set_defaults(void *fdt)
{
	mmc_boot_set_defaults(fdt);
}
