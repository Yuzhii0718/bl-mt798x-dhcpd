[English](#) | [简体中文](./sc/uart-recovery.md)

# UART BootROM Recovery (MT7987 / Cudy TR3600)

How to recover a device whose FIP (BL31 + U-Boot) is broken but whose BL2 is still intact.

## 1. When you need this

The web failsafe, the DHCP server and the TFTP client all live inside **U-Boot**, which is stored in the
FIP partition. If the FIP is corrupted, all of them are gone with it. What you get instead is BL2
printing something like:

```
BL2: Failed to load image id 5 (-2)
```

and then hanging. At that point nothing on the flash can help you — you have to go one layer below BL2,
back to the SoC BootROM.

## 2. Why BL2 itself cannot recover the device

Three candidate fallbacks, all ruled out:

| Candidate | Verdict |
| --- | --- |
| Network stack in BL2 | **Does not exist.** The BL2 binary contains no `tftp` / `ethernet` / `arp` / `dhcp` / `mdio` / `gmac` code at all. BL2 only knows SPI-NAND reads, NMBM, DRAM init and UART output. |
| UART download in the flash-resident BL2 | **Not available.** `_RAM_BOOT_RAM_BOOT_UART_DL` in `plat/mediatek/apsoc_common/bl2/Config-uart_dl.in` is gated by `depends on _BOOT_DEVICE_RAM`. It only applies to a BL2 that the BootROM itself loaded into SRAM — not to the one sitting in flash. |
| Dual-FIP | **Not applicable to this layout.** `_DUAL_FIP` defaults to `y` only for `(_BOOT_DEVICE_SPIM_NAND && _NAND_UBI)` or eMMC. TR3600 uses SPIM-NAND with *fixed partitions*, and the `_FIP_IN_*` / `_FIP2_IN_*` location choices are all `depends on _BOOT_DEVICE_EMMC`. There is simply nowhere to put a second FIP without changing the partition table, which would break stock compatibility. |

The way out is to make the BootROM drop into its UART download mode and push a fresh BL2 + FIP into RAM
over the serial line, using [981213/mtk_uartboot](https://github.com/981213/mtk_uartboot).

## 3. What you need

- A USB-TTL serial adapter wired to the board's UART (3.3 V, 115200 8N1).
- `mtk_uartboot` (Rust, needs `cargo`).
- A **RAM-boot BL2** built with UART download support — see below, or use the prebuilt
  `output/bl2-mt7987_ram_boot_uartdl.bin`.
- A known-good FIP, e.g. `output/fip-mt7987_cudy_tr3600_2025-*.bin`.

## 4. Building the RAM-boot BL2

The config lives in the shared ATF configs directory (`atf-20250711/configs` is a symlink to
`atf-20240117-bacca82a8/configs`):

`configs/mt7987_ram_boot_defconfig`

```
_PLAT_MT7987=y
_DDR4_FREQ_2666=y
_BOOT_DEVICE_RAM=y
_RAM_BOOT_RAM_BOOT_UART_DL=y
_DRAM_DEBUG_LOG=y
```

This pins the board-matched DDR4 rate to 2666 MT/s and resolves to `BOOT_DEVICE="ram"` +
`RAM_BOOT_UART_DL=1`, which selects the `BL2_BOOT_RAM` rule in
`bl2_image.mk` and pulls in `bl2_boot_ram.c`, `uart_dl.c` and `hsuart-extra.c`, plus
`ENABLE_CONSOLE_GETC`.

```sh
cd atf-20250711
make mt7987_ram_boot_defconfig
make bl2 -j$(nproc) \
  CROSS_COMPILE=aarch64-linux-gnu- CROSS_COMPILER=aarch64-linux-gnu- \
  CONFIG_CROSS_COMPILER=aarch64-linux-gnu- \
  DTC=../uboot-mtk-20250711/scripts/dtc/dtc
```

`DTC=` is required because TF-A needs a `dtc` binary and most distros do not ship one by default; the
U-Boot tree already contains a usable copy.

The result is `build/mt7987/release/**bl2.bin**` (note: `bl2.bin`, *not* `bl2.img` — this one carries no
`SPINAND!` BootROM header, because it is delivered over UART rather than read from flash).

Sanity checks on the produced file:

| Check | Expected |
| --- | --- |
| Entry point | `0x201000` — matches `mtk_uartboot`'s default `--load-addr`, so no `-l` needed |
| First bytes | `f4 03 00 aa` (`mov x20, x0`, the AArch64 BL2 entrypoint), **no** `SPINAND!` magic |
| Strings | must contain `Starting UART download handshake ...` |

Remember to restore the normal board config afterwards, otherwise the next `build.sh` run starts from
the RAM-boot config:

```sh
make mt7987_cudy_tr3600_defconfig     # BOOT_DEVICE="spim-nand"
```

## 5. Recovery procedure

### 5.1 Build mtk_uartboot

```sh
git clone https://github.com/981213/mtk_uartboot
cd mtk_uartboot && cargo build --release
```

### 5.2 Force the BootROM into download mode

The BootROM only falls back to UART download when it *fails* to load a valid BL2. Since BL2 is intact,
you must make the read fail on purpose: at the moment of power-on, short the SPI-NAND's **CLK** or **DO**
pin to ground (the well-known "short the flash" trick on MediaTek routers). Release it as soon as the
BootROM has given up.

Keep the serial console open — you will see the BootROM stop producing its usual output instead of
handing over to BL2.

### 5.3 Push BL2 + FIP over UART

```sh
./target/release/mtk_uartboot -s /dev/ttyUSB0 --aarch64 \
  -p /path/to/bl-mt798x-dhcpd/output/bl2-mt7987_ram_boot_uartdl.bin \
  -f /path/to/bl-mt798x-dhcpd/output/fip-mt7987_cudy_tr3600_2025-Yuzhii-dhcpd-fixed-parts_md5-XXXX.bin
```

Sequence: BootROM accepts the payload at `0x201000` → BL2 runs from SRAM → `Starting UART download
handshake ...` → BL2 receives the FIP into DRAM → BL31 → U-Boot. The FIP is about 1.3 MB, roughly ten
seconds at the default 921600 baud.

If you get as far as the `Starting UART download handshake ...` line, BL2 is alive in SRAM and the rest
is routine.

### 5.4 Rewrite the FIP in flash

The U-Boot now running from RAM is the full SPIM-NAND build, so everything works normally:

- **Web UI** — plug into the LAN port, the built-in DHCP server hands you an address, browse to
  `192.168.1.1` and upload the FIP; or
- **Boot menu** — pick the "Upgrade ATF FIP" entry and load it over TFTP.

Reboot, and the device boots from flash again.

## 6. Rehearse before you flash

**This procedure is only worth anything if you have run it once already.** Do the whole chain — short the
pin, enter download mode, push BL2, push FIP, reach the U-Boot prompt — while the device is still
healthy. If the first time you attempt it is after you have bricked the device, you have no safety net,
only a theory.

Other things worth doing before the first flash:

1. **Always attach the serial console.** Without it you cannot even tell which stage failed.
2. **Flash the FIP first, leave BL2 alone.** The stock BL2 and this repo's BL2 both use a 2K+128 NAND
   header and both can load the new FIP, so replacing BL2 first buys you nothing. Updating only the FIP
   lets you validate the new U-Boot against a known-good BL2.
3. **Use the stock U-Boot's boot menu to flash**, not `dd` to `/dev/mtd4` from Linux. The boot menu runs
   `MTK_UPGRADE_FIP_VERIFY`, which compares the common prefix of the old and new `compatible` lists and
   will reject an image meant for another board.
4. **Know which slot you are on.** This build enables `CONFIG_MTK_DUAL_BOOT=y` (with
   `MTK_DUAL_BOOT_IMAGE_ROOTFS_VERIFY` turned off) to match the stock bootloader's A/B semantics: it
   boots whichever slot the `dual_boot.current_slot` environment variable names — slot 0 is
   `kernel`/`rootfs`, slot 1 is `kernel2`/`rootfs2`. The boot menu's firmware upgrade writes to the
   *other* slot and then flips `dual_boot.current_slot`. If you later flash with a sysupgrade script that
   is not slot-aware (one that unconditionally writes `kernel`/`rootfs`), the bootloader may still be
   pointing at the other slot and your new image will look like it did not take effect. Check with
   `fw_printenv dual_boot.current_slot`.

## 7. Caveats

- `mtk_uartboot`'s README says it has been tested on MT7622/MT7629 and MT798x. MT7987 belongs to the
  MT798x family but is **not named explicitly** — verify on your own hardware.
- It does **not** work on devices with secure boot enabled.
- Shorting pins on a powered board carries its own risk. Be sure you have identified the correct pin.
- The last resort, if UART recovery does not work, is an external programmer. Note the chip is a Micron
  **SPI-NAND** (256 MiB, 2 KiB page + 128 B OOB, 128 KiB block) — the usual CH341A NOR-flash workflow
  does not apply.

## 8. TR3600 reference data

```
mtd0  BL2      0x000000  0x100000   1024 KiB
mtd1  backup   0x100000  0x080000    512 KiB   (unused, all zeroes on stock)
mtd2  Factory  0x180000  0x400000   4096 KiB   (MT7993 Wi-Fi EEPROM at offset 0)
mtd3  bdinfo   0x580000  0x040000    256 KiB   (encrypted, opaque)
mtd4  FIP      0x5c0000  0x200000   2048 KiB   <-- BL31 + U-Boot
mtd5  ubi      0x7c0000 0xe840000 237824 KiB
```

- Flash: 256 MiB SPI-NAND, 2 KiB page + 128 B OOB, 128 KiB block, 2048 blocks total.
- NMBM reserves 1/16 of the blocks (management region starts at block 1920), leaving 240 MiB usable.
- SoC: MT7987B, dual-core Cortex-A53, 512 MiB DRAM (type auto-detected — MT7987 selects
  `_SUPPORTS_DDR_AUTO`, so no `_DRAM_DDR4` setting is needed or honoured).
- BL2 size limit 1024 KiB (current build ~261 KiB); FIP size limit 2048 KiB (current build ~1311 KiB).

### UBI volumes and the rootfs_data size

The `ubi` partition holds an A/B layout: `u-boot-env` (1 MiB), `kernel`/`rootfs` (slot 0),
`kernel2`/`rootfs2` (slot 1) and a shared `rootfs_data`.

The `kernel*` and `rootfs*` volumes are sized to the exact image written into them and are deleted and
recreated on every upgrade, so they carry no internal slack — free space in the UBI device is the only
growth buffer.

`rootfs_data` is the exception: `mtd_dual_boot_post_upgrade()` recreates it at
`CONFIG_MTK_DUAL_BOOT_ROOTFS_DATA_SIZE` MiB. Upstream defaults to 8 and caps the range at 128. **The
stock Cudy bootloader uses 140**, which is why this tree carries a one-line patch in
`board/mediatek/Kconfig` widening `range 1 128` to `range 1 1024`.

Evidence — disassembly of the stock U-Boot extracted from the FIP (BL33 at offset 0xb030), located via
the `cmn w0, #0x1c` ENOSPC check (only two matches in the whole binary), identity confirmed by the two
adjacent error strings:

```
41e063e4:  adrp/add x0, 0x41eb57a2  -> "Error: failed to set new image slot valid in env"
41e063fc:  adrp/add x0, 0x41eb57d4  -> "Error: failed to save new image slot to env"
41e06410:  mov w1, #0x0                       ; autoresize = false
41e06414:  mov x0, #0x8c00000  // 146800640   ; 140 MiB
41e06418:  bl  <create_ubi_volume>
41e0641c:  cmn w0, #0x1c                      ; falls back to autoresize only on ENOSPC
```

140 MiB rounds up to 1157 LEBs of 124 KiB, which matches the `rootfs_data` size reported by `ubinfo -a`
on the device exactly. Do not assume this volume auto-grows — it does not; U-Boot pins its size on every
firmware upgrade it performs.

Auto-resize is not a usable alternative under A/B. With `CONFIG_MTK_DUAL_BOOT` set, the
`remove_ubi_volume(PART_ROOTFS_DATA_NAME)` call that normally runs *before* writing the new
kernel/rootfs is skipped, and `rootfs_data` is only recreated afterwards. A volume that had filled all
free space would therefore leave nothing for the other slot to grow into — permanently, since free space
would stay at zero.
