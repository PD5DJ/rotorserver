# N1MM Rotor Server

Antenna rotor control server for **N1MM Logger+**, with built-in storm protection, wind monitoring, and a fully web-based interface. Runs on any Debian-based Linux system — a Raspberry Pi is a perfect fit for a permanent shack installation.

![N1MM Rotor Server — rotor compass cards](https://rotorserver.pd5dj.nl/images/main-dark.png)

---

## What it does

N1MM Rotor Server sits between your rotor controllers and your network. N1MM Logger sends it heading commands; the server moves the rotor and continuously broadcasts the current heading back so N1MM Logger's display stays in sync.

On top of that it monitors wind speed and automatically parks your antennas when a storm is coming — and releases them again when the wind drops back down.

Everything is configured and monitored through a **web interface** that runs on the server itself. No SSH, no config files, no command line after installation.

---

## Features

- Controls up to **16 rotors** simultaneously over USB serial
- **N1MM Logger+ integration** — drop-in replacement for the N1MM Rotor Client, same UDP protocol
- **Fully web-based** — add rotors, adjust settings, monitor live headings and weather, all from any browser
- **Storm protection** — automatically parks antennas when wind exceeds your Beaufort threshold, releases when it calms
- **Wind monitoring** from multiple sources: Open-Meteo, yr.no, OpenWeatherMap, WeatherAPI, Ecowitt weather station, serial sensor, or UDP
- **48-hour wind and meteo history** with interactive graphs
- **Live log viewer** with filters (ROTOR · WIND · STORM · API · CONN · ERR)
- **Dark and light theme**, responsive layout — works on desktop, tablet, and phone
- **Two-level password protection** — separate passwords for control and admin access
- Supports **secondary broadcast port** for running N1MM Logger and a third-party rotor client simultaneously

Currently two rotor protocols are implemented: **Yaesu GS-232A/B** and **Prosistel CBOX 2003**.

---

## Screenshots

| Rotors | Meteo |
|--------|-------|
| ![Rotors tab](https://rotorserver.pd5dj.nl/images/main.png) | ![Meteo tab](https://rotorserver.pd5dj.nl/images/meteo.png) |

| Wind history | Log viewer |
|---|---|
| ![Wind history](https://rotorserver.pd5dj.nl/images/meteo-history.png) | ![Log viewer](https://rotorserver.pd5dj.nl/images/log.png) |

---

## Requirements

- Any Debian-based Linux system — **Raspberry Pi** 3B / 4 / 5 recommended, or any PC running Debian, Ubuntu, or similar
- One USB serial adapter per rotor controller
- Network connection (wired recommended)
- A browser on any device on the same network

---

## Installation

Copy the package to your Linux system, then run the install script. That's it.

```bash
tar xzf n1mm_rotor_server.tar.gz
cd n1mm_rotor_server
sudo bash install.sh
```

The installer compiles the software, sets up two systemd services, and starts them automatically. Open `http://<your-linux-ip>/` in your browser — the web interface is ready.

After installation you never need to touch the Linux system again. All configuration is done from the browser.

**Updating to a new version:**

```bash
sudo bash update.sh
```

Your configuration is preserved.

---

## How it works

```
N1MM Logger                    N1MM Rotor Server              Rotor controller
───────────────     UDP        ─────────────────     USB      ─────────────────
  Send GOTO    ──────────────▶   Move rotor     ──────────▶   G2800 / G-450
  Receive pos  ◀──────────────   Broadcast pos  ◀──────────   Prosistel / ...
```

- N1MM Logger sends an XML heading command to **port 12040**
- The server moves the rotor via USB serial
- The server broadcasts the current heading on **port 13010** (and optionally a second port for additional applications)
- N1MM Logger picks up the broadcast and updates its display

---

## Web interface

Open `http://<server-ip>/` in any browser.

### Rotors tab
Live compass card per rotor. Click to send a GOTO, double-click for instant move, Stop button to halt. Multi Control card commands all rotors at once.

### Meteo tab
Live wind and weather data. Wind rose, Beaufort force, gust, temperature, humidity, pressure, UV, solar radiation. Click any card for a 48-hour history graph.

### Settings
Click the wrench icon (top-right). Five tabs:

| Tab | What you configure |
|-----|-------------------|
| **Rotors** | Add, edit, remove rotor controllers |
| **Server** | Network ports, broadcast address, poll intervals |
| **Meteo** | Wind data source, Ecowitt settings, location |
| **Storm** | Beaufort threshold, sustain timers, correction interval |
| **Web** | Port, session timeout, control and admin passwords |

---

## Storm protection

When storm protection is enabled the server continuously watches the wind. If the wind exceeds the configured Beaufort threshold for a set number of minutes (sustain timer), all storm-enabled rotors are automatically parked at a safe heading relative to the wind direction.

Per rotor you can configure:
- **Storm offset** — heading relative to the wind (0° = into the wind)
- **Always controllable** — keep the rotor manually reachable via N1MM Logger during a storm
- **Return timeout** — auto-return to the safe heading after N minutes of inactivity

Storm protection can be toggled or forced on immediately from the Meteo tab without going into settings.

---

## Wind sources

| Source | Type | Account needed |
|--------|------|---------------|
| Open-Meteo | Internet forecast | No |
| yr.no | Internet forecast | No |
| OpenWeatherMap | Internet forecast | Free API key |
| WeatherAPI | Internet forecast | Free API key |
| Ecowitt station | Local weather station (HTTP push) | No |
| Serial sensor | USB anemometer | No |

Ecowitt supports automatic fallback to an internet source when no push is received, data forwarding to a second server, and a configurable wind speed correction factor.

---

## N1MM Logger configuration

In N1MM Logger+: **Config → Configure Ports, Mode Control, Winkey…** → **Rotor tab**

For each antenna:
- **Rotor name** — must match exactly the name set in N1MM Rotor Server (case-sensitive)
- **Rotor type** — N1MM Rotor
- **IP address** — the IP address of the Linux system running N1MM Rotor Server
- **Ports** — leave at defaults (12040 / 13010) unless changed in server settings

---

## More information

Full documentation and feature overview: **[rotorserver.pd5dj.nl](https://rotorserver.pd5dj.nl/)**

---

## License

See `LICENSE` file.
