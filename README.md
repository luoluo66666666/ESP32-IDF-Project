# ESP-Wash

ESP32-S3 设备项目，支持三种控制/配网方式：

- 蓝牙 BLE
- 本地 WiFi 热点 AP + 本地 TCP
- STA 连路由器 + 云端 TCP

---

## 本地配网模式（AP）

首次上电（尚未配网成功）时，设备进入 AP 配网模式：

| 项目 | 默认值 |
|------|--------|
| 热点名称 | `ESP-WASH` |
| 热点密码 | `12345678` |
| 本地 TCP | `192.168.4.1:9000` |

手机连上热点后，通过 TCP 发送 `CFG:` / `SYS:` 命令进行配网。

配网成功（`SYS:MODE=STA` 或 `CFG:APPLY`，且 STA **拿到 IP**）后，设备会把 `wifi_prov=1` 写入 NVS。  
之后每次上电将**自动**连上次的店铺 WiFi 和云端 TCP，无需重新输入。

需要重新配网时，发送：`SYS:MODE=AP`

---

## STA 工作模式

执行 `SYS:MODE=STA` / `CFG:APPLY` 后：

1. 关闭本地 AP / 本地 TCP
2. 连接配置的路由器 WiFi
3. 连接配置的云端 TCP 服务器
4. STA 拿到 IP 后写入 NVS 并标记已配网

若新 WiFi 试连 **30 秒**内失败，会自动回滚旧配置并退回 AP 配网模式。

---

## 命令协议

BLE、AP 本地 TCP、STA 云端 TCP **三通道相同**：发什么命令，`ctrl_protocol` 原样回什么（与蓝牙一致，无 `CMD|` / `ACK|` 包装）。

| 前缀 | 用途 |
|------|------|
| `CMD:*` | 业务控制（模式、DO、温度等） |
| `CFG:*` | 配网参数（WiFi、云端地址、OTA 地址） |
| `SYS:*` | 模式切换、状态查询 |

---

## 本地 TCP 配网步骤

1. 手机连接 WiFi `ESP-WASH`
2. TCP 连接 `192.168.4.1:9000`
3. 发送命令，例如：

```text
CMD:MODE0
CFG:GET
```

建议每条命令以换行 `\n` 结尾。

---

## 配网命令（CFG / SYS）

### 读取当前配置

```text
CFG:GET
```

示例应答：

```text
CFG:GET,SSID=your_wifi,WIFI_PASSWORD=your_password,SERVER_IP=192.168.1.125,SERVER_PORT=9000,OTA_URL=
```

### 设置路由器

```text
CFG:WIFI_SSID=你的WiFi名
CFG:WIFI_PASSWORD=你的WiFi密码
```

### 设置云端 TCP 服务器

```text
CFG:SERVER_IP=192.168.1.125
CFG:SERVER_PORT=9000
```

### 仅保存到 NVS（不立刻重连）

```text
CFG:SAVE
```

### 切换到 STA 工作模式

```text
SYS:MODE=STA
```

或一步完成：

```text
CFG:APPLY
```

流程：保存配置 → 切 STA → 连路由器 → 拿到 IP 后写 NVS → 自动连云端 TCP。

### 退回 AP 配网

```text
SYS:MODE=AP
```

### 查询状态

```text
SYS:STATUS
```

示例应答：

```text
SYS:STATUS,MODE=STA,STA=CONNECTED,TCP=CONNECTED,TRIAL=IDLE,PROVISIONED=YES
```

---

## 远程 OTA 固件升级

**前提**：设备已在 STA 模式且连上路由器（有 IP）。  
可通过 BLE、AP 本地 TCP、云端 TCP 下发命令。

**固件下载地址没有写死在程序里**，必须由你填写并保存（写入 NVS），例如本机 8081、NAS、内网任意 HTTP 地址。

### 1. 填写并保存 OTA 地址（必做，只需配置一次）

```text
CFG:OTA_URL=http://你的服务器IP:端口/路径/ESP-wash.bin
```

示例（本机 8081 提供固件，8080 留给网页）：

```text
CFG:OTA_URL=http://192.168.1.125:8081/ESP-wash.bin
```

成功应答：`CFG:OK,OTA_URL`

`CFG:GET` 可查看当前保存的 `OTA_URL=`（未配置时为空）。

### 2. 触发升级

```text
CMD:OTA
```

使用上一步保存在 NVS 里的地址。

### 3. 临时 URL 升级（不写 NVS）

```text
CMD:OTA=http://192.168.1.125:8081/ESP-wash-v2.bin
```

应答示例：

```text
CMD:OTA,OK,URL=http://...
CMD:OTA,ERR,NO_WIFI       # STA 未连上 WiFi
CMD:OTA,ERR,NO_URL        # 未 CFG:OTA_URL 且未带 URL 参数
CMD:OTA,ERR,BAD_URL
CMD:OTA,ERR,TRIGGER
```

升级成功后设备自动重启，运行新固件。

---

## 编译好的固件放在哪里？

### 1. 编译产物在本项目里的位置

执行 `idf.py build` 后，**用于 OTA 的固件**是：

```text
build/ESP-wash.bin
```

同目录下还有（**OTA 不用这些**）：

| 文件 | 用途 |
|------|------|
| `build/bootloader/bootloader.bin` | 仅首次 USB 烧录 |
| `build/partition_table/partition-table.bin` | 仅首次 USB 烧录 |
| `build/ESP-wash.bin` | **HTTP OTA 用这个** |

首次烧录仍用 USB：

```bash
idf.py -p <串口> flash monitor
```

### 2. OTA 时固件要放在 HTTP 服务器上

`ESP-wash.bin` **不是**拷贝进 ESP 工程目录给设备读，而是放到**能 HTTP 下载的电脑/服务器**上，且 URL 要与 `CFG:OTA_URL` **完全一致**。

**URL 由你在 `CFG:OTA_URL=` 里填写**，须与浏览器能下载的地址完全一致（含 IP、端口、路径）。

| 端口示例 | 说明 |
|----------|------|
| **8080** | 常见为本机网页，一般**不要**与 OTA 混用 |
| **8081** | 可专门放固件（`Ota_firmware/ESP-wash.bin`） |

#### 示例：本机固件目录 + Python HTTP

1. 编译并复制固件：

```bash
idf.py build
copy build\ESP-wash.bin Ota_firmware\ESP-wash.bin
```

2. 在 `Ota_firmware` 起 HTTP（端口按你环境自选，例如 8081）：

```bash
cd Ota_firmware
python -m http.server 8081
```

3. 浏览器验证能下载后，再发给设备：

```text
CFG:OTA_URL=http://<你电脑局域网IP>:8081/ESP-wash.bin
CMD:OTA
```

也可把固件挂到 Node 等任意 HTTP 静态目录，只要设备能访问该 URL。

#### 注意

- 未执行 `CFG:OTA_URL=` 就发 `CMD:OTA` 会返回 `CMD:OTA,ERR,NO_URL`
- 设备通过 **HTTP** 下载，不是 HTTPS（当前固件未校验证书）
- 服务器 IP 必须是设备在 STA 模式下**能访问到的地址**（通常与云端 TCP 同网段）
- 每次改代码重新 `idf.py build` 后，需把新的 `build/ESP-wash.bin` **再拷贝/覆盖**到 `Ota_firmware/` 或 8081 静态目录

---

## 推荐首次配网流程

1. 手机连接 `ESP-WASH`
2. TCP 连接 `192.168.4.1:9000`
3. 发送：

```text
CFG:WIFI_SSID=你的路由器
CFG:WIFI_PASSWORD=路由器密码
CFG:SERVER_IP=192.168.1.125
CFG:SERVER_PORT=9000
CFG:OTA_URL=http://你的服务器/ESP-wash.bin
CFG:APPLY
```

4. 等待 STA 连上并自动连云端
5. 若失败，约 30 秒后会回滚并重新出现 `ESP-WASH` 热点

---

## 云端远程改配置

设备已在 STA 且 TCP 连上云端后，可直接发（无需再连 AP）：

**改店铺 WiFi：**

```text
CFG:WIFI_SSID=新SSID
CFG:WIFI_PASSWORD=新密码
CFG:APPLY
```

**改云端 TCP 地址：**

```text
CFG:SERVER_IP=新IP
CFG:SERVER_PORT=9000
CFG:APPLY
```

**改 OTA 固件地址并升级：**

```text
CFG:OTA_URL=http://新服务器/new.bin
CMD:OTA
```

---

## 注意事项

- ESP32 仅支持 **2.4GHz** WiFi
- `CFG:WIFI_SSID` 是店铺路由器名，**不是**热点名 `ESP-WASH`
- AP 本地 TCP（`192.168.4.1:9000`）仅用于配网/调试
- `CFG:GET` 会返回 WiFi 密码明文，请注意安全

---

## 编译

```bash
idf.py build
idf.py -p <PORT> flash monitor
```

---

## 分区与 NVS

专用分区 `device_nvs` 存储：

| 键 | 内容 |
|----|------|
| `sn` | 设备序列号 |
| `wifi_ssid` / `wifi_pwd` | 路由器 |
| `server_ip` / `server_port` | 云端 TCP |
| `ota_url` | OTA 固件 HTTP 地址 |
| `wifi_prov` | 是否已配网成功 |

应用 OTA 分区：`ota_0` / `ota_1`（见 `partitions.csv`）。
