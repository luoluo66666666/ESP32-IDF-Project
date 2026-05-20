# ESP-Wash

ESP-Wash is an ESP32-S3 device project with three control/configuration paths:

- BLE
- Local Wi-Fi AP + local TCP
- STA Wi-Fi + upstream TCP server

## Local Config Mode

On boot, the device starts in AP config mode by default:

- AP SSID: `ESP-WASH`
- AP password: `12345678`
- Local TCP server: `192.168.4.1:9000`

In this mode, a phone can connect to the device hotspot and send commands locally.

## STA Work Mode

After `SYS:MODE=STA`, the device switches to STA work mode:

- The local AP/TCP config path is closed
- The device connects to the configured router SSID/password
- Then the device connects to the configured upstream TCP server

If the STA Wi-Fi connection does not succeed within about 30 seconds, the device rolls back to the previous config and returns to AP config mode.

## Command Paths

- `CMD:*` business commands can be sent by BLE or local TCP
- `CFG:*` updates configuration values
- `SYS:*` switches operating mode and reports network status

## Local TCP Usage

1. Connect the phone to `ESP-WASH`
2. Open a TCP connection to `192.168.4.1:9000`
3. Send commands such as:

```text
CMD:MODE0
CFG:GET
```

The local TCP server accepts both:

- newline-terminated commands
- single packet commands without `\n`

## Configuration Commands

### Read current config

```text
CFG:GET
```

Example response:

```text
CFG:GET,SSID=your_wifi,WIFI_PASSWORD=your_password,SERVER_IP=192.168.1.125,SERVER_PORT=9000
```

### Set target router SSID

```text
CFG:WIFI_SSID=your_wifi_name
```

### Set target router password

```text
CFG:WIFI_PASSWORD=your_wifi_password
```

### Set upstream TCP server IP

```text
CFG:SERVER_IP=192.168.1.125
```

### Set upstream TCP server port

```text
CFG:SERVER_PORT=9000
```

### Save config only

```text
CFG:SAVE
```

This writes the current config to `device_nvs`, but does not switch modes immediately.

### Switch to STA work mode

```text
SYS:MODE=STA
```

This will:

1. Save the current config to NVS if needed
2. Leave AP config mode
3. Switch to STA work mode
4. Connect to the configured Wi-Fi router
5. Connect to the configured TCP server

### Return to AP config mode

```text
SYS:MODE=AP
```

This command switches the device back to AP config mode.

### Query network mode/status

```text
SYS:STATUS
```

Example response:

```text
SYS:STATUS,MODE=AP,STA=DISCONNECTED,TCP=DISCONNECTED,TRIAL=IDLE
```

## Recommended Config Flow

1. Connect phone to `ESP-WASH`
2. Connect TCP to `192.168.4.1:9000`
3. Send:

```text
CFG:WIFI_SSID=your_router
CFG:WIFI_PASSWORD=your_password
CFG:SERVER_IP=192.168.1.125
CFG:SERVER_PORT=9000
CFG:SAVE
SYS:MODE=STA
```

4. The device switches to STA work mode
5. If connect fails, wait about 30 seconds for auto rollback
6. Reconnect phone to `ESP-WASH` and continue configuration if needed

## Notes

- ESP32 can only connect to `2.4 GHz` Wi-Fi
- `CFG:WIFI_SSID` / `CFG:WIFI_PASSWORD` configure the router the device will join as STA
- They do not change the SoftAP name `ESP-WASH`
- The local AP/TCP path is for configuration/debug in AP config mode
- For best results, use BLE for `SYS:MODE=STA` / `SYS:MODE=AP` mode switching

## Build

```bash
idf.py build
idf.py -p <PORT> flash monitor
```

## Partition Notes

The project uses a dedicated `device_nvs` partition to store:

- device SN
- router SSID/password
- upstream server IP/port
