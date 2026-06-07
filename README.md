# ESP32-C6 4-Relay Controller

## Hardware

| Function | GPIO | Notes |
|---|---|---|
| Relay 0 | 19 | Active LOW |
| Relay 1 | 20 | Active LOW |
| Relay 2 | 21 | Active LOW |
| Relay 3 | 22 | Active LOW |
| Wi-Fi Reset | 11 | Pull to GND, hold 3 s |

Active LOW: GPIO HIGH = relay off, GPIO LOW = relay on.

## Build & Flash

Requires ESP-IDF v6.0.1. Inside the devcontainer all commands work as-is.

```bash
idf.py set-target esp32c6        # first time only
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Wi-Fi Setup

Provisioning uses BLE. The device advertises as `relay_XXYYZZ` (last 3 bytes of MAC).

1. Install **ESP BLE Provisioning** (Espressif, available on iOS and Android)
2. Power on the device
3. Open the app → scan → select `relay_XXYYZZ` → enter Wi-Fi credentials
4. Credentials are saved to NVS and survive reboots

**To re-provision:** hold GPIO 11 to GND for 3 seconds. The device erases credentials and restarts into provisioning mode.

## UDP Protocol

Port **12345**, JSON-RPC style. All requests are optional `id` for response correlation.

### get_sysinfo

```json
{"id": 1, "method": "get_sysinfo", "params": {}}
```

```json
{
  "id": 1,
  "result": {
    "mac": "aa:bb:cc:dd:ee:ff",
    "ip": "192.168.1.100",
    "type": "relay4",
    "version": "1.0",
    "children": [
      {"id": 0, "alias": "relay_0", "on": 0},
      {"id": 1, "alias": "relay_1", "on": 1},
      {"id": 2, "alias": "relay_2", "on": 0},
      {"id": 3, "alias": "relay_3", "on": 0}
    ]
  }
}
```

### set_relay_state

Partial update supported — only include the relays you want to change.

```json
{"id": 2, "method": "set_relay_state", "params": {"children": [{"id": 0, "on": 1}, {"id": 2, "on": 0}]}}
```

Response returns the full state of all relays.

### set_alias

```json
{"id": 3, "method": "set_alias", "params": {"children": [{"id": 0, "alias": "living room"}, {"id": 1, "alias": "kitchen"}]}}
```

Aliases are persisted in NVS.

### Errors

```json
{"id": 1, "error": {"code": -32600, "message": "invalid request"}}
{"id": 1, "error": {"code": -32601, "message": "method not found"}}
```

## Quick Test

**Linux / macOS:**
```bash
echo '{"id":1,"method":"get_sysinfo","params":{}}' | nc -u -w1 <device-ip> 12345
```

**Windows PowerShell:**
```powershell
$udp = New-Object System.Net.Sockets.UdpClient
$udp.Connect("<device-ip>", 12345)
$bytes = [Text.Encoding]::UTF8.GetBytes('{"id":1,"method":"get_sysinfo","params":{}}')
$udp.Send($bytes, $bytes.Length)
$udp.Close()
```
