# Firmware

Firmware for the Heltec Wireless Tracker (V1.1 and V1.2, 863-928 MHz). Two boards send chat messages to each other over LoRa, with no Wi-Fi, Bluetooth or internet. A small Mac app in [`chat/`](chat/) shows one chat per board, with sent, delivered and read ticks. Each board also shares its GPS position, and its screen shows how far away the other board is and an arrow pointing to it.

This is the first step for [#3](https://github.com/1051158/STKickstarter/issues/3) (offline messaging), [#5](https://github.com/1051158/STKickstarter/issues/5) (friends' locations) and [#6](https://github.com/1051158/STKickstarter/issues/6) (pointing to a friend). It handles two boards and doesn't route messages through other boards yet.

## What you need

- Two Heltec Wireless Trackers with the LoRa antenna screwed on. Never power a board without its antenna: transmitting into nothing can damage the radio.
- USB-C cables to a Mac.
- [PlatformIO](https://platformio.org) to build and flash: `pipx install platformio`. The first build downloads the ESP32 toolchain into `~/.platformio`, roughly 1 GB.
- Python 3 for the chat app (macOS has it once the Xcode command line tools are installed).

## Flash the boards

Plug in both boards and list their ports:

```sh
ls /dev/cu.usbmodem*
```

From this folder, flash each port:

```sh
pio run -t upload --upload-port /dev/cu.usbmodem83201
pio run -t upload --upload-port /dev/cu.usbmodem83301
```

If an upload can't connect, put that board in download mode by hand. Hold the USER button, press and release RST, then release USER. Upload again, then press RST once to start the new firmware.

## Chat between the boards

With both boards plugged in:

```sh
chat/run.sh
```

The first run creates a Python environment in `chat/.venv`. The app finds the boards by themselves and opens a chat page in your browser. Type in one pane and the message shows up in the other.

- One tick: the board sent the message over the radio.
- Two grey ticks: the other board received it and confirmed.
- Two white ticks and "Read": someone clicked into the other board's chat.
- "Not delivered": no confirmation after 4 tries, about 7 seconds. Usually the other board is off or out of range.

Opening the app restarts both boards, which takes about 2 seconds.

Under each message you'll see the signal. RSSI is how loud the signal arrived, in dBm. 0 is very loud, and LoRa still decodes around -120 to -130 dBm. Two boards on a desk show about -30. SNR is how far the signal stands above the background noise, in dB. Above 0 is clean, and the chip reports at most about +13.

## Find your friend

The board starts on the locate screen. Press the USER button briefly to switch between the locate screen and the chat screen. An incoming message switches to the chat screen by itself.

The locate screen shows the other board's ID, the distance, the compass direction (for example `40 NO`) and how many seconds ago its position came in. The bottom line shows the GPS state: `GPS sat 9` means a fix with 9 satellites, `GPS zoekt 3` means it's still searching and sees 3. GPS needs a view of the sky. Indoors it often finds satellites but no fix, and the first fix outside can take a minute or two.

The board has no compass, so it can't tell which way you're holding it. The arrow therefore works in two ways:

- Standing still, a cyan arrow with an `N` shows the direction on a map. Hold the board with the top of the screen pointing north and the arrow points to your friend.
- Walking faster than 2 km/h, a green arrow marked `loop` uses the direction you're walking in. The top of the screen is then straight ahead.

The Mac app shows the same under each board's header: the friend's distance and direction with a small compass, the GPS satellites and accuracy, and a Find button that makes the other board flash.

Closer than 15 metres the screen shows `Dichtbij` instead of an arrow, because GPS is only accurate to a few metres. Hold the USER button for a second to make the other board flash yellow for 10 seconds, so you can spot your friend.

The chat screen shows the board's ID, message counts, the last signal and the last message it received.

Without GPS you can still test on a desk: type `SETPOS <lat> <lon>` in a serial monitor (or use the test position in the Mac app) to give a board a pretend position, and `SETPOS off` to go back to GPS. With one board set to `52.3731 4.8925` and the other to `52.3776 4.8925`, the boards show 500 m at 0° (north) and 180° (south).

A tilt-compensated compass (#26) makes the arrow work standing still, without holding the board north.

## Radio settings

| Setting | Value |
|---|---|
| Frequency | 869.525 MHz (EU868, sub-band 869.4-869.65 MHz, 10% duty cycle) |
| Bandwidth, spreading factor, coding rate | 125 kHz, SF7, 4/5 |
| Sync word | 0x12 (private, so LoRaWAN gateways ignore it) |
| Transmit power | 2 dBm |

The transmit power is low on purpose because the boards sit close together. Raise `TX_DBM` in `src/main.cpp` for range tests. The chip goes up to 22 dBm, and this sub-band allows up to 27 dBm ERP in the Netherlands. A 180-character message is on air for about 0.3 seconds, so normal chatting stays far below the 10% duty cycle.

## How it works

Each board takes a 4-character ID from its chip's MAC address. The Mac app and the board talk over USB serial at 115200 baud, one command per line:

| From | Line | Meaning |
|---|---|---|
| Mac | `INFO` | Ask for the board ID |
| Mac | `SEND <n> <text>` | Send message number n |
| Mac | `READ <from> <n>` | Message n from board `<from>` was read |
| Board | `ID <id>` | Board ID, also printed at start |
| Board | `SENT <n>` | Message n went out over the radio |
| Board | `DELIVERED <n> <rssi> <snr>` | The other board confirmed message n |
| Board | `FAILED <n>` | No confirmation after 4 tries |
| Board | `MSG <from> <n> <rssi> <snr> <text>` | Incoming message |
| Board | `READ <from> <n>` | The other board read message n |
| Board | `ERR <text>` | Something went wrong |
| Mac | `FINDME` | Make the other board flash |
| Mac | `SETPOS <lat> <lon>` or `SETPOS off` | Set or clear a test position |
| Board | `GPS <fix> <sats> <lat> <lon> <kmh> <course> <hdop>` | Own position every 2 seconds. Fix is 0 (none), 1 (GPS) or 2 (test position) |
| Board | `PEER <id> <lat> <lon> <age_s> <dist_m> <bearing> <rssi> <snr>` | The other board's position every 2 seconds. Distance and bearing are -1 without an own position |
| Board | `FINDME <from>` | The other board asked to be found, this board flashes |
| Board | `FINDSENT` | This board sent a find-me request |

Over the radio a board sends `M <from> <n> <text>` for a message. The receiver answers with `A <receiver> <from> <n>` to confirm and later `R <reader> <from> <n>` for read. A sender keeps one message in flight at a time and retries it until it gets the confirmation, so messages arrive in order. The receiver drops retries it already showed.

A board with a position sends `P <from> <lat> <lon>` every 15 to 18 seconds, about 40 ms on air, and `F <from>` for find-me. These aren't confirmed: a lost position is replaced by the next one.

The GPS chip (UC6580) sends its data over a serial line on GPIO 33 and 34. The firmware reads it with [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus), which also works out distance and bearing.

The pins come from the Meshtastic [`heltec_wireless_tracker` variant](https://github.com/meshtastic/firmware/blob/master/variants/esp32s3/heltec_wireless_tracker/variant.h). V1.0 boards differ: their backlight is on GPIO 45 and they switch the screen power on with LOW instead of HIGH. This firmware doesn't support V1.0.
