# Wi-Fi Health Detector

An ESP32 gadget that shows how good your Wi-Fi signal is where it's sitting. A small OLED screen shows the network name, the signal strength in dBm with a 4-bar meter, and the current date and time. Three LEDs (green, yellow, red) show the signal quality at a glance from across the room.

You don't need to put Wi-Fi credentials in the code. On first boot the device shows a QR code. You scan it with Espressif's provisioning app on your phone and choose your network there. The device saves the credentials and reconnects on its own after that.

```
+-----------------------------+
| Wi-Fi SSID:                 |
| MyHomeNetwork               |
|                             |
| -52 dBm   ▂▄▆█              |
|                             |
| 10/25/2026 14:03:27         |
+-----------------------------+
```

## Features

- Live signal strength (RSSI) of the connected access point, updated every second
- 0–4 bar signal meter and traffic-light LEDs
- Wi-Fi setup by QR code (ESP SoftAP provisioning), with no SSID or password in the source
- Credentials stored in flash (NVS), so later boots connect automatically
- Date and time synced from `pool.ntp.org`
- No graphics library: the OLED is driven through ESP-IDF's `esp_lcd` SSD1306 driver, using a small framebuffer and a built-in 5×7 font

## Hardware

| Part | Notes |
|---|---|
| ESP32 dev board | Any classic ESP32 board (e.g. ESP32-WROOM-32 DevKit). The config assumes at least 2 MB of flash. |
| SSD1306 OLED, 128×64, I2C | The common 0.96" 4-pin module, address `0x3C` |
| 3 LEDs | Red, yellow, green |
| 3 resistors, 220–330 Ω | One in series with each LED |
| Breadboard, jumper wires, USB cable | |
| Phone | With Espressif's provisioning app (see [Connecting to Wi-Fi](#connecting-to-wi-fi)) |

The ESP32 only supports **2.4 GHz** Wi-Fi. It can't see or join 5 GHz-only networks.

### Wiring

| From | To ESP32 |
|---|---|
| OLED VCC | 3V3 |
| OLED GND | GND |
| OLED SDA | GPIO 21 |
| OLED SCL | GPIO 22 |
| Red LED (anode, via resistor) | GPIO 13 |
| Yellow LED (anode, via resistor) | GPIO 14 |
| Green LED (anode, via resistor) | GPIO 27 |
| All LED cathodes (short leg) | GND |

The firmware turns on the ESP32's internal pull-ups on SDA and SCL. Most OLED modules also have their own pull-ups, so you don't need extra resistors on the I2C lines.

## Building and flashing

### Requirements

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.1.4/esp32/get-started/index.html) **v5.1.4**, the version this project was built with. Other v5.x releases should also work. ESP-IDF v6.0 moved the Wi-Fi provisioning component out of ESP-IDF, so the project won't build there without changes.
- Either the ESP-IDF terminal (command line) or the [ESP-IDF extension for VS Code](https://github.com/espressif/vscode-esp-idf-extension)

### Steps

1. Clone the repository:
   ```
   git clone https://github.com/<your-username>/wifi-health-detector.git
   cd wifi-health-detector
   ```
2. Build. On the first build, the component manager downloads the `espressif/qrcode` component automatically.
   ```
   idf.py build
   ```
   The included `sdkconfig` already targets the ESP32. **Don't run `idf.py set-target`**, because that replaces `sdkconfig` with the defaults.
3. Plug in the board and flash it, then open the serial monitor. Replace `PORT` with your board's port: `COM3` or similar on Windows (check Device Manager), `/dev/ttyUSB0` on Linux, or `/dev/cu.usbserial-*` on macOS.
   ```
   idf.py -p PORT flash monitor
   ```
   Press `Ctrl+]` to leave the monitor.

If you're using the VS Code extension, open the folder and use the **Build**, **Flash** and **Monitor** buttons in the status bar instead.

## Connecting to Wi-Fi

On first boot (or after erasing flash), there are no saved credentials, so the device starts in provisioning mode:

1. The OLED shows a QR code, and the device creates an open Wi-Fi network called `PROV_XXXXXX`. The last six characters come from the board's MAC address.
2. On your phone, install Espressif's provisioning app:
   - Android: **ESP SoftAP Provisioning**
   - iOS: **ESP SoftAP Prov**
3. In the app, scan the QR code on the OLED. The app connects to the `PROV_XXXXXX` network. On iOS you may need to join it manually in Settings. The phone may warn that this network has no internet; that's expected.
4. Pick your home network in the app and enter its password.
5. The device saves the credentials and connects. After a few seconds, while it syncs the clock, the screen switches to the signal display.

On later boots the device connects straight away, with no QR code.

### Switching to a different network

Erase the flash so the saved credentials are removed, then flash again. The QR code comes back on the next boot.
```
idf.py -p PORT erase-flash
idf.py -p PORT flash monitor
```

## Reading the display

RSSI is measured in dBm and is always negative. The closer it is to 0, the stronger the signal.

| RSSI | Bars | LED | Meaning |
|---|---|---|---|
| −55 dBm or stronger | 4 | Green | Excellent |
| −56 to −65 dBm | 3 | Yellow | Good |
| −66 to −75 dBm | 2 | Yellow | Fair |
| −76 to −85 dBm | 1 | Red | Weak |
| Below −85 dBm | 0 | (not updated) | Very weak or about to drop |

The serial monitor logs the RSSI and the time every second, which is handy for comparing spots around the house.

## How it works

1. **Startup:** sets up the OLED over I2C, the LED pins and NVS (the flash storage where Wi-Fi credentials are kept).
2. **Wi-Fi** (`wifi_start()` in `main/wifi.c`): if credentials are already saved, it connects as a normal station. If not, it starts ESP-IDF's provisioning manager in SoftAP mode (security 1 with a proof-of-possession code) and builds the QR payload the Espressif app expects. The QR code is drawn with the `espressif/qrcode` component and scaled to the full 64-pixel height of the screen.
3. **Wait for connection** (`wifi_wait_connected()`): blocks until the board gets an IP address, or until 5 reconnect attempts have failed. In that case the screen shows `Wi-Fi failed`.
4. **Clock** (`initialize_sntp()`): starts SNTP against `pool.ntp.org` and waits up to about 20 seconds for the first sync.
5. **Main loop:** once a second, reads the connected AP's SSID and RSSI with `esp_wifi_sta_get_ap_info()`, redraws the framebuffer, sends it to the OLED and sets the LED.

## Project structure

```
wifi-health-detector/
├── CMakeLists.txt                    # ESP-IDF project definition
├── dependencies.lock                 # pinned component versions
├── sdkconfig                         # ESP-IDF configuration (target: esp32)
└── main/
    ├── CMakeLists.txt                # sources and required ESP-IDF components
    ├── idf_component.yml             # depends on espressif/qrcode
    ├── wifi_health_detector_main.c   # OLED setup, drawing, font, LEDs, main loop
    ├── wifi.c                        # Wi-Fi connection, provisioning, SNTP time
    └── wifi.h
```

## Customizing

| What | Where |
|---|---|
| I2C pins, OLED address, LED pins | `#define`s at the top of `main/wifi_health_detector_main.c` |
| Screen upside down | `esp_lcd_panel_mirror(panel, true, true)` in `oled_init()`. This build flips the image 180° because the author's module is mounted that way. Change it to `false, false` if your text comes out upside down. |
| Time zone | The `TZ` string in `print_local_time()` in `main/wifi.c`. It defaults to US Eastern (`EST5EDT,M3.2.0,M11.1.0`) and uses the POSIX format, e.g. `PST8PDT,M3.2.0,M11.1.0`, `CET-1CEST,M3.5.0,M10.5.0/3`, `IST-5:30` or `UTC0`. |
| Signal thresholds and LED colours | `wifi_rssi_to_level()` in `main/wifi_health_detector_main.c` |
| Provisioning code (PoP) | `PROV_POP` in `main/wifi.c`. Change it from the default `abcd1234` to your own value. |
| Reconnect attempts | `MAXIMUM_RETRY` in `main/wifi.c` |

## Troubleshooting

- **OLED stays blank:** check the wiring and power. At boot, the serial monitor prints every I2C device it finds (`Found device at 0x3C`). If your module shows up at `0x3D`, change `OLED_ADDR`. If nothing shows up, check SDA and SCL.
- **Flashing gets stuck at `Connecting...`:** hold the board's **BOOT** button until the upload starts.
- **Screen shows `Wi-Fi failed`:** the board couldn't join the saved network. The password may be wrong, the network may be out of range, or it may be 5 GHz-only. Erase the flash and provision again.
- **Clock shows a 1969 or 1970 date:** the board hasn't reached an NTP server yet. Make sure the network has internet access.
- **The app can't find the device:** make sure the OLED is showing the QR code (only unprovisioned boards show it) and that your phone can see the `PROV_XXXXXX` network.

## Known limitations

- If the connection drops, the board retries 5 times and then stops trying until it's reset.
- The time zone is set at compile time (see [Customizing](#customizing)).
- The provisioning PoP is the same fixed value on every board running this firmware.
