# AnimalRace / RunHorse

GD32F407VET6 与 Android Unity 组成的嵌入式赛跑系统。GD32 负责输入采集、权威状态、计算、持久化和外设控制；Android Unity 负责串口接入、交互界面和动画表现。

此源码放在独立的 `runhorse` 分支中，与仓库原有 `joker` 分支互不合并、互不覆盖。

## 目录

| 目录 | 内容 |
| --- | --- |
| `unity/Unity_AnimalRace` | Unity 2018.4.36f1 工程源码、资源、Android AAR 串口插件和编辑器构建脚本 |
| `firmware/GD32F407VET6` | GD32F407VET6 应用源码、协议、服务、测试和 Keil 工程 |
| `bootloader/AnimalRace_GD32F407VE` | TF 卡安全 IAP Bootloader、A/B 应用槽、签名校验和 Keil 工程 |

## 工程入口

### Unity

使用 Unity `2018.4.36f1` 打开：

```text
unity/Unity_AnimalRace
```

Android 串口插件位于：

```text
unity/Unity_AnimalRace/Assets/Plugins/Android
```

### GD32 应用

Keil 工程位于：

```text
firmware/GD32F407VET6/MDK-ARM
```

目录中包含 Standalone、IAP A 和 IAP B 对应的工程配置与链接脚本。

### Bootloader

Keil 工程入口：

```text
bootloader/AnimalRace_GD32F407VE/Projects/MDK-ARM/GD_BOOT_SECURE.uvprojx
```

Bootloader 面向 512 KiB Flash 的 GD32F407VET6，应用 A/B 槽分别位于 `0x08020000` 和 `0x08040000`。详细布局见 Bootloader 目录中的 README。

## 仓库约定

- 不提交 Unity `Library`、`Temp`、构建 APK 和日志。
- 不提交 Keil `Output`、`Objects`、映射文件和本机用户配置。
- 不提交 Android 签名文件、密钥库或本机路径配置。
- Unity 的 `.meta` 文件必须与资源同时提交。
