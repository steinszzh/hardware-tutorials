# Hardware Tutorials / 硬件折腾与排坑实录

记录各类硬件设备在 Linux / 嵌入式环境下的逆向适配、协议对接、驱动调试与真实踩坑指南。

每一篇教程都包含：
- **架构与通信链路拆解**（从硬件固件到桌面端 / 协议层）
- **可复现的部署脚本与补丁**（已去除本机硬编码路径与个人设备信息）
- **真实踩坑实录 (`PITFALLS.md`)**：完整记录「现象 → 根因 → 诊断命令 → 解决方案」

---

## 目录

| 编号 | 主题 | 硬件 / 平台 | 关键词 | 文档 |
| --- | --- | --- | --- | --- |
| `01` | [Linux 下打通 Vokie 与 FoloToy AI Passport 蓝牙语音连接](./01-vokie-ai-passport-linux/README.md) | FoloToy AI Passport (ESP32 NimBLE) + Ubuntu Linux + Vokie Electron | `BLE` `BlueZ` `Electron ASAR` `LD_PRELOAD` `ESP32` `GATT` | [教程](./01-vokie-ai-passport-linux/README.md) · [踩坑实录](./01-vokie-ai-passport-linux/PITFALLS.md) |

---

## 仓库结构约定

每个硬件项目独立一个带编号的目录：

```text
hardware-tutorials/
├── README.md
└── 01-<project-name>/
    ├── README.md          # 完整实战教程与部署步骤
    ├── PITFALLS.md        # 深度踩坑点与排查手册
    ├── scripts/           # 辅助脚本、补丁与源码
    └── desktop/           # 桌面集成 / udev / systemd 配置模板（如适用）
```

## License

MIT
