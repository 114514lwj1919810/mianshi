# 制包工具

普通生产和升级不需要单独运行本目录脚本。请使用工程根目录`元气小丑生产升级工具.cmd`。

核心脚本：

- `key_management.py`：首次创建厂家 Ed25519 密钥，后续只读取现有密钥。
- `register_device.py`：登记 UID 和机台号，生成每机随机密钥。
- `manage_devices.py`：查看或修改 active/inactive，列表不输出密钥。
- `universal_iap_packager.py`：生成一卡多机 AES-256-GCM + Ed25519 升级包。
- `verify_universal_package.py`：独立重做签名、授权集、每机信封和 App 解密验证。
- `make_factory_card.py`：在通用包副本中加入某一块空板的 `IDENTITY.SEC`（8.3 文件名）。

依赖：Python 3 和 `cryptography`。第三方 Bootloader 验签源码使用 Monocypher 4.0.3，保留原 BSD-2-Clause/CC0 许可声明。
