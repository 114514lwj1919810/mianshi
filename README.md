# mianshi / joker

元气小丑 GD32 工程归档。仓库目标分支为 `joker`。

## 目录

- `joker-v1/`：GD32F407 游戏主工程，包含普通下载目标、A/B IAP 目标、业务代码、驱动、测试和说明书。
- `finall-joker/bootloader/`：GD32 安全 A/B Bootloader 工程。
- `finall-joker/制包工具/`：Python 与 PowerShell 生产、登记、签名、加密、验证和通用升级卡制作工具。
- `finall-joker/*.cmd`、`workflow.ps1`：生产升级入口脚本。
- `finall-joker/产品表/`：只保留产品型号和空白模板；真实设备表、身份文件及厂家私钥不进入 Git。

## Keil 工程

主工程：

```text
joker-v1/Projects/MDK-ARM/joker_gd32f407.uvprojx
joker-v1/Projects/MDK-ARM/joker_gd32f407_iap.uvprojx
joker-v1/Projects/MDK-ARM/joker_gd32f407_no_lock.uvprojx
joker-v1/Projects/MDK-ARM/joker_gd32f407_no_lock_iap.uvprojx
```

Bootloader：

```text
finall-joker/bootloader/Projects/MDK-ARM/GD_BOOT_SECURE.uvprojx
```

## 首次恢复生产环境

1. 安装 Keil MDK5、ARM Compiler 5，并导入 `joker-v1/Resources/KeilPack/` 内的芯片包。
2. 安装 `finall-joker/制包工具/requirements.txt` 中的 Python 依赖。
3. 将 `finall-joker/产品表/devices.csv.example` 复制为 `devices.csv`。
4. 从离线安全备份恢复厂家签名私钥到：

   ```text
   finall-joker/产品表/签名密钥/manufacturer_ed25519_private.pem
   ```

5. 从离线安全备份恢复真实 `devices.csv` 和每机身份文件；没有这些材料时，只能编译源码，不能给现有量产设备制作有效授权包。
6. 运行 `finall-joker/元气小丑生产升级工具.cmd`。

如需建立一套全新的、与旧设备不兼容的签名体系，可运行制包工具中的 `key_management.py` 生成新密钥，并同步更新 Bootloader 内厂家公钥后重新烧录 Bootloader。

## 安全说明

本仓库故意不包含：

- 厂家 Ed25519 私钥；
- 每台机器的 `device_key`、UID 产品表；
- `IDENTITY.SEC`、`DEVICES.SEC`、`UPDATE.SEC`；
- 已生成的加密固件包和新板初始化包；
- Keil 编译产物。

这些文件一旦进入 Git 历史，仅删除最新版本不能真正撤销泄漏，必须立即更换相应密钥。

