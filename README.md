# ATF and u-boot for mt798x with DHCPD

A modified version of hanwckf's U-Boot for MT798x by Yuzhii, with support for DHCPD and a beautiful web UI. (Builds available for versions 2022/2023/2024/2025)

Supports GitHub Actions for automatic builds, and can generate both normal and overclocked BL2.

**Warnign: Flashing custom bootloaders can brick your device. Proceed with caution and at your own risk.**

## About bl-mt798x

- <https://cmi.hanwckf.top/p/mt798x-uboot-usage>

> Version-2022 WEB UI preview

![Version-2022](document/pictures/uboot-2022.png)

> Version-2023/2024 WEB UI preview

![Version-2023/2024](document/pictures/uboot-2023.png)

> Version-2025 WEB UI preview

U-Boot 2025 adds more features:

- System info display
- Factory (RF) update
- Backup download
- Web terminal
- Environment manager
- Device reboot

![Version-2025](document/pictures/uboot-2025.png)

You can configure the features you need.

- [x] MTK_DHCPD
  - [x] MTK_DHCPD_ENHANCED
- Failsafe Web UI style:
  - [x] WEBUI_FAILSAFE_UI_NEW
  - [ ] WEBUI_FAILSAFE_UI_OLD
- [x] WEBUI_FAILSAFE_FACTORY
- [x] WEBUI_FAILSAFE_BACKUP
- [x] WEBUI_FAILSAFE_ENV
- [x] WEBUI_FAILSAFE_CONSOLE

> Enable WEBUI_FAILSAFE_UI_OLD will use the traditional webui interface.

## Prepare

```bash
sudo apt install gcc-aarch64-linux-gnu build-essential flex bison libssl-dev device-tree-compiler qemu-user-static
```

## Build

```bash
chmod +x build.sh
SOC=mt7981 BOARD=sn_r1 VERSION=2025 ./build.sh
SOC=mt7981 BOARD=cmcc_a10 VERSION=2022 MULTI_LAYOUT=1 ./build.sh
```

- SOC=mt7981/mt7986
- VERSION=2022/2023/2024/2025
- MULTI_LAYOUT=1 (Optional, only for multi-layout devices, e.g. xiaomi-wr30u, redmi-ax6000)
- Version differences:

| Version | ATF | UBOOT |
| --- | --- | --- |
| 2022 | 20220606-637ba581b | 20220606 |
| 2023 | 20231013-0ea67d76a | 20230718-09eda825 |
| 2024 | 20240117-bacca82a8 | 20230718-09eda825 |
| 2025 | 20250711 | 20250711 |

Generated files will be in the `output`

## Generate GPT with python2.7

> install denpendencies

```bash
sudo apt-get install python2-dev swig
```

> run

```bash
chmod +x generate_gpt.sh
./generate_gpt.sh
```

Generated files will be in the `output_gpt`

> You need to add your device's partition info JSON file in the "mt798x_gpt" directory, e.g. "atf-dir/tools/dev/gpt_editor/example/gpt.json".

When you enable `SDMMC=1` (e.g. `SDMMC=1 ./generate_gpt.sh`), the generated GPT image will support MTK SDMMC.

### Show GPT info

Create a directory named `mt798x_gpt_bin` in the respository root directory, and put your GPT bin files in it.

Then run:

```bash
chmod +x show_gpt.sh
./show_gpt.sh
```

Then it will display the GPT partition info of all GPT bin files in `mt798x_gpt_bin` directory, and output the results to `gpt_info.txt` in the `output_gpt` directory.

## Use Action to build

- [x] Build FIP
- [ ] Build GPT (Only gpt.json exists)
- [ ] Build BL2 (Normal)
- [ ] Build BL2 (Overclocking)
- [ ] Multi-layout support (Only for multi-layout devices)
- [ ] Special subnet support (Custom default IP for DHCPD)

> Although you can customize the DHCPD subnet, the mask is fixed to "255.255.255.0", so you must ensure your device is in this subnet.

---

## FIT support

This function is based on 1715173329's [bl-mt798x-oss](https://github.com/1715173329/bl-mt798x-oss/tree/fit-example)

I have created a patch based on his work to support FIT images.

Only a limited number of models are supported. If your device is not listed, you can try to add support yourself by following his work.

***BUT NOT TESTED YET!***

So, **you MUST test it yourself, and there is a risk of BRICKING your device!**

There are two ways to build:

1. Apply patch and build Version 2022

    ```bash
    git apply modify-patch/0002-uboot-2022-fit-merge-code-from-1715173329-to-support.patch
    SOC=mt7981 BOARD=your_board VERSION=2022 ./build.sh
    ```

2. Build via Actions

Open the repository's Actions tab, choose the "Build FIT BL2 and FIP" workflow, and run it.

---

### xiaomi-wr30u multi-layout uboot firmware compatibility

|Firmware type|uboot (default)|uboot (immortalwrt-112m)|uboot (qwrt)|
|:----:|:----:|:----:|:----:|
|[xiaomi stock mtd8/mtd9](https://github.com/hanwckf/xiaomi-router-stock-ubi-bin/tree/main/xiaomi-wr30u)|√|×|×|
|[immortalwrt-mt798x stock](https://github.com/hanwckf/immortalwrt-mt798x/blob/openwrt-21.02/target/linux/mediatek/files-5.4/arch/arm64/boot/dts/mediatek/mt7981-xiaomi-mi-router-wr30u-stock.dts)|√|×|×|
|[OpenWrt stock](https://github.com/openwrt/openwrt/blob/main/target/linux/mediatek/dts/mt7981b-xiaomi-mi-router-wr30u-stock.dts)|√|×|×|
|[immortalwrt stock](https://github.com/immortalwrt/immortalwrt/blob/master/target/linux/mediatek/dts/mt7981b-xiaomi-mi-router-wr30u-stock.dts)|√|×|×|
|[X-Wrt stock](https://github.com/x-wrt/x-wrt/blob/master/target/linux/mediatek/dts/mt7981b-xiaomi-mi-router-wr30u-stock.dts)|√|×|×|
|[immortalwrt-mt798x 112m](https://github.com/hanwckf/immortalwrt-mt798x/blob/openwrt-21.02/target/linux/mediatek/files-5.4/arch/arm64/boot/dts/mediatek/mt7981-xiaomi-mi-router-wr30u-112m.dts)|×|√|×|
|[GL.iNet by 237176253](https://www.right.com.cn/forum/thread-8297881-1-1.html)|×|√|×|
|[X-Wrt 112m nmbm](https://github.com/x-wrt/x-wrt/blob/master/target/linux/mediatek/dts/mt7981b-xiaomi-mi-router-wr30u-112m-nmbm.dts)|×|√|×|
|[OpenWrt 112m nmbm](https://github.com/openwrt/openwrt/blob/main/target/linux/mediatek/dts/mt7981b-xiaomi-mi-router-wr30u-112m-nmbm.dts)|×|√|×|
|[immortalwrt 112m nmbm](https://github.com/immortalwrt/immortalwrt/blob/master/target/linux/mediatek/dts/mt7981b-xiaomi-mi-router-wr30u-112m-nmbm.dts)|×|√|×|
|[X-Wrt 112m nmbm](https://github.com/x-wrt/x-wrt/blob/master/target/linux/mediatek/dts/mt7981b-xiaomi-mi-router-wr30u-112m-nmbm.dts)|×|√|×|
|[QWRT](https://www.right.com.cn/forum/thread-8284824-1-1.html)|×|×|√|
|[OpenWrt ubootmod](https://github.com/openwrt/openwrt/blob/main/target/linux/mediatek/dts/mt7981b-xiaomi-mi-router-wr30u-ubootmod.dts)|×|×|×|
|[immortalwrt ubootmod](https://github.com/immortalwrt/immortalwrt/blob/master/target/linux/mediatek/dts/mt7981b-xiaomi-mi-router-wr30u-ubootmod.dts)|×|×|×|
|[X-Wrt ubootmod](https://github.com/x-wrt/x-wrt/blob/master/target/linux/mediatek/dts/mt7981b-xiaomi-mi-router-wr30u-ubootmod.dts)|×|×|×|

### redmi-ax6000 multi-layout uboot firmware compatibility

|Firmware type|uboot (default)|uboot (immortalwrt-110m)|
|:----:|:----:|:----:|
|[xiaomi stock mtd8/mtd9](https://github.com/hanwckf/xiaomi-router-stock-ubi-bin/tree/main/redmi-ax6000)|√|×|
|[immortalwrt-mt798x stock](https://github.com/hanwckf/immortalwrt-mt798x/blob/openwrt-21.02/target/linux/mediatek/files-5.4/arch/arm64/boot/dts/mediatek/mt7986a-xiaomi-redmi-router-ax6000-stock.dts)|√|×|
|[OpenWrt stock](https://github.com/openwrt/openwrt/blob/main/target/linux/mediatek/dts/mt7986a-xiaomi-redmi-router-ax6000-stock.dts)|√|×|
|[immortalwrt stock](https://github.com/immortalwrt/immortalwrt/blob/master/target/linux/mediatek/dts/mt7986a-xiaomi-redmi-router-ax6000-stock.dts)|√|×|
|[X-Wrt stock](https://github.com/x-wrt/x-wrt/blob/master/target/linux/mediatek/dts/mt7986a-xiaomi-redmi-router-ax6000-stock.dts)|√|×|
|[immortalwrt-mt798x](https://github.com/hanwckf/immortalwrt-mt798x/blob/openwrt-21.02/target/linux/mediatek/files-5.4/arch/arm64/boot/dts/mediatek/mt7986a-xiaomi-redmi-router-ax6000.dts)|×|√|
|[GL.iNet by 237176253](https://www.right.com.cn/forum/thread-8297881-1-1.html)|×|√|
|[X-Wrt ubootlayout](https://github.com/x-wrt/x-wrt/blob/master/target/linux/mediatek/dts/mt7986a-xiaomi-redmi-router-ax6000-ubootlayout.dts)|×|√|
|[OpenWrt ubootmod](https://github.com/openwrt/openwrt/blob/main/target/linux/mediatek/dts/mt7986a-xiaomi-redmi-router-ax6000-ubootmod.dts)|×|×|
|[immortalwrt ubootmod](https://github.com/immortalwrt/immortalwrt/blob/master/target/linux/mediatek/dts/mt7986a-xiaomi-redmi-router-ax6000-ubootmod.dts)|×|×|
|[X-Wrt ubootmod](https://github.com/x-wrt/x-wrt/blob/master/target/linux/mediatek/dts/mt7986a-xiaomi-redmi-router-ax6000-ubootmod.dts)|×|×|
