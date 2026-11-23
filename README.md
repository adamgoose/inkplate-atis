# Inkplate ATIS

Shows the current D-ATIS, weather and runway configuration for an airport
(KMSP by default) on a Soldered Inkplate 6COLOR.

```
atis.info ──► server (Bun + Effect) ──► /frame.bin ──► Inkplate firmware
                 ▲
     data/KMSP.json (runways from AIRAC navdata)
```

The server does the work: it fetches the ATIS, works out which runways are
landing, departing and closed, draws the 600×448 frame in the panel's seven
inks and serves it as raw pixels. The firmware wakes, downloads the frame,
refreshes the panel only if it changed, and goes back to deep sleep.

## Server

```bash
cd server
bun install
bun run start
```

Open `http://localhost:8080/` for a live preview of what the panel will show.

| Route         | Purpose                                                        |
| ------------- | -------------------------------------------------------------- |
| `/frame.bin`  | Frame for the firmware: 134,400 bytes, 4 bits per pixel. Honours `If-None-Match`. |
| `/frame.png`  | The same frame as a PNG.                                       |
| `/state.json` | Parsed ATIS and runway data.                                   |

Environment variables:

| Variable          | Default                 | Meaning                                   |
| ----------------- | ----------------------- | ----------------------------------------- |
| `PORT`            | `8080`                  | Listen port.                              |
| `ICAO`            | `KMSP`                  | Airport. Needs a matching `data/{ICAO}.json`. |
| `REFRESH_SECONDS` | `300`                   | How long the firmware sleeps between checks. |
| `ATIS_CACHE`      | `60 seconds`            | How long an atis.info response is reused. |
| `ATIS_API`        | `https://atis.info/api` | Upstream base URL.                        |

### Runway data

`data/KMSP.json` was extracted from AIRAC 2611. After a new cycle, or for
another airport:

```bash
bun run extract KMSP ~/Documents/SimToolkitPro/navdata.s3db
```

### Tests

```bash
bun test
```

## Firmware

1. Copy `firmware/inkplate_atis/config.example.h` to `config.h` and set the
   WiFi credentials and the server's LAN address.
2. In the Arduino IDE, add the Inkplate board package
   (`https://github.com/SolderedElectronics/Dasduino-Board-Definitions-for-Arduino-IDE/raw/master/package_Dasduino_Boards_index.json`),
   install the **InkplateLibrary** library, and select **Soldered Inkplate 6COLOR**.
3. Open `firmware/inkplate_atis/inkplate_atis.ino` and upload.

Or with arduino-cli:

```bash
arduino-cli compile --fqbn Inkplate_Boards:esp32:Inkplate6COLOR firmware/inkplate_atis
arduino-cli upload  --fqbn Inkplate_Boards:esp32:Inkplate6COLOR -p /dev/cu.usbserial-XXXX firmware/inkplate_atis
```

Behaviour:

- Sleeps for the interval the server advertises, then checks again. An
  unchanged frame costs a few seconds of WiFi and no panel refresh.
- The wake button forces an immediate full redraw.
- If the server or WiFi is unreachable it keeps the last frame, retries every
  minute, and shows a notice after three failures in a row.
- Reports battery voltage to the server, which adds a LOW BATT flag below 3.5 V.

## Reading the display

- Solid black runways are in use, outlined ones are idle, red ones with crosses are closed.
- Green arrows show landing direction, blue arrows departure direction.
- The compass arrow points the way the wind is blowing.

Fonts are [Spleen](https://github.com/fcambus/spleen) (BSD 2-Clause).
