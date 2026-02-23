# Tuya Pet Feeder Local Control Research
## Advwin 6L Automatic Cat Feeder with Camera - Home Assistant Integration

Research date: February 2026

---

## Executive Summary

There are multiple approaches to gaining local control of a Tuya-based pet feeder,
ranging from purely software-based (no hardware modification) to full root access via
serial console. For this specific device -- an Advwin 6L with camera -- the recommended
path is the **Tuya IoT Platform + tinytuya + LocalTuya** approach, which requires zero
hardware modification and provides reliable local control of feeding, scheduling, and
sensor data. The camera stream is a separate, harder problem.

---

## Approaches Ordered by Difficulty (Easiest First)

### 1. Tuya IoT Platform + tinytuya + LocalTuya (RECOMMENDED)
**Difficulty: Low | Risk: None | Reversible: Yes**

This is the most practical, currently-working method. It does NOT replace the firmware.
Instead, it extracts the device's local encryption key, then communicates directly with
the device over your LAN using the Tuya protocol (bypassing the cloud for control).

#### Step-by-step process:

1. **Create Tuya IoT Developer Account**
   - Register at https://iot.tuya.com
   - Select "Skip this step" when asked for Account Type
   - Create a Cloud Project (pick the correct data center region for your location)
   - Note the **API ID** (Client ID) and **API Secret** (Client Secret)
   - Subscribe to the following APIs in your project:
     - IoT Core
     - Authorization Token Management
     - Smart Home Device Management
   - **Important**: The IoT Core subscription expires (default 1 month). Renew it for
     1, 3, or 6 months as needed.

2. **Link your Smart Life / Tuya Smart app account**
   - In the Tuya IoT project, go to Devices > Link Tuya App Account
   - Select "Automatic" and "Read Only Status"
   - Scan the QR code with the Smart Life app (Me tab > QR code button)

3. **Extract local keys with tinytuya**
   ```bash
   pip install tinytuya
   python -m tinytuya wizard
   ```
   - Enter API ID, API Secret, and Region when prompted
   - This generates `devices.json` containing every device's:
     - `id` (Device ID)
     - `key` (Local Key -- this is the critical piece)
     - `ip` (local IP address)
     - Device name and version

4. **Install LocalTuya or tuya-local in Home Assistant**
   - **LocalTuya** (rospogrigio fork or xzetsubou fork): Install via HACS
     - Supports protocol versions 3.1 through 3.5
     - Push-based status updates (fast)
     - Can auto-retrieve local keys via Cloud API config
   - **tuya-local** (make-all): Alternative with YAML-based device profiles
     - Has pre-built profiles for many pet feeders
     - May need manual DPS mapping for non-standard devices

5. **Configure the pet feeder entities**
   - Add the device using Device ID + Local Key
   - Map Data Points (DPS) to Home Assistant entities (see DPS section below)

#### Standard Tuya Pet Feeder Data Points (DPS):

| DP ID | Name              | Type    | Description                              |
|-------|-------------------|---------|------------------------------------------|
| 1     | Meal Plan         | Raw     | Sync feeding schedules (up to 10)        |
| 2     | Quick Feed        | Boolean | Trigger a quick feed                     |
| 3     | Manual Feed       | Value   | Manual feed, range 1-12 portions         |
| 4     | Feed State        | Enum    | standby / feeding / done                 |
| 5     | Unit Switch       | Enum    | cup / oz / grid                          |
| 6     | Slow Feed         | Boolean | Enable slow feeding mode                 |
| 11    | Battery %         | Value   | 0-100                                    |
| 14    | Fault             | Fault   | Error codes e1, e2, e3                   |
| 15    | Feed Report       | Value   | Feeding report, 0-12 portions            |
| 16    | Surplus Grain     | Value   | Remaining food 0-100%                    |
| 19    | Light             | Boolean | Night light control                      |
| 20    | Switch            | Boolean | Main device switch                       |

**Camera-equipped feeders often use higher DP IDs:**

| DP ID | Name              | Description                              |
|-------|-------------------|------------------------------------------|
| 101   | Indicator Light   | LED indicator on/off                     |
| 103   | Manual Feed       | Alternative manual feed switch           |
| 104   | Express Feed      | Quick/express feeding                    |
| 107   | Portions          | Portion count                            |
| 108   | Night Vision      | Camera night vision on/off               |
| 109   | SD Card Status    | SD card presence/health                  |
| 113   | Motion Recording  | Motion-triggered recording               |
| 115   | Motion Detection  | Motion detection images                  |
| 134   | Motion Switch     | Enable/disable motion detection          |
| 239   | Food Bucket       | Empty ("less") / Full ("full") status    |
| 245   | Feeding Command   | Trigger feeding (set value = portions)   |

**Note:** DPS mappings vary by manufacturer. The Advwin feeder may use a different subset.
Use the Tuya IoT Platform API Explorer ("Query Things Data Model") to discover exact DPS
for your specific device, or check HA logs when adding the device to see reported DPS.

#### Triggering a feed from Home Assistant:
```yaml
service: localtuya.set_dp
data:
  device_id: "your_device_id_here"
  dp: 3        # or 245, depending on your device
  value: 1     # number of portions
```

#### Limitations:
- Camera RTSP stream is NOT accessible through this method
- Some DPS values (especially feeding schedules in DP 1) use proprietary binary encoding
- The device still phones home to Tuya cloud; this method just bypasses it for control
- Local key changes if you re-pair the device; re-run tinytuya wizard to get the new key

---

### 2. tuya-local (make-all) with Pre-built Device Profiles
**Difficulty: Low | Risk: None | Reversible: Yes**

The tuya-local integration (https://github.com/make-all/tuya-local) maintains YAML
device profiles for hundreds of Tuya devices, including several pet feeder profiles.
Look for profiles like `ceres_plus_petfeeder.yaml` or search the device list.

Process:
1. Get local key using tinytuya (same as Method 1)
2. Install tuya-local via HACS
3. Add the device; tuya-local will attempt to auto-detect the device type
4. If auto-detection fails or matches wrong, manually select the closest profile

**Caveat:** Pet feeders with cameras are "erratic at providing DPS info" and may
cause false-positive device matches. Manual configuration may be necessary.

---

### 3. Tuya Cloud Integration (Official)
**Difficulty: Very Low | Risk: None | Reversible: Yes**

The official Tuya integration in Home Assistant works via cloud API. This is the
simplest setup but has drawbacks:

- All commands route through Tuya's cloud servers (latency, privacy, availability)
- Pet feeder entities may be incomplete or non-functional
- Camera feed may not be exposed
- Tuya can change API terms/rate limits at any time

Only recommended as a fallback or for camera access where local control is unavailable.

---

### 4. tuya-cloudcutter (OTA Firmware Replacement for BK7231 Devices)
**Difficulty: Medium | Risk: Medium | Reversible: Sometimes**

tuya-cloudcutter exploits a WiFi vulnerability to jailbreak Tuya devices using BK7231T,
BK7231N, or Realtek RTL8720CF chips, allowing OTA flashing of custom firmware like
OpenBeken or ESPHome (via LibreTiny).

#### Current status (early 2026):
- **480+ supported device profiles** with ~90% first-pass OTA success rate
- Works on BK7231T and BK7231N chips (common in simple Tuya devices)
- **NOT applicable to camera-equipped pet feeders** -- these use video SoCs (Anyka,
  GK7102C, Ingenic T31) that are NOT BK7231-based
- The WiFi module in the feeder may be BK7231-based, but the camera/main board
  runs a separate, more powerful SoC

#### When it IS useful:
- Simple Tuya devices (plugs, switches, bulbs, non-camera feeders)
- Devices confirmed to have BK7231 or RTL8720CF WiFi modules
- Check the device database: https://openbekeniot.github.io/webapp/devicesList.html

#### Alternative firmware options for BK7231:
- **OpenBeken** (OpenBK7231T): Tasmota-like, MQTT + HA compatible
- **ESPHome via LibreTiny**: Full ESPHome integration, auto-discovery in HA

---

### 5. tuya-convert (OTA Firmware Replacement for ESP Devices)
**Difficulty: Medium | Risk: Medium | Reversible: Sometimes**

#### Current status (early 2026): LARGELY OBSOLETE

tuya-convert was the original OTA exploit for flashing Tasmota/ESPHome onto Tuya
devices with ESP8266/ESP8285/ESP32 chips.

**Why it no longer works for most devices:**
- Tuya patched the PSK vulnerability starting January 2019
- A second-generation patch was deployed shortly after the workaround
- Manufacturers have been silently switching from ESP chips to BK7231 or Realtek
- **No ESP-based Tuya pet feeder with camera has been identified in recent years**
- The tuya-convert GitHub repo has had no meaningful updates

**Bottom line:** Only works on old stock with pre-2020 firmware on ESP chips. Do not
count on this for any device purchased in 2024+.

---

### 6. UART / Serial Console Root Access (Hardware Method)
**Difficulty: High | Risk: High | Reversible: Depends**

This involves physically opening the device, soldering to UART pads on the PCB, and
accessing the Linux serial console.

#### What we know about camera pet feeder internals:

**Typical architecture (dual-SoC design):**
- **Camera/Main SoC**: Handles video, WiFi, cloud connectivity
  - Common chips: Anyka AK3918EN080, GK7102C, Ingenic T31
  - Runs embedded Linux (kernel 3.4.x or similar)
  - Has UART console pads on PCB
- **Motor Control MCU**: Handles feeding mechanism
  - Common chip: STM8L052C6T6 (TuyaMCU)
  - Communicates with main SoC via serial (`/dev/ttySAK1`)
  - Receives feeding commands as binary serial packets
- **WiFi Module**: Separate module for network connectivity
  - Examples: BL-M8188FU3 (RTL8188FTV), Azurewave modules
  - Connected to main SoC via USB or SDIO

**3Paws Smart Pet Feeder teardown (similar device class):**
- Main SoC: GK7102C (camera processor)
- MCU: STM8L052C6T6 (TuyaMCU for motor control)
- WiFi: BL-M8188FU3 (Realtek RTL8188FTV, 802.11n 2.4GHz)
- UART pads: J2 connector, 4 through-holes, 3.3V idle, 115200 baud

**SNHACK research findings (BALIMO/PETLIBRO feeders with AK3918):**
- UART at 115200 bps on labeled RX/TX/GND pads
- Console requires login; root password was cracked: **AK2040jk**
  (hash: `root:kRVZynxtm9Jac:0:0:99999:7:::`)
- Telnet enabled on port 23 by default (may require password)
- Feeding motor controlled via serial commands to `/dev/ttySAK1`:
  - BALIMO: `echo -en "\xff\xff\x01\x01\x00" > /dev/ttySAK1`
  - PETLIBRO: `echo -en "\x55\xaa\x03\x01\x00\x01\x01\x05" > /dev/ttySAK1`
- SPI flash can be dumped for firmware extraction (Binwalk parses squashfs/jffs2)

**Anyka AK3918 SD card boot exploit:**
- The AK3918 bootloader checks for a folder named `usbnet` on SD card
- If it exists, it executes a file called `product_test` as root
- This allows arbitrary code execution without soldering
- Can be used to enable telnet, dump firmware, install backdoors
- FTP may also be open on some devices (no password) at `/etc/jffs2/`

**Ingenic T31 (alternative SoC found in some Tuya cameras):**
- Thingino (https://thingino.com/) is an open-source firmware for Ingenic SoC cameras
- Supports RTSP streaming, ONVIF, MQTT
- Installation via SD card image
- If the Advwin feeder uses T31, Thingino could provide full camera + local control
- Would need to confirm the specific SoC via teardown

#### UART access procedure (general):
1. Open the device (void warranty)
2. Identify UART pads (RX, TX, GND) -- usually 3-4 through-holes near the SoC
3. Connect USB-to-UART adapter (3.3V logic level, NOT 5V)
4. Open serial terminal at 115200 baud, 8N1
5. Power on device; watch for U-Boot and Linux boot messages
6. Press key during U-Boot to enter bootloader (if not locked)
7. Once at Linux login, try: root / (no password), root / AK2040jk, or other defaults
8. If locked, try SD card exploit or SPI flash dump

#### Risks:
- Soldering to small pads can damage the PCB
- Bricking is possible if you overwrite flash incorrectly
- No going back if you destroy boot partitions without a backup
- The device likely uses different firmware than documented devices

---

## Chip Identification Summary

Camera-equipped Tuya pet feeders do NOT typically use ESP chips. The common SoC families:

| SoC Family     | Examples              | Used For         | Alt Firmware?          |
|----------------|-----------------------|------------------|------------------------|
| Anyka          | AK3918EN080           | Camera + main    | Custom rootfs via SD   |
| Goke           | GK7102C               | Camera + main    | Limited                |
| Ingenic        | T31, T20              | Camera + main    | Thingino, OpenIPC      |
| Realtek WiFi   | RTL8188FTV            | WiFi module      | No (not main SoC)      |
| BK7231         | BK7231T/N             | WiFi (no camera) | OpenBeken, ESPHome     |
| STM8           | STM8L052C6T6          | Motor MCU        | No (TuyaMCU protocol)  |
| ESP            | ESP8266/ESP32         | Rare in cameras  | Tasmota, ESPHome       |

**For the Advwin 6L with camera:** Most likely uses an Anyka, Goke, or Ingenic SoC for
the camera/main processing, with a separate TuyaMCU (STM8 or similar) for motor control.
The only way to confirm is to open the device.

---

## Recommended Strategy for the Advwin 6L

### Phase 1: Local Control Without Hardware Modification
1. Set up Tuya IoT Developer account
2. Run tinytuya wizard to extract Device ID and Local Key
3. Install LocalTuya (xzetsubou fork) via HACS
4. Configure entities using discovered DPS values
5. Create HA automations for feeding schedules
6. Use tinytuya for scripting/debugging: `python -m tinytuya scan`

### Phase 2: Investigate Camera Access
1. Check if the Tuya cloud integration exposes camera entities
2. Try the RTSP URL patterns common to Tuya cameras (unlikely to work without hacking)
3. If camera access is critical, proceed to Phase 3

### Phase 3: Hardware Investigation (Optional)
1. Open the device and photograph the PCB
2. Identify the main SoC, WiFi module, and MCU chips
3. Locate UART pads
4. Based on the SoC:
   - **Ingenic T31**: Try Thingino firmware via SD card
   - **Anyka AK3918**: Try SD card `usbnet/product_test` exploit
   - **GK7102C**: Limited options; may need SPI flash dump
5. If RTSP stream is obtained, add to HA as a generic camera

### Phase 4: Full Custom Firmware (Advanced, Optional)
1. Dump original firmware via SPI or UART
2. Back up everything
3. Flash Thingino or custom rootfs
4. Re-implement motor control via serial commands
5. Set up MQTT/RTSP for full local integration

---

## Key Tools Reference

| Tool              | Purpose                           | URL                                              |
|-------------------|-----------------------------------|--------------------------------------------------|
| tinytuya          | Local key extraction + control    | https://github.com/jasonacox/tinytuya            |
| LocalTuya         | HA integration (local)            | https://github.com/rospogrigio/localtuya         |
| LocalTuya (fork)  | HA integration (actively maintained) | https://xzetsubou.github.io/hass-localtuya/   |
| tuya-local        | HA integration (YAML profiles)    | https://github.com/make-all/tuya-local           |
| tuya-cloudcutter  | OTA flash BK7231 devices          | https://github.com/tuya-cloudcutter/tuya-cloudcutter |
| tuya-convert      | OTA flash ESP devices (obsolete)  | https://github.com/ct-Open-Source/tuya-convert   |
| OpenBeken         | Alt firmware for BK7231           | https://github.com/openshwprojects/OpenBK7231T_App |
| Thingino          | Alt firmware for Ingenic cameras  | https://thingino.com/                            |
| LibreTiny/ESPHome | ESPHome for non-ESP chips         | https://esphome.io/components/libretiny/         |
| Binwalk           | Firmware extraction tool          | https://github.com/ReFirmLabs/binwalk            |

---

## Sources

- tuya-convert GitHub: https://github.com/ct-Open-Source/tuya-convert
- Tasmota tuya-convert docs: https://tasmota.github.io/docs/Tuya-Convert/
- tuya-cloudcutter GitHub: https://github.com/tuya-cloudcutter/tuya-cloudcutter
- tinytuya GitHub: https://github.com/jasonacox/tinytuya
- tinytuya PyPI: https://pypi.org/project/tinytuya/
- LocalTuya GitHub: https://github.com/rospogrigio/localtuya
- LocalTuya fork docs: https://xzetsubou.github.io/hass-localtuya/
- tuya-local GitHub: https://github.com/make-all/tuya-local
- Tuya IoT Platform: https://developer.tuya.com/en/docs/iot/pet-feeder-product-function-definition?id=K9tlinn9lgmrq
- Tuya Pet Camera configs: https://developer.tuya.com/en/docs/iot-device-dev/pet_feeder_solution?id=Kay7r9zu4rupm
- SNHACK Attack research: https://www.whid.ninja/blog/snhack-attack-how-hackers-could-turn-your-smart-pet-feeder-into-an-all-you-can-eat-buffet
- 3Paws pet feeder teardown: https://www.elektroda.com/news/news3990557.html
- Anyka camera hacking: https://ricardojlrufino.wordpress.com/2022/02/14/hack-ipcam-anyka-teardown-and-root-access/
- Anyka SD card rootfs: https://ricardojlrufino.wordpress.com/2022/02/15/hack-ipcam-anyka-booting-rootfs-from-sdcard/
- Anyka hacking journey: https://github.com/VGerris/Anyka_ak3918_hacking_journey
- Thingino firmware: https://thingino.com/
- OpenBeken: https://github.com/openshwprojects/OpenBK7231T_App
- LibreTiny/ESPHome: https://esphome.io/components/libretiny/
- LocalTuya pet feeder guide: http://blog.ov1d1u.net/2022/11/configure-tuya-pet-feeder-with-home.html
- Local key guide: https://www.remcokersten.nl/posts/get-tuya-localkey/
- HA pet feeder entities discussion: https://community.home-assistant.io/t/pet-feeder-entities/454077
- Neil Turner tuya-cloudcutter: https://neilturner.me.uk/2025/07/11/tuya-cloudcutter/
- Digiblur cloudcutter guide: https://digiblur.com/2024/12/13/tuya-cloudcutter-with-esphome-bk7231-how-to-guide-home-assistant/
