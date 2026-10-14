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

Understanding the boot sequence is critical. The custom rootfs has two key modifications:
- `tuya_daemon.sh` is neutered (starts with `exit`) so the stock Tuya binary never runs
- `start_tuya_ipc.sh` has a background subshell that swaps in the ArnCo RTSP binary after WiFi connects

```
1. Device powers on
2. /usr/sbin/start_tuya_ipc.sh runs:
   a. /mnt/config/tmp_test.sh executes:
      - Copies mqtt_feeder to /tmp/bin/ (in PATH)
      - Prepares the verified IR-cut hardware correction for the normal binary swap
      - Enables the live watermark and starts the delayed camera watchdog
      NOTE: The name "tmp_test.sh" cannot be changed — the stock firmware
      hardcodes this filename as a hook in start_tuya_ipc.sh (likely for
      factory testing). We repurpose it since the rootfs is read-only squashfs.
   b. tuya_daemon.sh starts but immediately exits (neutered with `exit` at top)
   c. feed_watchdog_daemon.sh starts (keeps hardware watchdog fed)
   d. Background subshell in start_tuya_ipc.sh waits for WiFi (up to 60s) + 5s
   e. Copies /usr/sbin/ak_tuya_rtsp to /tmp/bin/ak_tuya_ipc and starts it
      (ArnCo binary — initialises camera ISP/sensor and serves RTSP)
   f. Starts mqtt_feeder (found in /tmp/bin/ via PATH)
3. RTSP available at rtsp://10.0.10.129:554/jkstream
4. mqtt_feeder connects to MQTT broker, publishes HA Discovery
```

**Key insight**: The ArnCo RTSP binary (`ak_tuya_rtsp`) initialises the camera hardware (ISP, sensor, video encoder, OSD) from scratch. The ArnCo swap logic lives in the custom rootfs's `start_tuya_ipc.sh`, NOT in `tmp_test.sh`. Keep `tmp_test.sh` minimal: PATH setup, the verified camera hardware helper and the delayed camera watchdog hook. Do not duplicate the ArnCo swap in `tmp_test.sh` or it will race with `start_tuya_ipc.sh`.

**Time**: The device has no battery-backed RTC. The camera restores `save_last_date.txt` initially and its vendor time service subsequently updates the clock to local wall time. On 14 October this was verified against current Sydney time. The previous background NTP commands silently failed because `ntpd` was not in PATH; the gateway did not answer a bounded NTP test either. Those commands were removed. Do not run a competing UTC-setting NTP daemon against the vendor service, which stores local time in the system clock. No new WAN rule was added. The old claim that the overlay inherently freezes because of blocked cloud time was incorrect: the watermark flag had been disabled.

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

Version 1.2 accepts only a non-retained, non-duplicate `PRESS` feed message
while idle. Portion commands must be integers from 1 to 10; HA's integral
decimal format (for example `3.0`) is also accepted. MQTT reconnects publish
the current feeding state and portion setting. The portion setting remains
in memory, so restarting the process starts at its default of one portion.
HA automations must set and confirm portions before each feed.

The MCU's acknowledgement and idle state do not prove food was delivered.
Verify dispensing with someone present; never automatically retry an
ambiguous feed.

### Build

Requires Docker. Cross-compiles a static musl-linked ARM binary (~81K):

```bash
cd mqtt_feeder
./build.sh
```

The linker uses `-static -no-pie` for the feeder's older ARM kernel. Run
command validation on the development machine with `make -C mqtt_feeder test`.
The test uses mocked feeder hardware and does not open a UART or MQTT
connection. On the device, `mqtt_feeder --version` exits before configuration,
network or UART initialisation.

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
/mnt/config/tmp_test.sh       # Boot hook: PATH, camera hardware correction, watermark, watchdog
```

### Boot Hook (`/mnt/config/tmp_test.sh`)

This script must stay minimal. The ArnCo swap logic is in the rootfs's `start_tuya_ipc.sh` — do NOT duplicate it here or both will race.

```sh
#!/bin/sh
mkdir -p /tmp/bin
cp /mnt/config/mqtt_feeder /tmp/bin/mqtt_feeder 2>/dev/null
chmod +x /tmp/bin/mqtt_feeder 2>/dev/null

# Correct the RTSP binary's board-specific IR-cut map before its normal swap.
if [ -x /mnt/config/feeder-camera-hardware-fix.sh ]; then
  /mnt/config/feeder-camera-hardware-fix.sh >/tmp/bin/camera-hardware-fix.log 2>&1 || true
fi

# Preserve the live date overlay; the vendor time service supplies local time.
touch /mnt/config/dp_file_flag/on_osd_file

# Camera-only recovery and bounded RAM-backed logging.
if [ -x /mnt/config/feeder-camera-watch.sh ]; then
  /mnt/config/feeder-camera-watch.sh >/dev/null 2>&1 </dev/null &
fi
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

The RTSP server is provided by the ArnCo firmware binary (`ak_tuya_rtsp`), which is a modified version of the stock Tuya `ak_tuya_ipc`. The ArnCo binary includes an ONVIF/RTSP server that the stock binary lacks. It initialises all camera hardware (ISP, sensor, video encoder, OSD, audio) from scratch — it does not need the stock binary to run first.

**The binary swap happens at boot** (see Boot Flow above). The custom rootfs's `start_tuya_ipc.sh` copies ArnCo to `/tmp/bin/ak_tuya_ipc` and starts it after WiFi connects.

**OSD timestamp**: The overlay updates once per second when `/mnt/config/dp_file_flag/on_osd_file` exists. If the file is absent, the firmware can leave its startup timestamp painted onto otherwise live video. Restoring that flag fixed the stale date on 14 October 2026. The boot hook preserves it. The initial cached time is replaced when the vendor time service establishes the current local time. This installation retains its existing 12-hour display without AM/PM (13:00 Sydney appears as 1:00).

### Image orientation

This installation needs a 180-degree rotation. The ArnCo camera reads
`/mnt/config/dp_file_flag/on_flip_file`; creating that persistent empty file
enables its native flip/mirror operation. The `flip = 1` value in
`basic_config.ini` did not enable rotation on its own. No HA/FFmpeg rotation
filter is needed.

Apply the flag with `touch /mnt/config/dp_file_flag/on_flip_file` and `sync`,
then reboot the feeder while it is idle. A camera-only process restart can
leave this firmware's kernel video buffers allocated; on 14 October it failed
to recover and a normal device reboot was required. Verify a fresh frame in HA,
its live timestamp, MQTT availability and portions after reboot. To undo the
rotation, remove only that flag and reboot while idle. Never issue a food
command as a camera test.

### IR-cut hardware compatibility

The stock feeder executable drives the IR-cut coil with a 200 ms pulse on GPIO80 for day or GPIO81 for night. The installed ArnCo RTSP executable instead uses GPIO23/24 and treats GPIO81 as a speaker-enable output. This mismatch left the infrared filter in the wrong position and produced a pink daylight image. A bounded pulse using the original mapping restored neutral colours.

`scripts/feeder-camera-hardware-fix.sh` corrects only that exact ArnCo build: ten IR-cut GPIO instructions, the incompatible GPIO81 speaker initialisation/control, and the native encoder keyframe interval described below. It verifies the complete source SHA-256, creates a corrected RAM copy, verifies the complete result, then temporarily bind-mounts it over the source used by the unchanged boot script. After the normal boot script copies and starts it, the helper verifies the running file, removes the temporary mount and deletes the extra RAM copy. The immutable firmware and normal boot-time swap remain intact; only `/tmp/bin/ak_tuya_ipc` retains the correction. The helper refuses an unknown executable rather than patching arbitrary offsets.

- Original SHA-256: `5c07bcd7b0bdd0d76583ea15ca0a241f3c467afcc617204eddfe7fa8546fdf23`
- Corrected SHA-256: `295b3231418b9191b1f95ee63ea7339b941f8b8c46bdfd56a408b85ccc874add`

Automatic day/night decisions remain in the camera firmware. The helper never communicates with MQTT or the feeder MCU. To roll back, remove the hardware helper invocation from `tmp_test.sh` and reboot outside a meal window; the original flash executable is unchanged. The live timestamp flag can be retained independently. A host-side `--prepare-only` run supports checksum and byte comparison without mounts or camera actions; use `CAMERA_HW_SOURCE` and `CAMERA_HW_PATCHED` for temporary fixture paths.

### Adding to Home Assistant

Use the Generic Camera integration with an RTSP stream source. This installation uses `rtsp://127.0.0.1:8554/kitchen_camera`, backed by `/config/go2rtc.yaml` on HA. Keep RTSP transport TCP and enable **Use wallclock as timestamps** in the integration's advanced options. The feeder can omit packet timestamps; without this option HA's HLS worker reports `No dts in 7 consecutive packets`.

Do not use an HTTP URL on the RTSP port as a still-image URL. The integration generates stills from its stream. A longer FFmpeg probe may be needed when testing the raw stream because parameter sets arrive in-band rather than in SDP. HA browser playback was verified at 1920×1080 after recovery on 14 October 2026.

### Faster kitchen preview and playback

The RTSP executable hard-codes a 60-frame keyframe interval in the native hardware encoder, for both main and sub streams. At the observed output rate of ten frames per second, the raw stream supplied a keyframe every six seconds. Its RTSP SDP also omits H.264 parameter sets, so joining decoders must wait for in-band setup data. HA snapshots took 8.45 seconds or hit the ten-second timeout. Preloading alone did not resolve this.

The camera hardware helper changes the native encoder's GOP from 60 to 10 frames alongside the IR-cut corrections. ARM instruction `mov r3, #60` at virtual address `0x141df0` (file offset `1252848`) becomes `mov r3, #10`; the following halfword store writes the encoder's GOP field. Full source/output checksums guard this board-specific patch. The encoder's resolution, frame-rate target, bitrate settings and hardware implementation are retained. More frequent keyframes can change bandwidth and compression efficiency.

Keep the existing direct relay configuration:

```yaml
streams:
  kitchen_camera:
    - rtsp://FEEDER_IP:554/jkstream
```

Generic Camera continues to use `rtsp://127.0.0.1:8554/kitchen_camera`, TCP and **Use wallclock as timestamps**. Its **Preload stream** preference keeps the stream ready between dashboard visits; see HA's [camera documentation](https://www.home-assistant.io/integrations/camera/#streaming-video). HA still performs its normal relay, demux and snapshot decoding, but no additional H.264 software encoder is needed.

After the 14 October 2026 deployment and normal reboot, the device logged `gop=10` for both streams, and a raw probe measured one second between keyframes. Three authenticated HA snapshots completed successfully in 3.02–3.99 seconds. A subsequent browser live-player check reached ready state 4 at 1920×1080 in 2.13 seconds and continued playing. An earlier temporary HA software-encoding workaround was removed; the final process check showed only stream-copy FFmpeg workers and no H.264 software encoder. The normal relay and snapshot-decoding costs remain. These short startup measurements do not measure video delay behind real time or guarantee every client network's performance.

To revert only the keyframe change while preserving colour correction, restore the prior verified hardware helper and reboot outside a meal window. Its output SHA-256 is `1f37ee7a488e1f5d8b0a0d1c7ac0dba44dcb50ce3aaaff23cc46f82072baedda`; the deployed device keeps that helper as `feeder-camera-hardware-fix.pre-speedup-20261014.sh`. Removing the helper invocation entirely also removes the IR-cut correction.

### Bounded camera logs and recovery

`scripts/feeder-camera-watch.sh` is installed as `/mnt/config/feeder-camera-watch.sh`. A small hook in `tmp_test.sh` launches it once. It waits 90 seconds for normal startup, then checks every 30 seconds. If the existing camera process is missing, it starts the existing `/tmp/bin/ak_tuya_ipc`, with a 120-second retry cooldown. It never sends MQTT, UART or food commands and does not repeat the boot-time binary swap.

The RAM-backed `/tmp/rtsp.log` is trimmed above 256 KiB, retaining its newest 64 KiB and its inode. The camera's ordinary append writer therefore continues writing to the same file. Restart attempts discard their output. On 14 October a 6.8 MB log and an earlier kernel OOM kill were found on this 27 MB device. The evidence log was archived privately, reclaimed, and a normal reboot restored camera operation. A camera-only restart was insufficient after that failure: the watchdog reduces recurrence but cannot guarantee recovery of exhausted kernel camera buffers.

For a non-actuating host retention check, set `CAMERA_WATCH_LOG` and `CAMERA_WATCH_STATE_DIR` to temporary paths and run the script with `--trim-only`.

### If RTSP Stops Working

1. Check `pidof ak_tuya_ipc`, port 554, available memory, and `/tmp/rtsp.log` size.
2. Read recent log errors and `dmesg` for OOM kills. Preserve needed evidence before clearing a log.
3. Verify one `feeder-camera-watch.sh` instance and its boot hook; never start a second camera process while one is running.
4. If the camera process cannot recover after an OOM failure, perform a normal reboot outside a meal window. Allow Wi-Fi, time synchronisation and normal startup to finish.
5. Verify direct RTSP, HA playback, one MQTT client, idle status and the intended portion setting. A client restart returns portions to its firmware default; setting portions is separate from dispensing.
6. For a stale timestamp, verify `on_osd_file` and the current device clock. For pink daylight video, verify the corrected running checksum and `/tmp/bin/camera-hardware-fix.log`; do not leave either IR coil continuously energised.

## Filesystem Layout

```
/usr/sbin/
├── ak_tuya_ipc           # Stock Tuya binary (unused — tuya_daemon.sh is neutered)
├── ak_tuya_rtsp          # ArnCo binary (RTSP-enabled, initialises HW from scratch)
├── start_tuya_ipc.sh     # Modified startup script (ArnCo swap after WiFi)
├── tuya_daemon.sh        # Neutered (starts with `exit`) — prevents stock binary
├── feed_watchdog_daemon.sh  # Hardware watchdog feeder
└── product_wifi_scan.sh  # WiFi setup

/mnt/config/              # Persistent jffs2 partition
├── mqtt_feeder           # Our MQTT bridge binary
├── mqtt_feeder.conf      # MQTT config
├── tmp_test.sh           # Boot hook: PATH, camera hardware correction, watermark, watchdog
├── feeder-camera-hardware-fix.sh  # Verified IR-cut and encoder keyframe correction
├── feeder-camera-watch.sh        # Camera recovery and bounded RAM-backed logs
├── save_last_date.txt    # ArnCo's cached date (used for OSD timestamp)
├── basic_config.ini      # Device config (tz_sec, flip, night mode, etc.)
├── dev_config.ini        # Device config (volume, contrast, motion detect)
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
- `deploy_rtsp.sh` — **CAUTION**: Deploys custom `rtsp_server` and overwrites `tmp_test.sh` with a version that does NOT include the ArnCo swap. This will break RTSP. See warning below.
- `transfer_arnco.sh` — Transfer ArnCo binary to device
- `fix_device.sh` / `fix_script.expect` — Fix broken device state

### Custom RTSP Server (`rtsp_server/`) — Dead End

The `rtsp_server/` directory contains a custom RTSP server written against the Anyka SDK. **This does not work on the device** and should not be deployed. The binary links against `libmpi_osd.so` (OSD library), which pulls in device-only shared libraries (`libplat_mem.so`, `libplat_dbg.so`, `libplat_log.so`, `libplat_osal.so`) that exist on the device at `/usr/lib/` but are NOT in the SDK. These libraries provide DMA allocation, debug logging, and OSAL symbols that cannot be stubbed. Attempts to provide stub `.so` files, use `LD_PRELOAD`, or export C stub functions all failed due to the deep dependency chain.

The working RTSP solution is the ArnCo binary, not this custom code. The `rtsp_server/` code is kept for reference only.

**WARNING about `deploy_rtsp.sh`**: Running this script overwrites `/mnt/config/tmp_test.sh` on the device with a version that starts the (broken) custom `rtsp_server` instead of relying on the ArnCo swap in `start_tuya_ipc.sh`. If you accidentally run it, restore `tmp_test.sh` on the device to its correct minimal form (see Boot Flow section).

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
1. Check ArnCo is running: `ps | grep ak_tuya_ipc`
2. Check port 554: `netstat -tlnp | grep 554`
3. Check log: `grep -i "ONVIFSERVER\|rtsp" /tmp/rtsp.log`
4. If not running, reboot — the ArnCo swap in `start_tuya_ipc.sh` requires a clean boot

### Device not responding after flash
1. Connect UART (see UART Recovery section)
2. Check if U-Boot is accessible
3. If boot loops, reflash rootfs via XMODEM
4. If U-Boot is corrupted, device needs JTAG recovery (unlikely)

### Green LED not illuminated
The LED is controlled by MCU command `FF FF 11 01 02`. mqtt_feeder sends this during MCU init. If the LED is off, mqtt_feeder may not have initialised the MCU yet, or ArnCo's LED handling differs from stock.
