[English](../uart-recovery.md) | [简体中文](#)

# UART BootROM 救砖（MT7987 / Cudy TR3600）

用于 FIP（BL31 + U-Boot）已损坏、但 BL2 仍完好的设备。

## 1. 什么时候需要它

网页 failsafe、DHCP 服务、TFTP 客户端**全部住在 U-Boot 里**，而 U-Boot 存放在 FIP 分区。FIP 一坏，
这些统统跟着消失。你能看到的只有 BL2 打印：

```
BL2: Failed to load image id 5 (-2)
```

然后挂死。此时 flash 上没有任何东西能救你——必须绕到 BL2 下面一层，回到 SoC 的 BootROM。

## 2. 为什么 BL2 自己救不了

三条候选兜底，逐条排除：

| 候选方案 | 结论 |
| --- | --- |
| BL2 内置网络栈 | **不存在。** BL2 二进制里搜不到任何 `tftp` / `ethernet` / `arp` / `dhcp` / `mdio` / `gmac` 代码。BL2 只会读 SPI-NAND、跑 NMBM、初始化 DRAM、往 UART 打字。 |
| flash 上这颗 BL2 的 UART 下载 | **用不了。** `plat/mediatek/apsoc_common/bl2/Config-uart_dl.in` 里的 `_RAM_BOOT_RAM_BOOT_UART_DL` 明确 `depends on _BOOT_DEVICE_RAM`，只适用于「由 BootROM 载入 SRAM 执行」的那种 BL2，不适用于 flash 里这颗。 |
| Dual-FIP | **这个布局不适用。** `_DUAL_FIP` 只在 `(_BOOT_DEVICE_SPIM_NAND && _NAND_UBI)` 或 eMMC 时默认 `y`。TR3600 是 SPIM-NAND + *固定分区*，而 `_FIP_IN_*` / `_FIP2_IN_*` 位置选项全部 `depends on _BOOT_DEVICE_EMMC`。不改分区表就没地方放第二份 FIP，改了又会破坏与原厂的兼容性。 |

唯一出路是让 BootROM 进入 UART 下载模式，通过串口把新的 BL2 + FIP 推进内存执行，工具是
[981213/mtk_uartboot](https://github.com/981213/mtk_uartboot)。

## 3. 准备什么

- USB-TTL 串口适配器，接到板子的 UART（3.3 V，115200 8N1）。
- `mtk_uartboot`（Rust 项目，需要 `cargo`）。
- 一个**带 UART 下载支持的 RAM-boot BL2**——见下文，或直接用预构建的
  `output/bl2-mt7987_ram_boot_uartdl.bin`。
- 一个确认可用的 FIP，例如 `output/fip-mt7987_cudy_tr3600_2025-*.bin`。

## 4. 构建 RAM-boot BL2

配置放在共享的 ATF configs 目录（`atf-20250711/configs` 是指向
`atf-20240117-bacca82a8/configs` 的符号链接）：

`configs/mt7987_ram_boot_defconfig`

```
_PLAT_MT7987=y
_DDR4_FREQ_2666=y
_BOOT_DEVICE_RAM=y
_RAM_BOOT_RAM_BOOT_UART_DL=y
_DRAM_DEBUG_LOG=y
```

该配置会把匹配本机板级设计的 DDR4 速率固定为 2666 MT/s，并解析为
`BOOT_DEVICE="ram"` + `RAM_BOOT_UART_DL=1`，命中 `bl2_image.mk` 里的 `BL2_BOOT_RAM` 规则，
拉入 `bl2_boot_ram.c`、`uart_dl.c`、`hsuart-extra.c`，并打开 `ENABLE_CONSOLE_GETC`。

```sh
cd atf-20250711
make mt7987_ram_boot_defconfig
make bl2 -j$(nproc) \
  CROSS_COMPILE=aarch64-linux-gnu- CROSS_COMPILER=aarch64-linux-gnu- \
  CONFIG_CROSS_COMPILER=aarch64-linux-gnu- \
  DTC=../uboot-mtk-20250711/scripts/dtc/dtc
```

必须带 `DTC=`，因为 TF-A 需要 `dtc` 而多数发行版默认不装；U-Boot 树里自带一个可用的。

产物是 `build/mt7987/release/**bl2.bin**`（注意是 `bl2.bin` 而**不是** `bl2.img`——它不带
`SPINAND!` BootROM 头，因为是走串口送进去而不是从 flash 读）。

产物自检：

| 检查项 | 应为 |
| --- | --- |
| 入口地址 | `0x201000`——与 `mtk_uartboot` 的 `--load-addr` 默认值一致，不用带 `-l` |
| 开头字节 | `f4 03 00 aa`（`mov x20, x0`，AArch64 BL2 入口），**不应有** `SPINAND!` magic |
| 字符串 | 必须含 `Starting UART download handshake ...` |

用完记得把板级配置切回来，否则下次跑 `build.sh` 会基于 RAM-boot 配置开始：

```sh
make mt7987_cudy_tr3600_defconfig     # BOOT_DEVICE="spim-nand"
```

## 5. 救砖流程

### 5.1 编译 mtk_uartboot

```sh
git clone https://github.com/981213/mtk_uartboot
cd mtk_uartboot && cargo build --release
```

### 5.2 强制 BootROM 进入下载模式

BootROM 只有在**加载 BL2 失败**时才会回落到 UART 下载。既然 BL2 是好的，就得让它读取失败：上电瞬间把
SPI-NAND 的 **CLK** 或 **DO** 引脚对地短接（MTK 路由圈子里通用的「短接 flash」手法），BootROM 放弃后
立即松开。

保持串口窗口打开——你会看到 BootROM 不再像平常那样把控制权交给 BL2。

### 5.3 通过串口推送 BL2 + FIP

```sh
./target/release/mtk_uartboot -s /dev/ttyUSB0 --aarch64 \
  -p /path/to/bl-mt798x-dhcpd/output/bl2-mt7987_ram_boot_uartdl.bin \
  -f /path/to/bl-mt798x-dhcpd/output/fip-mt7987_cudy_tr3600_2025-Yuzhii-dhcpd-fixed-parts_md5-XXXX.bin
```

顺序：BootROM 在 `0x201000` 接收 payload → BL2 在 SRAM 中运行 → 打印
`Starting UART download handshake ...` → BL2 把 FIP 收进 DRAM → BL31 → U-Boot。FIP 约 1.3 MB，在默认
921600 baud 下大约十几秒。

**只要看到 `Starting UART download handshake ...` 这一行，就说明 BL2 已经在 SRAM 里跑起来了**，后面就是
常规操作。

### 5.4 重写 flash 里的 FIP

此时内存里跑的是完整的 SPIM-NAND 版 U-Boot，一切功能正常：

- **网页 UI**——接 LAN 口，内置 DHCP 会给你分配地址，浏览器打开 `192.168.1.1` 上传 FIP；或者
- **启动菜单**——选 "Upgrade ATF FIP"，走 TFTP 载入。

重启，设备就恢复从 flash 启动了。

## 6. 刷机前务必先演练

**这套流程只有在你已经完整跑通过一次之后才有价值。** 趁设备还健康的时候，把整条链路走一遍——短接引脚、
进下载模式、送 BL2、送 FIP、看到 U-Boot 提示符。如果你第一次尝试是在把设备刷砖之后，那你手里的不是退路，
只是一个假设。

首刷前另外几件值得做的事：

1. **必须接串口。** 没有串口你连是哪一阶段失败的都判断不了。
2. **先刷 FIP，不动 BL2。** 原厂 BL2 和本仓库的 BL2 都使用 2K+128 的 NAND 头，都能加载新 FIP，所以先换
   BL2 没有任何收益。只更新 FIP 可以在保留一个已知可用 BL2 的前提下验证新 U-Boot。
3. **用原厂 U-Boot 的启动菜单刷**，而不是在 Linux 下 `dd` 到 `/dev/mtd4`。启动菜单会走
   `MTK_UPGRADE_FIP_VERIFY`，比对新旧 `compatible` 列表的公共前缀，能挡掉刷错板子固件这类错误。
4. **搞清楚自己在哪个槽。** 本构建开启了 `CONFIG_MTK_DUAL_BOOT=y`（并关掉了
   `MTK_DUAL_BOOT_IMAGE_ROOTFS_VERIFY`），与原厂 bootloader 的 A/B 语义一致：按环境变量
   `dual_boot.current_slot` 指定的槽启动——slot 0 是 `kernel`/`rootfs`，slot 1 是 `kernel2`/`rootfs2`。
   启动菜单的固件升级会写入**另一个**槽并翻转 `dual_boot.current_slot`。如果之后改用不感知槽位的
   sysupgrade 脚本（无条件写 `kernel`/`rootfs`），bootloader 可能仍指向另一个槽，表现为"刷了但没生效"。
   用 `fw_printenv dual_boot.current_slot` 确认。

## 7. 注意事项

- `mtk_uartboot` 的 README 声明测试过 MT7622/MT7629 和 MT798x。MT7987 属于 MT798x 系列，但**没有被点名**，
  需要你在自己的硬件上实测确认。
- 对**开启了 secure boot** 的设备无效。
- 在带电板子上短接引脚本身有风险，务必先确认引脚位置正确。
- UART 救砖也失败时的最后手段是外置编程器。注意这颗是 Micron **SPI-NAND**（256 MiB，2 KiB 页 + 128 B
  OOB，128 KiB 块），常见的 CH341A 刷 NOR 那一套流程不适用。

## 8. TR3600 参考数据

```
mtd0  BL2      0x000000  0x100000   1024 KiB
mtd1  backup   0x100000  0x080000    512 KiB   （未使用，原厂实测全 0）
mtd2  Factory  0x180000  0x400000   4096 KiB   （偏移 0 处是 MT7993 Wi-Fi EEPROM）
mtd3  bdinfo   0x580000  0x040000    256 KiB   （加密，不可读）
mtd4  FIP      0x5c0000  0x200000   2048 KiB   <-- BL31 + U-Boot
mtd5  ubi      0x7c0000 0xe840000 237824 KiB
```

- Flash：256 MiB SPI-NAND，2 KiB 页 + 128 B OOB，128 KiB 块，共 2048 块。
- NMBM 保留 1/16 的块（管理区从 block 1920 开始），可用容量 240 MiB。
- SoC：MT7987B，双核 Cortex-A53，512 MiB DRAM（类型运行时自动识别——MT7987 会 select
  `_SUPPORTS_DDR_AUTO`，因此 `_DRAM_DDR4` 既不需要写、写了也无效）。
- BL2 上限 1024 KiB（当前构建约 261 KiB）；FIP 上限 2048 KiB（当前构建约 1311 KiB）。

### UBI 卷布局与 rootfs_data 尺寸

`ubi` 分区里是 A/B 布局：`u-boot-env`（1 MiB）、`kernel`/`rootfs`（slot 0）、`kernel2`/`rootfs2`
（slot 1），以及共享的 `rootfs_data`。

`kernel*` 和 `rootfs*` 的尺寸按写入镜像的精确大小分配，每次升级都是删掉重建，**内部没有任何余量**——
UBI 设备上的空闲空间是唯一的增长缓冲。

`rootfs_data` 是例外：`mtd_dual_boot_post_upgrade()` 会按 `CONFIG_MTK_DUAL_BOOT_ROOTFS_DATA_SIZE`
（单位 MiB）重建它。上游默认值是 8、`range` 上限是 128。**原厂 Cudy bootloader 用的是 140**，所以本仓库
在 `board/mediatek/Kconfig` 里带了一行补丁，把 `range 1 128` 放宽为 `range 1 1024`。

依据——从 FIP 里提取原厂 U-Boot（BL33 位于偏移 0xb030）反汇编，用 `cmn w0, #0x1c` 这个 ENOSPC 检查定位
（全二进制仅 2 处命中），再由相邻两条错误字符串确认函数身份：

```
41e063e4:  adrp/add x0, 0x41eb57a2  -> "Error: failed to set new image slot valid in env"
41e063fc:  adrp/add x0, 0x41eb57d4  -> "Error: failed to save new image slot to env"
41e06410:  mov w1, #0x0                       ; autoresize = false
41e06414:  mov x0, #0x8c00000  // 146800640   ; 140 MiB
41e06418:  bl  <create_ubi_volume>
41e0641c:  cmn w0, #0x1c                      ; 仅在 ENOSPC 时才回退到 autoresize
```

140 MiB 按 124 KiB 的 LEB 向上取整正好是 1157 LEB，与设备上 `ubinfo -a` 报告的 `rootfs_data` 尺寸分毫不
差。**不要以为这个卷会自动增长**——它不会；U-Boot 每次执行固件升级都会把它的尺寸钉死。

在 A/B 模式下 auto-resize 不是可行的替代方案。开启 `CONFIG_MTK_DUAL_BOOT` 后，本来在写入新
kernel/rootfs **之前**执行的 `remove_ubi_volume(PART_ROOTFS_DATA_NAME)` 会被跳过，`rootfs_data` 只在之后
才重建。一个吃光全部空闲空间的卷会让另一个槽再也没有增长余地，而且是永久性的——空闲将一直是 0。
