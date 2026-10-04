# Netis NX62 / Netcore N60 Pro (MT7986A)

MT7986A, DDR4 512 MB / 1 GB / 2 GB, 128 MB SPI-NAND (2 KB page, 128 KB block).
The Netis NX62 is the international version of the Netcore N60 Pro.

## Build

```bash
BOARD=netis_nx62 MULTI_LAYOUT=1 ./build.sh               # ATF 2025
BOARD=netis_nx62 MULTI_LAYOUT=1 VERSION=SP2 ./build.sh   # ATF 2026
```

Flash **both** `bl2.img` (partition `bl2`) and `fip.bin` (partition `fip`).

## No NMBM

Unlike `netcore_n60-pro`, the default variant of `netis_nx62` does **not** use
NMBM: BL2 skips bad blocks (`_NAND_SKIP_BAD`) and U-Boot works on the raw
`spi-nand0`, like the official OpenWrt bootloader.

The stock firmware keeps its NMBM management tables in the last 8 MB of the
flash, while official OpenWrt uses the flash up to the end for UBI. An NMBM
bootloader creates the NMBM tables on every boot if they are missing, i.e. on
top of the OpenWrt UBI blocks. Without NMBM in the bootloader both firmwares
can coexist.

## MTD layouts

| Layout | Firmware | `ubi` |
| --- | --- | --- |
| `default` | stock firmware, stock-layout NMBM builds (Kwrt, ImmortalWrt mt798x, …) | `117248k` (0x580000–0x7800000) |
| `openwrt` | official OpenWrt 25.12+, ImmortalWrt 24.10+ (`netcore_n60-pro`) | `125440k` (0x580000–0x8000000) |

Both layouts start with `1024k(bl2),512k(u-boot-env),2048k(factory),2048k(fip)`.
The `default` layout never touches the NMBM area, so the stock kernel keeps
its NMBM tables.

## TRNG

`_MT7986_TRNG_NS_ACCESS` is enabled: the stock MediaTek SDK kernel 5.4 reads
the TRNG registers directly, official OpenWrt uses the `MTK_SIP_TRNG_GET_RND`
SMC; both work.

## Notes

- Back up `bl2`, `u-boot-env`, `factory` and `fip` (better: the whole flash)
  before flashing. `factory` holds the Wi-Fi calibration and MAC addresses.
- If the router runs stock or an NMBM build, make sure NMBM has not remapped
  any blocks before switching to this bootloader.
- Going back from `openwrt` to stock needs a full flash backup taken on stock:
  after OpenWrt the NMBM tables are gone.
- The 512 MB NAND version (Chinese market) is not supported.
