// SPDX-License-Identifier: GPL-2.0
/*
 * WODPOSIT SPI NAND driver By oldcat 20260505
 *
 * SPI NAND flash driver for Wodposit (沃存) devices.
 * Based on foresee driver by Grandstream Networks, Inc.
 */

#ifndef __UBOOT__
#include <linux/device.h>
#include <linux/kernel.h>
#endif
#include <linux/mtd/spinand.h>

/* Wodposit manufacturer ID – placeholder, actual ID needs confirmation */
#define SPINAND_MFR_WODPOSIT		0xA5

/* Common read/write operation variants (same as foresee) */
static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 2, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_DUALIO_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_X4(true, 0, NULL, 0),
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_X4(false, 0, NULL, 0),
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

/*
 * OOB layout for chips with on-die ECC (no separate ECC area in OOB).
 * First 2 bytes are typically used for bad block marker.
 */
static int wodposit_ooblayout_ecc(struct mtd_info *mtd, int section,
				  struct mtd_oob_region *region)
{
	/* No explicit ECC region – hardware handles it internally */
	return -ERANGE;
}

static int wodposit_ooblayout_free(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	region->offset = 2;
	region->length = mtd->oobsize - 2;
	return 0;
}

static const struct mtd_ooblayout_ops wodposit_ooblayout = {
	.ecc = wodposit_ooblayout_ecc,
	.rfree = wodposit_ooblayout_free,
};

/*
 * ECC status extraction – placeholder.
 * For on-die ECC, usually returns 0 if no error, >0 for corrected bits.
 * Actual implementation may read status register bits.
 */
static int wodposit_ecc_get_status(struct spinand_device *spinand, u8 status)
{
	/* Default: assume no error or already corrected */
	return 0;
}

/*
 * Chip table – currently includes the observed WPS3NS01W model.
 * Additional models can be added based on datasheets.
 *
 * Parameter assumptions for WPS3NS01W:
 *   - 1 Gbit (128 MB) density
 *   - Page size: 2048 bytes, OOB size: 64 bytes
 *   - 1024 blocks, each has 64 pages
 *   - On-die ECC, 4-bit per 512 bytes (common for 1Gb SPI NAND)
 *   - Device ID 0x01 is a placeholder; replace with actual READID value.
 */
static const struct spinand_info wodposit_spinand_table[] = {
	SPINAND_INFO("WPS3NS01W",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xA0),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&wodposit_ooblayout,
				     wodposit_ecc_get_status)),
	/* Example: 2Gbit variant (model might be WPS3NS02W) – uncomment when needed */
	/*
	SPINAND_INFO("WPS3NS02W",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x02),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&wodposit_ooblayout,
				     wodposit_ecc_get_status)),
	*/
};

static const struct spinand_manufacturer_ops wodposit_spinand_manuf_ops = {
	/* No custom ops needed for now */
};

const struct spinand_manufacturer wodposit_spinand_manufacturer = {
	.id = SPINAND_MFR_WODPOSIT,
	.name = "wodposit",
	.chips = wodposit_spinand_table,
	.nchips = ARRAY_SIZE(wodposit_spinand_table),
	.ops = &wodposit_spinand_manuf_ops,
};
