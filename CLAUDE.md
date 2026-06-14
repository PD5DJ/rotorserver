# N1MM Rotor Server — Linux

Single-file C project (`n1mm_rotor_server.c`) for controlling antenna rotors from N1MM Logger.
Built by PD5DJ. Version 2.10.3.

## Building

```bash
sudo make
```

Compiler: gcc, flags: `-std=c99 -Wall -Wextra -Wpedantic -O2`, links: `-lpthread -lm`

Targets: `server` (n1mm_rotor_server), `web` (n1mm_rotor_web), `all` (both).
`sudo make install-www` copies www/ files to `/etc/n1mm_rotor_server/www/` without full reinstall.
`sudo bash webupdate.sh` — same as install-www but also restarts n1mm_rotor_web service.

**Note:** `n1mm_rotor_gui.c` and `n1mm_rotor_gui.desktop` are no longer used. The GTK4 GUI has been replaced by the web interface.

## CLI (n1mm_rotor_server)

```bash
sudo ./n1mm_rotor_server --setup              # configure server settings
sudo ./n1mm_rotor_server --add               # add rotor (wizard)
sudo ./n1mm_rotor_server --change            # edit existing rotor (wizard)
sudo ./n1mm_rotor_server --delete [name]     # delete rotor (name optional → interactive menu)
sudo ./n1mm_rotor_server --list              # overview of rotors + server settings
     ./n1mm_rotor_server --run               # start server
```

## Config file (`n1mm_rotor_server.conf`)

```ini
# Server settings
cmd_port=12040
bcast_port=13010
bcast_port2=0                     # secondary broadcast port (0=off, 13011–13015)
bcast_addr=255.255.255.255        # comma-separated for multiple addresses
idle_ms=1000                      # broadcast interval when rotor is idle
moving_ms=200                     # broadcast interval when rotor is moving
logfile=/var/log/n1mm_rotor_server.log

# Rotors
num_rotors=1
rotor0_name=2m yagi
rotor0_by_path=platform-xhci-hcd.0-usb-0:2:1.0-port0
rotor0_protocol=YAESU
rotor0_baud=9600
rotor0_offset=0                   # heading offset in degrees (-180..180)
rotor0_storm_enabled=1            # 0=off, 1=on — include in storm correction
rotor0_storm_offset=0             # degrees relative to wind (-180..180)
rotor0_always_controllable=0      # 1=always accept N1MM GOTO even during storm correction
rotor0_return_timeout_min=0       # 0=off; 15/30/60/120 = minutes idle before return to storm
rotor0_simulate=0                 # 1=simulate movement, no serial port needed

# Wind monitor
wind_enabled=1
wind_source=openmeteo             # openmeteo | yrno | owm | wapi | serial | udp | ecowitt
wind_lat=52.4656
wind_lon=4.5314
wind_interval_min=15              # fetch interval (5/10/15/30/60 min)
wind_owm_apikey=                  # OpenWeatherMap API key
wind_wapi_apikey=                 # WeatherAPI key
wind_serial_device=               # by-path (source=serial only)
wind_serial_baud=19200
wind_udp_port=0                   # listen port (source=udp only)
wind_ecowitt_port=49199           # Ecowitt: TCP listen port for HTTP push
wind_ecowitt_passkey=             # Ecowitt: validate incoming PASSKEY (empty=accept all)
wind_ecowitt_fallback=            # Ecowitt: fallback source when no data (openmeteo|yrno|owm|wapi)
wind_ecowitt_fallback_min=5       # Ecowitt: minutes without push before fallback activates
wind_ecowitt_fwd_host=            # Ecowitt: forward destination host/IP (empty=off)
wind_ecowitt_fwd_port=0           # Ecowitt: forward destination port (0=off)
wind_ecowitt_fwd_path=/           # Ecowitt: forward destination path (default /)
wind_ecowitt_fwd_passkey=         # Ecowitt: PASSKEY to put in forwarded body (empty=keep original)
wind_ecowitt_correction=100       # Ecowitt: wind speed correction % (100=off, 167=×1.67, applied to m/s before Bft conversion)

# Storm mode
storm_enabled=0                   # persisted on/off state — written by server on STORM ON/OFF
storm_threshold_bft=7             # wind force threshold in Beaufort (0–12)
storm_sustain_min=5               # consecutive minutes ≥ threshold before correction starts
storm_release_min=10              # consecutive minutes < threshold before correction stops
storm_interval_min=15             # re-send GOTO interval during active correction (5/10/15/30/60 min)
storm_block_manual=1              # 1=block N1MM GOTO during correction (unless always_controllable)

# Web interface
web_port=80
web_password=                     # admin password: storm control, config changes
web_view_password=                # control password: required to send GOTO/STOP (empty=free)
```

## Architecture

### Threads (n1mm_rotor_server)
- **main thread**: setup/config, opens serial ports
- **udp_rx_thread**: listens on `cmd_port` (UDP), receives N1MM XML commands and storm/wind control
- **poll_bcast_thread**: polls each rotor, broadcasts position on `bcast_port` (and `bcast_port2` if set)
- **wind_thread**: fetches/receives wind data, broadcasts on WIND_BCAST_PORT (55555) every 1 s
- **storm_thread**: monitors wind, drives storm-enabled rotors to safe position when feature is on

### Web server (n1mm_rotor_web)
Separate binary that serves the web UI and bridges the Unix socket to SSE clients.

- **unix_rx_thread**: connects to `/tmp/n1mm_rotor_server.sock`, receives rotor/wind lines; writes `wind_hist.dat` every 5 minutes
- **http_thread**: accepts HTTP connections, serves static files and API endpoints
- **sim_thread**: advances simulated rotors, detects stale rotor data (8s timeout → has_data=0)

Web server API endpoints:
| Method | Path | Auth | Description |
|---|---|---|---|
| GET | `/api/status` | public | version, auth_required, control_required |
| GET | `/api/config` | public | full config as JSON (passwords not included) |
| POST | `/api/config` | admin | save config, optionally restart server |
| POST | `/api/set-password` | admin | save a single password field |
| GET | `/api/devices` | public | list USB serial devices |
| GET | `/api/events` | public | SSE stream (rotors, wind, hist) |
| GET | `/api/hist` | public | wind history JSON (in-memory, ~6h) |
| GET | `/api/hist-meteo` | public | 48h meteo+wind history from wind_hist.dat |
| GET | `/api/log` | public | server log lines as JSON array; `?since=<unix>` (all lines ≥ timestamp, up to 50 000) or `?lines=N` (last N lines, ring-buffer) |
| POST | `/api/log/clear` | admin | truncate server log file |
| POST | `/api/login` | — | authenticate, returns token + operator flag |
| POST | `/api/goto` | control | send GOTO (requires control token when control password is set) |
| POST | `/api/stop` | control | send STOP (requires control token when control password is set) |
| POST | `/api/storm` | admin | STORM ON/OFF or STORM FORCE (body: `{"on":1}` / `{"on":0}` / `{"force":1}`) |
| POST | `/api/restart` | admin | restart n1mm_rotor_server service |
| POST | `/api/reboot` | admin | reboot the system |
| POST | `/api/shutdown` | admin | shut down the system |

### Authentication model
- **Not logged in**: can view the full dashboard (rotors, meteo, graphs) but cannot send GOTO/STOP or access settings.
- **Control password** (`web_view_password`): required to send GOTO/STOP commands. Only `/api/goto` and `/api/stop` are blocked when this password is set; all other endpoints are public. Attempting rotor control without login shows a red toast "Please login first".
- **Admin password** (`web_password`): required for storm control, config save, and password management. Also required for rotor control when no separate control password is set.
- Login returns a token valid for 8 hours. Token sent as `Authorization: Bearer <token>` header or `?token=<token>` query parameter.
- Each password has its own Save button in the Web settings panel; the global Save & restart button does not touch passwords.

### Broadcast timing
Each rotor has its own timer. Interval switches dynamically:
- `idle_ms` when rotor is stationary
- `moving_ms` when rotor is driving to target
- Back to idle once azimuth is within 2° of target
- Back to idle on **stall**: no azimuth change ≥ `STALL_MIN_DEG` = 5° within `STALL_TIMEOUT_MS` = 5000 ms

### USB reconnect
When USB is unplugged during operation:
- After `MAX_CONSEC_ERRORS` (3) consecutive query errors: close port, `fd = -1`, `moving = 0`
- Every `RECONNECT_INTERVAL_MS` (5000 ms) attempts to reopen via `by_path`
- On recovery: reopen port, for PROSISTEL re-disable CPM

Web server staleness detection:
- If no broadcast received for a rotor within `ROTOR_STALE_S` (8s): set `has_data=0`, push SSE
- When Unix socket disconnects: immediately reset all non-simulated rotors to `has_data=0`

### SSE send timeout
`sse_add()` sets `SO_SNDTIMEO = 2 s` on each SSE client socket. Without this, a dead or slow client
whose TCP send buffer fills up causes `send()` inside `sse_broadcast()` to block for minutes while
holding `g_sse_lock` — stalling all SSE broadcasts to all clients. The 2-second timeout caps the
worst-case stall per dead client.

### UDP protocol
- **Receive** (N1MM → server): XML on port 12040
  ```xml
  <N1MMRotor><rotor>2m yagi</rotor><goazi>180</goazi><offset>0</offset></N1MMRotor>
  ```
- **Receive** (control commands): `STORM ON` / `STORM OFF` / `STORM FORCE` / `WIND?`
- **Send** (server → N1MM): `NAME @ heading` on port 13010
  Heading is azimuth × 10 (integer), so 180.0° = 1800.

### HTTP request handling
The web server reads the complete HTTP request body using a recv loop based on `Content-Length`. Buffer size is 64 KB (`HTTP_BUF_SIZE`). The config POST includes a safety check: if the parsed rotor count doesn't match `num_rotors` in the JSON (truncated body detection), the save is rejected with HTTP 400.

## Web UI (`www/`)

### Files
- `index.html` — single-page app shell
- `style.css` — dark/light theme, responsive layout
- `app.js` — all UI logic, SSE handling, compass drawing

### Key JS concepts
- All icons are inline SVG constants (`ICO_POWER`, `ICO_TRASH`, `ICO_CHART`, `ICO_TERMINAL`, etc.) — no font/emoji dependencies
- Compass drawn on `<canvas>` via `drawCompass(canvas, currentAz, targetAz, hasData, isMulti, offset)`
- Single-click compass: 250ms timer + `requireControlAuth`, cancelled by double-click → immediate GOTO
- Multi Control card: appears automatically when >1 rotor, always first card, ID `rcard-multi`
- Storm header badge: shows "STORM" text with SVG icon; same height as tab buttons
- Connection status: colours the ⏻ menu button green (connected) or red blinking (disconnected)
- Theme toggle: sun icon in dark mode (click → light), moon icon in light mode (click → dark)
- `requireControlAuth(cb)` — gates GOTO/STOP: shows red toast if not logged in, else calls cb
- `requireAuth(cb)` — gates admin actions: shows toast if not admin, else calls cb
- `attachZoomScroll(canvas, scrollEl, fullHist, drawFn)` — zoom + scrollbar helper for history charts

### Log terminal panel
- `btn-log` (terminal icon `>_`) in title bar — toggles floating log panel
- Red dot on button when hard errors are present (rotor offline, GOTO/STOP rejected)
- `logEntries[]` — in-memory array, max `LOG_HARD_MAX = 20000` entries: `{ts, type, msg}`
- Log sourced from actual server log file via SSE streaming and `/api/log?since=<unix>` REST endpoint
- Log types and colors: `ROTOR` (blue), `WIND` (green), `STORM` (orange), `API` (grey), `CONN` (dark grey), `ERR` (red)
- Filter chips per type + ALL toggle; filter state persists across open/close
- Auto-scroll: if at bottom → new entries scroll in automatically; if scrolled up → "▼ N new" badge shown
- Draggable by header bar; opens centered at 90% × 82% viewport; `resize: both`
- **History window**: `logHistoryHours` — 1 h / 6 h / 12 h / 1 d / 2 d / 3 d / 7 d (stored in localStorage `log_history_hours`, default 24)
- `fetchLogHistory(force)` — uses `GET /api/log?since=<unix>`; skips re-fetch on panel reopen if data already covers the window; `force=true` on period change or log clear
- `logFetchedSince` — ms timestamp marking the oldest data already loaded; prevents redundant re-fetches
- `logCutoff()` — returns `Date.now() - logHistoryHours * 3600000`; used by trim and SSE filter
- SSE reconnects logged as `CONN` (not `ERR`)

### Rotor edit dialog
- Layout: `re-form` div wrappers — full-width stacked label + input fields
- Device spinner shows all `/dev/serial/by-path/` ports; already-registered ports show `[naam]` suffix
- `normDev(s)` — normalises `usbv2` ↔ `usb` path variants for cross-platform matching
- **Simulate mode**: Device / Protocol / Baud fields hidden when Simulate checkbox is checked; `by_path` cleared on save

### Meteo tab
- **Wind Monitor** card: source, direction, force, gust, storm threshold, timer; 📈 button opens wind 48h popup
  - **Monitor ON/OFF** button (2/3 width): toggles the storm-monitoring feature
  - **Activate** button (1/3 width, orange): forces immediate storm correction, bypassing the sustain timer; only enabled when Monitor is ON
- **Wind Direction** card: compass rose showing wind direction
- **Value cards** (temp, hum, baro, precip, uvsolar): each has an 📈 button → 48h history popup
- History popups: zoom levels 30m/1h/3h/6h/12h/24h/48h; horizontal scrollbar appears when zoomed in

### Mobile portrait
- Title text and version info hidden (`max-width: 600px` + `orientation: portrait`)
- Storm badge uses `padding: 6px` to match tab button height

## Structs

```c
RotorCfg {
    char name[64];
    char by_path[256];
    char by_path_alt[256];
    char serial[64];
    char protocol[16];           // "YAESU" or "PROSISTEL"
    int  baud;
    int  offset;                 // heading offset (-180..180)
    int  storm_enabled;
    int  storm_offset;
    int  always_controllable;
    int  simulate;               // 1 = no serial port, movement simulated
    int  return_timeout_min;     // 0=off; minutes idle before auto-return to storm position
}

Rotor {
    RotorCfg     cfg;
    int          fd;
    double       current_az;
    double       target_az;
    int          moving;
    Protocol     protocol;
    YaesuVariant variant;
    long long    last_bcast_ms;
    double       stall_ref_az;
    long long    stall_ref_ms;
    int          consec_errors;
    long long    next_reconnect_ms;
    int          storm_active;
    long long    last_manual_goto_ms;  // time of last manual GOTO (used by return_timeout_min)
    double       sim_dist;       // total arc for simulate ramp profile
    pthread_mutex_t lock;
}

Config {
    int  cmd_port, bcast_port, bcast_port2;
    char bcast_addr[256];
    int  idle_ms, moving_ms;
    char logfile[256];
    int  num_rotors;
    RotorCfg rotors[MAX_ROTORS]; // MAX_ROTORS = 16
    // Wind monitor
    int    wind_enabled;
    char   wind_source[16];
    double wind_lat, wind_lon;
    int    wind_interval_min;
    char   wind_serial_device[256];
    int    wind_serial_baud;
    int    wind_udp_port;
    char   wind_owm_apikey[64];
    char   wind_wapi_apikey[64];
    int    wind_ecowitt_port;
    char   wind_ecowitt_passkey[64];
    char   wind_ecowitt_fallback[16];
    int    wind_ecowitt_fallback_min;
    char   wind_ecowitt_fwd_host[128];
    int    wind_ecowitt_fwd_port;
    char   wind_ecowitt_fwd_path[128];
    char   wind_ecowitt_fwd_passkey[64];
    int    wind_ecowitt_correction;    // wind speed correction % (100=off, 167=×1.67)
    // Storm mode
    int    storm_enabled;
    int    storm_threshold_bft;
    int    storm_sustain_min;
    int    storm_release_min;
    int    storm_interval_min;
    int    storm_block_manual;
    // Web
    int    web_port;
    char   web_password[64];
    char   web_view_password[64];
}
```

---

## Wind Monitor

### Wind broadcast protocol (server → web)
Via Unix socket: `WIND @ <dir>/<bft>/<gust_ms>/<storm_active>/<last_fetch_unix>/<correction_active>/<timer_secs>/<fallback_active>`

Wind thread starts with `last_bft=0` (not -1) so a broadcast is sent immediately on startup, making the wind card appear in the web UI even before the first data arrives.

### Wind source abstraction
```c
typedef enum { WIND_SRC_OPENMETEO, WIND_SRC_YRNO, WIND_SRC_OWM, WIND_SRC_WAPI,
               WIND_SRC_SERIAL, WIND_SRC_UDP, WIND_SRC_ECOWITT } WindSource;
```

### Wind direction smoothing buffer
To prevent brief direction spikes (common with live sensors like Ecowitt) from triggering incorrect storm correction moves:
- Ring buffer `g_wind_dir_buf[WIND_DIR_SMOOTH_N]` (N=10) stores the last 10 direction measurements
- Protected by `g_wind_lock`
- `wind_dir_push(dir)` called at every `g_wind.direction_deg =` assignment
- `wind_dir_smooth()` returns the **circular mean** (cos/sin method) — correctly handles wrap-around (e.g. 350°/10° averages to 0°, not 180°)
- `storm_apply_goto()` uses `wind_dir_smooth()` instead of the raw instantaneous value
- Log line includes both `wind_smooth=` and `instant=` for diagnostics

### Ecowitt wind force correction
`wind_ecowitt_correction` (default 100) is applied to the raw m/s wind speed **before** Beaufort conversion:
- 100 = no change
- 167 = sensor reads 3 Bft but actual is ~5 Bft
- Applied to both wind speed and gust speed

---

## Storm Mode

### GOTO blocking logic
```c
if (r->storm_active && g_cfg.storm_block_manual && !r->cfg.always_controllable) {
    // ignore GOTO
}
```

### Storm Force
`STORM FORCE` UDP/Unix-socket command (also `/api/storm` with `{"force":1}`):
- Bypasses the sustain timer — immediately starts correction
- Sets `g_storm_active = 1`, `g_storm_correcting = 1`, all storm-enabled `rotor.storm_active = 1`
- Calls `storm_apply_goto()` directly
- Persists storm ON state to config

### Return-to-storm timer (`return_timeout_min`)
For `always_controllable` rotors with `return_timeout_min > 0`:
- Every manual GOTO sets `r->last_manual_goto_ms = now_ms()`
- `storm_thread` checks every 5 s: if `now - last_manual_goto_ms >= return_timeout_min * 60000` and storm correction is active → drives rotor back to storm position using `wind_dir_smooth()`
- Timer resets on each new GOTO; set to 0 after firing
- Configurable per rotor: Off / 1 / 5 / 10 / 15 / 30 / 60 / 120 minutes
- Countdown shown on rotor card status line as `↩ return in MM:SS` (blue text) when active
- Clicking the compass rose counts as a manual GOTO and resets the timer

### Simulate + storm GOTO fix
`storm_apply_goto()` sets `r->sim_dist = fabs(target - r->current_az)` for simulated rotors, same as manual GOTO, so the ramp profile works correctly.

### Direction logic (stop-aware, smoothed)
```
smooth_dir = circular_mean(last 10 direction readings)
primary  = (smooth_dir + storm_offset + 360) % 360
fallback = (smooth_dir + 180 + storm_offset + 360) % 360
if fabs(current_az - primary) > 180: target = fallback
else: target = primary
if target == 0: target = 360    # North = 360; 0 reserved for "no data"
```

---

## Persistent wind/meteo history (wind_hist.dat)

- File: `/etc/n1mm_rotor_server/wind_hist.dat`
- Written by `n1mm_rotor_web` (unix_rx_thread) every `WIND_HIST_DAT_INTVL_S` = 300 s (5 min)
- Format per line: `ts,dir,bft,gust,temp,feels,hum,baro,precip,uv,solar` (empty field = no data)
- Compacted to last 48 hours every 12 writes (~1 hour) via `hist_dat_compact()`
- Served by `GET /api/hist-meteo` as JSON array; only entries within 48h cutoff returned
- Web UI uses this for 48h history popups on Wind Monitor and all meteo value cards

---

## Installation

```bash
sudo bash install.sh     # full install
sudo bash update.sh      # rebuild + reinstall binaries + www, restart services
sudo bash webupdate.sh   # copy www/ files only + restart n1mm_rotor_web (no rebuild)
sudo make install-www    # copy www/ files only (no service restart)
sudo bash uninstall.sh   # remove everything (asks about config + log)
```

### Sudoers
Install.sh creates `/etc/sudoers.d/n1mm_rotor_server` with `!requiretty` and NOPASSWD rules for:
- `systemctl start/stop/restart n1mm_rotor_server`
- `systemctl start/stop/restart n1mm_rotor_web`

---

## Known limitations / TODOs
- Maximum 8 broadcast destination addresses
- Reboot/shutdown via web UI uses `reboot(2)` syscall directly via `CAP_SYS_BOOT` ambient capability — no sudo needed
