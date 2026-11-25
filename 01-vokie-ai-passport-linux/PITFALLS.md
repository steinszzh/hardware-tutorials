# 踩坑实录：Linux + Vokie + FoloToy AI Passport 蓝牙联调

本文记录在 Ubuntu Linux 上从零打通 **Vokie 桌面端 ↔ FoloToy AI Passport (ESP32 NimBLE)** 过程中踩到的 9 个真实深坑。按排查难度与杀伤力排序，方便后续遇到类似 BLE / Electron / ESP32 问题时直接对照定位。

---

## 坑 1：BlueZ 残留 `Paired/Bonded` 记录导致服务发现阶段秒断（最隐蔽！）

### 现象
- `bleak` 可以扫描到设备，也能触发 `Connected: true`；
- 但仅仅 1 秒后立刻断开，反复报错：
  ```text
  failed to discover services, device disconnected
  ```
- `ServicesResolved` 永远是 `false`，重启 Vokie、重启蓝牙、重启硬件全都无效。

### 根因
用 `bluetoothctl info <MAC>` 检查时发现：
```text
Paired: yes
Bonded: yes
Trusted: yes
```
系统 BlueZ 缓存了之前（例如旧固件或系统蓝牙设置里手动配对时）遗留的 LTK（长期密钥）。当 BlueZ 再次连接这台无需绑定或 NVS 密钥已变的 ESP32 NimBLE 设备时，BlueZ 会主动发起加密协商，ESP32 返回 `PIN or Key Missing`，导致 BlueZ 在 GATT 服务发现完成前直接断开连接。

### 诊断命令
```bash
busctl get-property org.bluez /org/bluez/hci0/dev_XX_XX_XX_XX_XX_XX \
  org.bluez.Device1 Connected ServicesResolved Paired Bonded
bluetoothctl info XX:XX:XX:XX:XX:XX
```

### 解决方案
1. 手动清除旧记录：
   ```bash
   bluetoothctl remove XX:XX:XX:XX:XX:XX
   ```
2. 在 `ai-passport-helper` 中加入自动自愈逻辑 `_clear_stale_bluez_device()`：一旦首次连接因 `discover services` / `disconnected` 失败，自动通过 `dbus-fast` 查询 `Paired` / `Bonded`，若为 `true` 则调用 `org.bluez.Adapter1.RemoveDevice` 清理后立即重试。

---

## 坑 2：ESP32 开机提示音（`boot_sound`）与 BLE 连接时序碰撞

### 现象
- 设备通过 USB RTS 脉冲复位重启后，约 1.1 秒就开始 BLE 广播；
- 如果主机在复位后 `3.0s ~ 3.2s` 立刻发起连接，大概率在 `t = 4.6s` 时被断开，报 `failed to discover services, device disconnected`。

### 根因
抓 `/dev/ttyACM0`（115200 baud）ESP32 串口日志后真相大白：
```text
I (1114) NimBLE: GAP procedure initiated: advertise;
I (1138) boot_sound: Playing boot sound (64000 bytes, volume 55)
I (3286) boot_sound: Boot sound finished (64000/64000 bytes): ESP_OK
```
ESP32 在 `1138ms ~ 3286ms` 期间正在播放开机音频（占用 I2S/DMA 和 CPU），此时切入 GATT 服务发现极易超时或撞上音频释放阶段。

### 解决方案
1. 将 USB 复位后的等待窗口 `USB_BOOT_WAIT` 从 `3.2s` 提高到 **`4.2s`**（完整越过 `3286ms`）；
2. 在 `USB_BOOT_WAIT` 结束后**清空缓存的广播包**（`self.ble_devices.clear()`），避免直接拿 `boot_sound` 期间收到的旧广播去连。

---

## 坑 3：扫描前无条件 USB 复位导致「15 秒无限重启死循环」

### 现象
- 每次连接失败后，ESP32 其实在断开瞬间已经恢复广播，或者仍在稳定运行；
- 但旧版 helper 在 `discover_loop()` / `scan_for_preferred()` 一开始就先执行 `maybe_usb_reset()`，把一台正在正常广播的 ESP32 强行打回重启，紧接着又撞上开机时序，形成死循环。

### 解决方案
- **改为「先被动扫描，超时再唤醒」**：先扫描 `SCAN_RESET_TRIGGER_DELAY = 4.0s`；
- 只有在 4 秒内确实完全收不到任何广播（说明固件已休眠停播）时，才发送 USB RTS 脉冲；
- 同时把 `USB_RESET_COOLDOWN` 提高到 `25.0s`，避免短时间内连续反复重启硬件。

---

## 坑 4：重启 Vokie 后完全看不到外接设备页面 & 插件连不上宿主

### 现象
- 插件明明已经安装在 `~/.config/vokie/plugins/`，但重启 Vokie 后设置页里压根没有「外接设备 / Device Lab」入口；
- 或者用 `ss -tlnp` 检查，发现 `PluginWebSocketServer` 压根没监听。

### 根因
解包分析 `/opt/Vokie/resources/app.asar` 发现三处 Linux 平台硬编码限制：
1. `out/main/index.js`：`const pluginServerReady = isLinuxRemoteOnly() ? Promise.resolve() : this.pluginWebSocketServer.start();` —— Linux 端直接跳过了 WebSocket 服务端启动；
2. `out/main/index.js`：`AiPassportDevicePresenter` 在 Linux 上 `supported: false`；
3. `out/renderer/assets/main-*.js`：`!unref(isLinuxTarget)` 直接隐藏了 `.device-lab-page` 组件。

### 解决方案（免 root）
先检查 Electron Fuses，确认 `EnableEmbeddedAsarIntegrityValidation` 为 `0` 且 `/opt/Vokie/chrome-sandbox` 非 SUID：
1. 用 [scripts/patch-vokie-asar.py](./scripts/patch-vokie-asar.py) 复制一份 `app.asar` 到 `~/.config/vokie/app.asar`，**等长原位替换**这三处逻辑，并同步更新 ASAR 头部 JSON 中对应文件的 SHA256 与 4MB block hashes；
2. 编译 [scripts/asar_override.c](./scripts/asar_override.c) 为 `libvokie_asar_override.so`，通过 `LD_PRELOAD` 拦截 `open/openat/stat/lstat/fstatat/statx/access/fopen`，透明重定向 `app.asar` 路径；
3. 把 `~/.local/share/applications/vokie.desktop` 和 `~/.config/autostart/vokie.desktop` 的启动命令都改为 `start-vokie.sh`。

> ⚠️ **切记**：如果只改了终端里的启动命令，但用户点桌面图标或开机自启打开的还是 `/opt/Vokie/vokie`，就会「一重启又看不到设备」！

---

## 坑 5：ASAR 头部长度变化导致 Electron 无法加载

### 现象
修改 `out/main/index.js` 时如果替换后的字符串比原文长（哪怕多 1 个字节），直接覆写会破坏 ASAR 中所有后续文件的 `offset`；或者只改了内容没更新 `integrity.hash` / `integrity.blocks`，Electron 会拒绝加载。

### 解决方案
- **严格等长替换**：新代码长度 ≤ 原代码长度，不足部分用空格 `b" "` 补齐（在 JS 中完全合法）；
- 例如将重复出现 5 次的长常量名先绑定短变量 `const _p = AI_PASSPORT_EXTERNAL_PLUGIN_ID;`，把长度从 584 字节压到 486 字节（原长 537 字节）；
- 重新计算并原位替换 64 字符十六进制 SHA256（全文件 hash + 每 4MB 分块 hash），保证 header JSON 字节长度分毫不差。

---

## 坑 6：`worker/index.mjs` 在 Helper 退出后永久死亡

### 现象
调试或蓝牙重启过程中，只要 `ai-passport-helper` 子进程退出一次，Vokie 插件就直接报 `AI Passport helper stopped` 并彻底停摆，即使蓝牙恢复也不会重连，必须重启整个 Vokie。

### 解决方案
修改 `vokie-plugin/worker/index.mjs` 中 `child.on('close', ...)`：非主动停止时，清理设备状态并在 `1000ms` 后自动调用 `startHelper()` 重新拉起（详见 [scripts/worker-auto-restart.patch](./scripts/worker-auto-restart.patch)）。

---

## 坑 7：Python `asyncio.TimeoutError` 转字符串为空，日志变成 `Failed to connect to AI Passport: `

### 现象
日志里出现莫名其妙的空错误信息：
```text
Failed to connect to AI Passport: 
```
并且被当作致命错误上报给 UI 显示红字。

### 根因与解决
- Python 中 `str(asyncio.TimeoutError())` 返回空字符串 `""`；
- 改为 `message = str(exc).strip() or exc.__class__.__name__`；
- 同时把 `timeout` / `not found` / `discover services` / `disconnected` 等可恢复瞬态异常归类为 `diagnostic`（重连日志），而不是 `error`（避免在 UI 反复弹致命报错）。

---

## 坑 8：系统重启后 `com.vokie.desktop` 抢占 Electron 单例锁导致补丁完全失效

### 现象
- 已经把 `~/.config/autostart/vokie.desktop` 改成了 `start-vokie.sh`，手动重启 Vokie 一切正常；
- 但**一旦重启电脑**，Vokie 又变回原版：看不到外接设备，`ps aux` 里找不到 `ai-passport-helper`，`/proc/<pid>/environ` 里也没有 `LD_PRELOAD`。

### 根因
Vokie 内置的「开机自启」开关（`~/.config/vokie/app-settings.json` 中的 `"launchAtLogin": true`）每次启动时，会自动写入一个 **`~/.config/autostart/com.vokie.desktop`**，其 `Exec=` 指向未打补丁的原版 `"/opt/Vokie/vokie" --ozone-platform=x11`。

GNOME/桌面环境按文件名字母顺序加载自启项：`com.vokie.desktop`（`c` 开头）排在 `vokie.desktop`（`v` 开头）之前，因此：
1. `com.vokie.desktop` 抢先启动原版 `/opt/Vokie/vokie`，占住 Electron 单例锁（`SingletonLock`）；
2. 紧接着 `vokie.desktop` 拉起带 `LD_PRELOAD` 的 `start-vokie.sh`，检测到单例锁已存在后直接退出；
3. 最终存活的只有未打补丁的原版进程。

### 诊断命令
```bash
ls -la ~/.config/autostart/ | grep -i vokie
tr '\0' '\n' < /proc/$(pgrep -f '/opt/Vokie/vokie' | head -n 1)/environ | grep LD_PRELOAD
```

### 解决方案
1. 关闭 Vokie 内置自启：先退出 Vokie，再将 `~/.config/vokie/app-settings.json` 中的 `"launchAtLogin": true` 改为 `false`（之后不要在 Vokie 设置界面里重新打开它）；
2. 删除冲突项，只保留指向 `start-vokie.sh` 的 `~/.config/autostart/vokie.desktop`：
   ```bash
   rm -f ~/.config/autostart/com.vokie.desktop
   ```
3. 在 `scripts/start-vokie.sh` 启动前加入防呆清理 `rm -f "${XDG_CONFIG_HOME:-$HOME/.config}/autostart/com.vokie.desktop"`。

---

## 坑 9：从 GNOME 快捷方式或开机自启启动时找不到 `nvm` 的 `node`，插件静默不启动

### 现象
- 从**终端**运行 `start-vokie.sh` 一切正常；
- 但点 **GNOME 应用菜单快捷方式**或**开机自启**打开 Vokie 时，明明已经加载了补丁版 `app.asar`（`LD_PRELOAD` 生效，且 `PluginWebSocketServer` 已在 `127.0.0.1` 监听），却完全没有 `worker/index.mjs` 和 `ai-passport-helper` 进程，日志里也看不到任何插件输出，设备永远连不上。

### 根因
插件配置 `vokie.plugin.json` 中声明 `"runtime": "node"`，Vokie 会直接从 `PATH` 查找并启动 `node`。
如果系统的 Node.js 是通过 `nvm` / `fnm` / `volta` 安装在用户目录（例如 `~/.nvm/versions/node/v22.x.x/bin/node`），而 `/usr/bin/node` 不存在：
- 终端属于交互式 Shell，会加载 `~/.bashrc` 把 `nvm` 目录加入 `PATH`；
- GNOME 桌面快捷方式（`.desktop`）和开机自启属于**非交互环境**，不加载 `~/.bashrc`，`PATH` 里只有 `/usr/local/bin:/usr/bin:/bin`，导致 Vokie 拉起 `node` 直接 `ENOENT` 失败。

### 诊断命令
```bash
which node
ls -la /usr/bin/node /usr/local/bin/node
tr '\0' '\n' < /proc/$(pgrep -f '/opt/Vokie/vokie' | head -n 1)/environ | grep '^PATH='
```

### 解决方案
1. 在 `scripts/start-vokie.sh` 启动 `/opt/Vokie/vokie` 前，检测 `command -v node`；若不存在则自动扫描 `~/.nvm`、`~/.fnm`、`~/.volta`、`~/.asdf`、`~/.local/bin` 并将其前置到 `PATH`（最新版 [scripts/start-vokie.sh](./scripts/start-vokie.sh) 已内置）；
2. 同时建议在 `~/.local/share/applications/vokie.desktop` 的 `Exec=` 中显式加上 `--ozone-platform=x11 %U`，避免 Wayland 下二次重启；
3. 如果之前排查时在 UI 里误关了插件开关，确认 `~/.config/vokie/plugins/registry.json` 中 `"enabled"` 为 `true`。

---

## 附：快速排错 Checklist

遇到连接异常时，按顺序跑这 6 条命令，99% 的问题 30 秒内定位：

1. **看是否被原生自启项抢占 & 是否加载了补丁版 ASAR**：
   ```bash
   ls -la ~/.config/autostart/ | grep -i vokie
   ls -l /proc/$(pgrep -f '/opt/Vokie/vokie' | head -n 1)/fd | grep app.asar
   ```
2. **看 Vokie 进程的 `PATH` 里有没有 `node`（以及插件是否处于 `enabled: true`）**：
   ```bash
   tr '\0' '\n' < /proc/$(pgrep -f '/opt/Vokie/vokie' | head -n 1)/environ | grep '^PATH='
   grep '"enabled"' ~/.config/vokie/plugins/registry.json
   ```
3. **看 BlueZ 是否有残留配对**：
   ```bash
   bluetoothctl info <MAC> | grep -E 'Paired|Bonded|Connected'
   ```
4. **看插件进程是否存活**：
   ```bash
   ps aux | grep ai-passport-helper | grep -v grep
   ```
5. **看 Vokie 实时插件日志**：
   ```bash
   tail -n 50 ~/.config/vokie/logs/app.log | grep -E 'external-device|helper'
   ```
6. **看 ESP32 串口底层事件**（插着 USB 时）：
   ```bash
   python3 -c 'import serial; s=serial.Serial("/dev/ttyACM0",115200); [print(s.readline().decode(errors="replace"),end="") for _ in range(100)]'
   ```
