# Advwin Pet Feeder - Local Control

Custom firmware for the Advwin 6L pet feeder, replacing Tuya cloud dependency with local RTSP video streaming and MQTT feeder control via Home Assistant.

## Device Info

| Field | Value |
|-------|-------|
| Model | V330L_PT01V_OEM (Advwin 6L) |
| SoC | Anyka AK3918EV330 (ARM926EJ-S, ARMv5TE) |
| RAM | 64MB |
| Kernel | Linux 4.4.192 |
| Sensor | SmartSens SC223A (1920x1080) |
| WiFi | RTL8188FTV |
| MCU | Unknown (motor + button + LED control) |
| MCU Firmware | 3-C6-PT01V_TY.1.7 |
| IP | 10.0.10.129 |
| Root Login | root / AK2040jk (via telnet) |
| Tuya Device ID | *(redacted)* |
| Tuya Local Key | *(redacted — extract with tinytuya)* |
| Tuya Protocol | 3.2 |

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ Home Assistant                                              │
│  ├── RTSP Camera ← rtsp://10.0.10.129:554/jkstream         │
│  ├── Feed Button → MQTT → mqtt_feeder on device             │
│  └── Portions Slider → MQTT → mqtt_feeder on device         │
└─────────────────────────────────────────────────────────────┘
                              │
                    ┌─────────┴──────────┐
                    │  Pet Feeder Device  │
                    │                     │
                    │  ak_tuya_ipc (ArnCo)│ ← RTSP server on :554
                    │  mqtt_feeder        │ ← MQTT bridge
                    │        │            │
                    │  /dev/ttySAK1       │ ← 9600 8N1
                    │        │            │
                    │  [Motor MCU]        │ ← JKPer serial protocol
                    └─────────────────────┘
```

## Boot Flow

Understanding the boot sequence is critical — the RTSP camera only works if the stock binary initialises hardware first.

```
1. Device powers on
2. /usr/sbin/start_tuya_ipc.sh runs:
   a. /mnt/config/tmp_test.sh executes (copies mqtt_feeder to /tmp/bin/ for PATH)
      NOTE: The name "tmp_test.sh" cannot be changed — the stock firmware
      hardcodes this filename as a hook in start_tuya_ipc.sh (likely for
      factory testing). We repurpose it since the rootfs is read-only squashfs.
   b. Stock ak_tuya_ipc starts via tuya_daemon.sh (initialises camera ISP, audio, etc.)
   c. feed_watchdog_daemon.sh starts (keeps hardware watchdog fed)
   d. Background subshell waits for WiFi (up to 60s)
   e. After WiFi: kills stock binary + tuya_daemon.sh
   f. Copies /usr/sbin/ak_tuya_rtsp to /tmp/bin/ak_tuya_ipc and starts it
      (ArnCo binary — has RTSP server, reuses stock's hardware init)
   g. Starts mqtt_feeder (found in /tmp/bin/ via PATH)
3. RTSP available at rtsp://10.0.10.129:554/jkstream
4. mqtt_feeder connects to MQTT broker, publishes HA Discovery
```

**Key insight**: The ArnCo RTSP binary (`ak_tuya_rtsp`) cannot initialise the camera hardware from scratch. The stock `ak_tuya_ipc` must run first to set up the ISP/sensor, then ArnCo takes over and adds RTSP. If you start ArnCo directly without the stock binary running first, you get video encoding but no RTSP server.

## Network Ports

| Port | Service | Purpose |
|------|---------|---------|
| 23 | telnetd | Root shell |
| 554 | ak_tuya_ipc (ArnCo) | RTSP video stream |
| 6668 | ak_tuya_ipc (ArnCo) | Tuya local protocol |
| 9990 | jksdk_tcp | TCP command execution |

## MCU Serial Protocol (JKPer)

The motor MCU communicates over `/dev/ttySAK1` at 9600 baud 8N1 using the JKPer protocol (NOT TuyaMCU).

### Packet Format

```
FF FF <cmd> <len> <data...>
^^^^                Header (always FF FF)
      ^^^           Command byte
            ^^^     Data length
                 ^^ Payload (len bytes)
```

### MCU Initialisation Sequence (Required)

The MCU must be initialised before feed commands work. This sequence mirrors what the stock binary does at startup:

| Step | Command | Purpose | Expected Response |
|------|---------|---------|-------------------|
| 1 | `FF FF 0D 01 01` | Query MCU version | 21 bytes with version string "3-C6-PT01V_TY.1.7" |
| 2 | `FF FF 1D 01 00` | Init command | Brief ack |
| 3 | `FF FF 11 01 02` | LED on (link indicator) | Brief ack |
| 4 | `FF FF 06 06 YY MM DD HH MM SS` | Time sync (BCD, year since 2000) | Brief ack |

### Feed Command

```
FF FF 0F 01 01
      ^^       Command 0x0F = FEED (triggers one motor cycle)
         ^^    Length = 1
            ^^ Data = 0x01 (always 0x01, data byte does NOT control portions)
```

Each `0x0F` command triggers exactly one motor rotation (one portion). For multiple portions, send the command multiple times, waiting for the MCU response between each.

**Important**: Command `0x01` (documented in some BALIMO research) does NOT trigger the motor — it gets an echo response but no physical action. The correct feed command is `0x0F`.

### Other Known Commands

| Command | Data | Purpose |
|---------|------|---------|
| `0x06` | 6 bytes (time) | Time sync |
| `0x0B` | `0x01` | Unknown (two response packets) |
| `0x0C` | N | Set portion count on MCU display |
| `0x0D` | `0x01` | Query MCU firmware version |
| `0x0F` | `0x01` | **Trigger one feed cycle** |
| `0x10` | `0x01` | Unknown (no motor) |
| `0x11` | `0x02` | LED on (WiFi link indicator) |
| `0x1D` | `0x00` | Init/reset command |
| `0x20` | varies | MCU status report (unsolicited) |
| `0x32` | `0x02` | Config query |

### MCU Response Format

Responses use the same `FF FF` header, or sometimes `FF FC`:
- `FF FF 0C 01 0A` — portion count acknowledgement
- `FF FF 20 01 03` — status report
- `FF FC 07 05 ...` — extended status
- `FF FC 0F 03 ...` — feed completion status

### Manual Feed Test via Telnet

```bash
# Must init MCU first (or let mqtt_feeder do it)
echo -en '\xff\xff\x0f\x01\x01' > /dev/ttySAK1
```

## MQTT Feeder Bridge

The `mqtt_feeder/` directory contains a lightweight MQTT bridge that runs on the device and exposes feeding controls to Home Assistant via MQTT Discovery.

### HA Entities (Auto-Discovered)

| Entity | Type | What it does |
|--------|------|-------------|
| Pet Feeder: Feed | Button | Dispenses configured number of portions |
| Pet Feeder: Portions | Number (1-10) | Set how many portions per feed |
| Pet Feeder: Feeding | Binary sensor | feeding/idle status |

### Build

Requires Docker. Cross-compiles a static musl-linked ARM binary (~81K):

```bash
cd mqtt_feeder
./build.sh
```

### Deploy

Quick redeploy of a running device (kills old process, downloads new binary, starts):

```bash
# Edit tools/redeploy.exp with correct IP/credentials, then:
expect tools/redeploy.exp
```

Or manually via telnet:
```bash
telnet 10.0.10.129
# On device:
killall mqtt_feeder
wget -q http://<your-ip>:8888/mqtt_feeder_v11 -O /tmp/mqtt_feeder
chmod +x /tmp/mqtt_feeder
/tmp/mqtt_feeder &
```

### Device Files (Persistent Across Reboots)

All persistent files are on `/mnt/config/` (jffs2, writable):

```
/mnt/config/mqtt_feeder       # Static ARM binary (latest build)
/mnt/config/mqtt_feeder.conf  # MQTT broker config
/mnt/config/tmp_test.sh       # Boot script: copies mqtt_feeder to /tmp/bin/ (in PATH)
```

### Config File (`/mnt/config/mqtt_feeder.conf`)

```ini
broker_host=<your-mqtt-broker>
broker_port=1883
broker_user=homeassistant
broker_pass=<password>
serial_path=/dev/ttySAK1
```

### How Multi-Portion Feeding Works

The MCU only supports one motor cycle per `0x0F` command. mqtt_feeder implements multi-portion feeding with a state machine:

```
IDLE → send 0x0F → FEEDING (wait for MCU response)
                         ↓
              portions_done < target?
                    ↓ yes          ↓ no
              WAIT_NEXT (1s)     IDLE (done)
                    ↓
              send 0x0F → FEEDING
```

## RTSP Camera

### Stream URL

```
rtsp://10.0.10.129:554/jkstream
```

- Resolution: 1920x1080 (main) + 640x360 (sub)
- Codec: H.264 Main Profile
- Frame rate: 10-20 fps (auto adjusted by ISP for lighting)
- Audio: Yes (8kHz PCM with echo cancellation)

### How It Works

The RTSP server is provided by the ArnCo firmware binary (`ak_tuya_rtsp`), which is a modified version of the stock Tuya `ak_tuya_ipc`. The ArnCo binary includes an ONVIF/RTSP server that the stock binary lacks.

**The binary swap happens at boot** (see Boot Flow above). The stock binary must initialise the camera hardware (ISP, sensor, video encoder) before the ArnCo binary can take over and serve RTSP.

### Adding to Home Assistant

In `configuration.yaml`:
```yaml
camera:
  - platform: generic
    name: Pet Feeder Camera
    stream_source: rtsp://10.0.10.129:554/jkstream
    still_image_url: http://10.0.10.129:554/jkstream  # won't work, use stream
```

Or add via UI: Settings → Devices → Add Integration → Generic Camera → enter RTSP URL.

### If RTSP Stops Working

1. Check ArnCo binary is running: `ps | grep ak_tuya_ipc`
2. Check port 554: `netstat -tlnp | grep 554`
3. If not running, the stock binary probably didn't init hardware. Reboot the device.
4. Check `/tmp/rtsp.log` for errors
5. Look for `DEV_SUPPORT_ONVIFSERVER start` in the log — this means RTSP activated

## Filesystem Layout

```
/usr/sbin/
├── ak_tuya_ipc           # Stock Tuya binary (camera HW init, no RTSP)
├── ak_tuya_rtsp          # ArnCo binary (RTSP-enabled, needs stock init first)
├── start_tuya_ipc.sh     # Main startup script (stock → ArnCo swap logic)
├── tuya_daemon.sh        # Watchdog wrapper for ak_tuya_ipc
├── feed_watchdog_daemon.sh  # Hardware watchdog feeder
└── product_wifi_scan.sh  # WiFi setup

/mnt/config/              # Persistent jffs2 partition
├── mqtt_feeder           # Our MQTT bridge binary
├── mqtt_feeder.conf      # MQTT config
├── tmp_test.sh           # Boot hook (copies mqtt_feeder to PATH)
├── basic_config.ini      # Device config
├── dev_config.ini        # Device config
├── tuya_user.db          # Tuya credentials (needed for ArnCo)
└── wpa_conf              # WiFi credentials

/tmp/bin/                 # tmpfs, in PATH, populated at boot
├── ak_tuya_ipc           # → copied from /usr/sbin/ak_tuya_rtsp at boot
└── mqtt_feeder           # → copied from /mnt/config/mqtt_feeder at boot
```

**Important**: The rootfs is read-only squashfs. Only `/mnt/config/` (jffs2) and `/tmp/` (tmpfs) are writable. To modify startup scripts in the rootfs, you must rebuild and reflash the squashfs image.

## UART Recovery

If the device is bricked (bad rootfs flash, boot loop, etc.), you can recover via UART.

### Hardware

- 3 UART pads on the board (see `photos/` for locations)
- 3.3V TTL serial, 115200 baud
- Pinout (left to right, arrow marking on left pin):
  - **Left (arrow)** = GND
  - **Middle** = RXD
  - **Right** = TXD
- Connect TX, RX, GND (do NOT connect VCC)

### U-Boot Access

1. Connect UART, open serial terminal at 115200 baud
2. Power on the device
3. Rapidly press keys during "Hit any key to stop autoboot"
4. Use `scripts/catch_uboot*.sh` for automated interception

### Reflashing Rootfs via UART

If rootfs is corrupted and device won't boot to Linux:

```
# In U-Boot:
loadb 0x80000000                    # Start XMODEM receive
# Send rootfs.bin via XMODEM from your terminal
sf probe 0                          # Init SPI flash
sf erase 0x280000 0x500000          # Erase rootfs partition
sf write 0x80000000 0x280000 <size> # Write rootfs
reset                               # Reboot
```

**Flash layout** (SPI NOR):
| Offset | Size | Partition |
|--------|------|-----------|
| 0x000000 | 256K | U-Boot |
| 0x040000 | 64K | U-Boot env |
| 0x050000 | 2.2M | Kernel (uImage) |
| 0x280000 | 5M | Rootfs (squashfs) |
| Remaining | ~8.5M | Config + data |

### Building a Custom Rootfs

The rootfs is squashfs with specific Anyka parameters:
```bash
mksquashfs squashfs-root rootfs.bin -comp xz -b 131072 -noappend
```

## Tools

### MCU Protocol Tools (`tools/`)

Cross-compiled ARM test binaries for probing the MCU:

- `mcu_probe.c` — Scans all commands 0x00-0xFF, logs responses
- `mcu_probe2.c` — Full init sequence replay + targeted command scan
- `mcu_feed_test.c` — Tests 6 feed command candidates with pauses to observe motor

Build with the same Docker cross-compiler:
```bash
docker run --rm -v $(pwd):/build mqtt_feeder_builder \
  arm-linux-musleabi-gcc -static -Os -march=armv5te -o mcu_probe tools/mcu_probe.c
```

### Expect Scripts (`tools/`)

- `redeploy.exp` — Kill old mqtt_feeder, download new build, start it
- `deploy_and_test.exp` — Full deploy with log verification
- `run_feed_test.exp` — Deploy and run MCU feed test tool

### Device Scripts (`scripts/`)

- `catch_uboot*.sh` — Intercept U-Boot for UART recovery
- `recover_uart_bootloop.sh` — Full UART recovery procedure
- `detect_baud.sh` — Auto-detect serial baud rate
- `deploy_rtsp.sh` — Deploy RTSP-related files
- `transfer_arnco.sh` — Transfer ArnCo binary to device
- `fix_device.sh` / `fix_script.expect` — Fix broken device state

## UDM Firewall

Block cloud access to keep the device local-only:

In UniFi Network → Settings → Firewall & Security → Firewall Rules:

- **Name:** Block Pet Feeder Internet
- **Type:** LAN Out
- **Action:** Drop
- **Source:** IP 10.0.10.129
- **Destination:** Any (except RFC1918 private ranges)

This keeps LAN communication (RTSP, MQTT, telnet) working while preventing Tuya cloud phone-home.

## Troubleshooting

### Feed button doesn't dispense food
1. Check mqtt_feeder is running: `ps | grep mqtt_feeder`
2. Check serial port: `ls -la /dev/ttySAK1`
3. Check MQTT connection in log: `cat /tmp/mqtt_feeder.log`
4. Verify MCU init completed: look for "MCU init complete" in log
5. If ArnCo is also using serial port, there may be contention on /dev/ttySAK1

### RTSP stream not available
1. Ensure device was cleanly booted (stock binary must run first)
2. Check ArnCo is running: `ps | grep ak_tuya_ipc`
3. Check port 554: `netstat -tlnp | grep 554`
4. Check log: `grep -i "ONVIFSERVER\|rtsp" /tmp/rtsp.log`
5. If missing, reboot the device — the stock→ArnCo swap must happen in order

### Device not responding after flash
1. Connect UART (see UART Recovery section)
2. Check if U-Boot is accessible
3. If boot loops, reflash rootfs via XMODEM
4. If U-Boot is corrupted, device needs JTAG recovery (unlikely)

### Green LED not illuminated
The LED is controlled by MCU command `FF FF 11 01 02`. mqtt_feeder sends this during MCU init. If the LED is off, mqtt_feeder may not have initialised the MCU yet, or ArnCo's LED handling differs from stock.
