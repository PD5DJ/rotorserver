/*
 * n1mm_rotor_server.c — N1MM Rotor Server for Linux
 *
 * UDP port 12040 (recv): N1MM XML rotor commands
 * UDP port 13010 (send): rotor status broadcast  →  NAME@heading
 *
 * Up to MAX_ROTORS rotors, each on a YAESU GS-232A/B compatible controller.
 * Devices are identified by /dev/serial/by-path/ so plug-position is stable.
 *
 * Build:   make
 * Setup:   sudo ./n1mm_rotor_server --setup
 * Add:     sudo ./n1mm_rotor_server --add
 * Change:  sudo ./n1mm_rotor_server --change
 * Delete:  sudo ./n1mm_rotor_server --delete <name>
 * List:    sudo ./n1mm_rotor_server --list
 * Run:     ./n1mm_rotor_server --run
 *
 * Built by PD5DJ
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <limits.h>
#include <math.h>
#include <sys/un.h>

/* ─── Constants ─────────────────────────────────────────────────────────── */
#define VERSION              "2.10.3"
#define MAX_ROTORS           16
#define DEFAULT_CONFIG       "/etc/n1mm_rotor_server/n1mm_rotor_server.conf"
#define LOG_FILE             "/var/log/n1mm_rotor_server.log"
#define LOG_KEEP_HOURS       24     /* retain this many hours of log history */
#define LOG_ROTATE_INTERVAL  3600   /* check for old entries every N seconds */
#define DEFAULT_CMD_PORT     12040
#define DEFAULT_BCAST_PORT   13010
#define DEFAULT_BCAST_PORT2  0
#define DEFAULT_BCAST_ADDR   "255.255.255.255"
#define DEFAULT_IDLE_MS      1000
#define DEFAULT_MOVING_MS    200
#define SERIAL_TIMEOUT_MS    500
#define BY_PATH_DIR          "/dev/serial/by-path"
#define MAX_LINE             512
#define STALL_TIMEOUT_MS     5000  /* ms without az change → back to idle interval */
#define STALL_MIN_DEG        5.0   /* minimum az change to count as actual movement */
#define RECONNECT_INTERVAL_MS 5000 /* retry interval after USB disconnect */
#define WIND_BCAST_PORT      55555 /* wind broadcast port — internal only, never configurable */
#define MAX_CONSEC_ERRORS    3     /* consecutive query failures before closing port */
#define GUI_SOCK_PATH        "/tmp/n1mm_rotor_server.sock"  /* Unix socket for GUI IPC */
#define MAX_GUI_CLIENTS      8     /* max simultaneous GUI connections */

/* ─── Wind source ───────────────────────────────────────────────────────── */
typedef enum { WIND_SRC_OPENMETEO, WIND_SRC_YRNO, WIND_SRC_OWM, WIND_SRC_WAPI,
               WIND_SRC_SERIAL, WIND_SRC_UDP,
               WIND_SRC_ECOWITT } WindSource;

/* Shared wind data — written by wind_thread, read by storm_thread + web_thread */
typedef struct {
    /* Core wind — always populated when has_data=1 */
    double direction_deg;
    int    force_bft;
    double gust_ms;
    int    has_data;
    /* Extended meteo — NaN when not available from the current source */
    double temp_c;          /* air temperature (°C)               */
    double humidity_pct;    /* relative humidity (%)              */
    double baro_hpa;        /* sea-level / relative pressure (hPa)*/
    double precip_mm;       /* precipitation / rain rate (mm/h)   */
    double uv_index;        /* UV index (0–11+)                   */
    double solar_wm2;       /* solar radiation (W/m²)             */
    double feels_like_c;    /* apparent temperature (°C)          */
} WindData;

/* Initialise all extended fields to NaN */
static inline void wind_data_init(WindData *w) {
    w->direction_deg = 0; w->force_bft = 0; w->gust_ms = 0; w->has_data = 0;
    w->temp_c       = (double)__builtin_nan("");
    w->humidity_pct = (double)__builtin_nan("");
    w->baro_hpa     = (double)__builtin_nan("");
    w->precip_mm    = (double)__builtin_nan("");
    w->uv_index     = (double)__builtin_nan("");
    w->solar_wm2    = (double)__builtin_nan("");
    w->feels_like_c = (double)__builtin_nan("");
}

/* ─── Rotor config ──────────────────────────────────────────────────────── */
typedef struct {
    char name[64];
    char by_path[256];   /* filename within /dev/serial/by-path/  */
    char serial[64];     /* USB serial number — fallback if by-path changes */
    char protocol[16];   /* "YAESU" or "PROSISTEL"                */
    int  baud;
    int  offset;         /* heading offset in degrees (-180..180)  */
    int  storm_enabled;        /* 0/1 — storm monitor for this rotor     */
    int  storm_offset;         /* degrees relative to wind (-180..180)   */
    int  always_controllable;  /* 1 = N1MM GOTO always accepted, even during storm */
    int  simulate;             /* 1 = simulate movement, no serial port needed     */
    int  return_timeout_min;   /* 0=off, else minutes idle before return to storm  */
} RotorCfg;

typedef enum { PROTO_YAESU, PROTO_PROSISTEL } Protocol;

/* YAESU sub-variant (auto-detected) */
typedef enum { YAESU_UNKNOWN, YAESU_GS232A, YAESU_GS232B } YaesuVariant;

typedef struct {
    RotorCfg     cfg;
    int          fd;
    double       current_az;
    double       target_az;
    double       sim_dist;       /* total distance when simulate movement started */
    int          moving;
    Protocol     protocol;
    YaesuVariant variant;        /* YAESU only */
    long long    last_bcast_ms;
    double       stall_ref_az;      /* az snapshot for stall detection */
    long long    stall_ref_ms;      /* time of last confirmed az change while moving */
    int          consec_errors;     /* consecutive query failures */
    long long    next_reconnect_ms; /* earliest time to attempt port reopen */
    int          storm_active;          /* 1 = storm override — GOTO from N1MM ignored */
    long long    last_manual_goto_ms;   /* time of last manual GOTO (for return timer) */
    pthread_mutex_t lock;
} Rotor;

/* ─── Server config ─────────────────────────────────────────────────────── */
typedef struct {
    int  cmd_port;
    int  bcast_port;
    int  bcast_port2;
    char bcast_addr[256];
    int  idle_ms;
    int  moving_ms;

    int      num_rotors;
    RotorCfg rotors[MAX_ROTORS];

    /* Wind monitor */
    int    wind_enabled;              /* 0=off, 1=on                              */
    char   wind_source[16];           /* "openmeteo"|"yrno"|"owm"|"wapi"|"serial"|"udp" */
    double wind_lat;                  /* internet sources: latitude               */
    double wind_lon;                  /* internet sources: longitude              */
    int    wind_interval_min;         /* internet sources: fetch interval (5–60 min) */
    char   wind_serial_device[256];   /* Serial: by-path                          */
    int    wind_serial_baud;          /* Serial: baud rate, default 19200         */
    int    wind_udp_port;             /* UDP: listen port for external wind data  */
    char   wind_owm_apikey[64];       /* OpenWeatherMap: API key                  */
    char   wind_wapi_apikey[64];      /* WeatherAPI: API key                      */
    int    wind_ecowitt_port;            /* Ecowitt: HTTP listen port (default 49199)*/
    char   wind_ecowitt_passkey[64];     /* Ecowitt: validate incoming PASSKEY (empty=accept all) */
    char   wind_ecowitt_fallback[16];    /* Ecowitt: fallback source (openmeteo|yrno|owm|wapi|empty=off) */
    int    wind_ecowitt_fallback_min;    /* Ecowitt: minutes without data before fallback (default 5) */
    char   wind_ecowitt_fwd_host[128];   /* Ecowitt: forward host/IP (empty=off)     */
    int    wind_ecowitt_fwd_port;        /* Ecowitt: forward port (0=off)            */
    char   wind_ecowitt_fwd_path[128];   /* Ecowitt: forward path (default "/")      */
    int    wind_ecowitt_correction;      /* Ecowitt: wind speed correction % (100=off, 150=×1.5) */
    /* Storm monitor */
    int    storm_enabled;             /* 0=off, 1=on (master switch)              */
    int    storm_threshold_bft;       /* Beaufort threshold (0–12)                */
    int    storm_sustain_min;         /* minutes ≥ threshold before activation    */
    int    storm_release_min;         /* minutes < threshold before deactivation  */
    int    storm_interval_min;        /* correction interval (5/10/15/30/60 min)  */
    int    storm_block_manual;        /* 1 = block N1MM GOTO during storm (unless always_controllable) */
    /* Web interface */
    int    web_enabled;               /* 0=off, 1=on                              */
    int    web_port;                  /* HTTP listen port (default 80)            */
    char   web_password[64];          /* password for write operations (empty=none) */
} Config;

/* ─── Globals ───────────────────────────────────────────────────────────── */
static Config  g_cfg;
static Rotor   g_rotors[MAX_ROTORS];
static int     g_num_rotors = 0;
static volatile int g_running = 1;

static pthread_mutex_t g_log_lock  = PTHREAD_MUTEX_INITIALIZER;
static FILE           *g_logfp     = NULL;

/* Shared wind data (wind_thread writes, storm_thread reads) */
static WindData        g_wind;
static pthread_mutex_t g_wind_lock     = PTHREAD_MUTEX_INITIALIZER;

/* ── Wind direction smoothing buffer ──────────────────────────────────────
 * Keeps the last N direction readings so storm_apply_goto() uses a circular
 * mean instead of the instantaneous value.  This filters short spikes that
 * live sensors (Ecowitt, serial, UDP) produce between fetch intervals.
 * Protected by g_wind_lock — only access under that mutex.
 * ──────────────────────────────────────────────────────────────────────── */
#define WIND_DIR_SMOOTH_N  10   /* samples — ~10 min at 1 update/min        */
static double g_wind_dir_buf[WIND_DIR_SMOOTH_N];
static int    g_wind_dir_head  = 0;
static int    g_wind_dir_count = 0;

/* Push a new direction reading into the smoothing buffer (call under g_wind_lock) */
static void wind_dir_push(double dir)
{
    g_wind_dir_buf[g_wind_dir_head] = dir;
    g_wind_dir_head = (g_wind_dir_head + 1) % WIND_DIR_SMOOTH_N;
    if (g_wind_dir_count < WIND_DIR_SMOOTH_N) g_wind_dir_count++;
}

/* Return circular mean of buffered directions (call under g_wind_lock).
 * Handles wrap-around correctly: mean(350°, 10°) = 0°, not 180°. */
static double wind_dir_smooth(void)
{
    if (g_wind_dir_count == 0) return g_wind.direction_deg;
    double sx = 0.0, sy = 0.0;
    for (int i = 0; i < g_wind_dir_count; i++) {
        double rad = g_wind_dir_buf[i] * M_PI / 180.0;
        sx += cos(rad); sy += sin(rad);
    }
    double avg = atan2(sy, sx) * 180.0 / M_PI;
    if (avg < 0.0) avg += 360.0;
    return avg;
}

/* Storm state */
static volatile int    g_storm_active      = 0; /* 1 = storm mode ON (master switch, set by GUI) */
static volatile int    g_storm_correcting  = 0; /* 1 = sustain triggered, rotors being corrected */
static volatile int    g_wind_bcast_now    = 0; /* 1 = broadcast immediately                     */
static volatile int    g_storm_timer_secs  = 0; /* >0 = sustain elapsed s, <0 = release elapsed s */

/* Runtime config path — set by cmd_run, used by udp_rx_thread to persist storm state */
static char            g_config_path[PATH_MAX] = DEFAULT_CONFIG;

/* GUI Unix socket client list (gui_server_thread writes, poll/wind threads read) */
static int             g_gui_fds[MAX_GUI_CLIENTS];
static int             g_num_gui_fds = 0;
static pthread_mutex_t g_gui_lock = PTHREAD_MUTEX_INITIALIZER;

/* Last PASSKEY received from Ecowitt station — sent to GUI on arrival and on WIND? */
static char            g_ecowitt_last_pk[64] = "";

/* Ecowitt fallback state — written by wind_thread, read by gui broadcast */
static volatile time_t g_last_ecowitt_rx     = 0; /* unix time of last successful Ecowitt push */
static volatile int    g_wind_fallback_active = 0; /* 1 = currently using internet fallback     */

/* Forward declarations */
static int  config_write(const char *path);
static void gui_send_all(const char *msg);
static void storm_apply_goto(void);

/* ─── Time helper ────────────────────────────────────────────────────────── */
static long long now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

/* ─── Logging ───────────────────────────────────────────────────────────── */

/* Remove log entries older than LOG_KEEP_HOURS.
   Must be called with g_log_lock held and g_logfp open. */
static void log_rotate(void)
{
    if (!g_logfp) return;
    fflush(g_logfp);
    fclose(g_logfp);
    g_logfp = NULL;

    FILE *in = fopen(LOG_FILE, "r");
    if (!in) {
        g_logfp = fopen(LOG_FILE, "a");
        return;
    }

    char tmppath[300];
    snprintf(tmppath, sizeof(tmppath), "%s.tmp", LOG_FILE);
    FILE *out = fopen(tmppath, "w");
    if (!out) { fclose(in); g_logfp = fopen(LOG_FILE, "a"); return; }

    time_t cutoff = time(NULL) - (time_t)LOG_KEEP_HOURS * 3600;
    char buf[2048];
    while (fgets(buf, sizeof(buf), in)) {
        /* Parse [YYYY-MM-DD HH:MM:SS] */
        struct tm tm2 = {0};
        int yr, mo, dy, hr, mi, se;
        if (sscanf(buf, "[%d-%d-%d %d:%d:%d]", &yr, &mo, &dy, &hr, &mi, &se) == 6) {
            tm2.tm_year  = yr - 1900;
            tm2.tm_mon   = mo - 1;
            tm2.tm_mday  = dy;
            tm2.tm_hour  = hr;
            tm2.tm_min   = mi;
            tm2.tm_sec   = se;
            tm2.tm_isdst = -1;
            if (mktime(&tm2) < cutoff) continue;  /* drop old entry */
        }
        /* Drop high-frequency broadcast lines that no longer belong in the
           log file (may still be present from older server versions). */
        if (strstr(buf, "] Bcast")           ||
            strstr(buf, "] Bcast[SIM]")      ||
            strstr(buf, "] Wind: broadcast")) continue;
        fputs(buf, out);
    }
    fclose(in);
    fclose(out);
    rename(tmppath, LOG_FILE);
    g_logfp = fopen(LOG_FILE, "a");
}

static void vlog(const char *fmt, va_list ap)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &t);

    pthread_mutex_lock(&g_log_lock);

    /* Rotate once per hour to keep file within LOG_KEEP_HOURS window */
    static time_t s_last_rotate = 0;
    if (now - s_last_rotate >= LOG_ROTATE_INTERVAL) {
        s_last_rotate = now;
        log_rotate();
    }

    va_list ap2;
    va_copy(ap2, ap);
    fprintf(stdout, "[%s] ", ts);
    vfprintf(stdout, fmt, ap);
    fputc('\n', stdout);
    fflush(stdout);

    if (g_logfp) {
        fprintf(g_logfp, "[%s] ", ts);
        vfprintf(g_logfp, fmt, ap2);
        fputc('\n', g_logfp);
        fflush(g_logfp);
    }
    va_end(ap2);

    pthread_mutex_unlock(&g_log_lock);
}

static void logmsg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(fmt, ap);
    va_end(ap);
}

/* logdbg — stdout only, NOT written to the log file.
   Use for high-frequency entries (Bcast, Wind broadcast) that would
   otherwise flood the file and bury meaningful log entries. */
static void logdbg(const char *fmt, ...)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &t);

    pthread_mutex_lock(&g_log_lock);
    va_list ap;
    va_start(ap, fmt);
    fprintf(stdout, "[%s] ", ts);
    vfprintf(stdout, fmt, ap);
    fputc('\n', stdout);
    fflush(stdout);
    va_end(ap);
    pthread_mutex_unlock(&g_log_lock);
}

/* ─── Device path helpers ────────────────────────────────────────────────── */

/* Build full device path from by-path filename */
static void by_path_to_dev(const char *by_path_name, char *out, int outsz)
{
    snprintf(out, (size_t)outsz, "%s/%s", BY_PATH_DIR, by_path_name);
}

/* Resolve by-path symlink to actual /dev/ttyXXX name */
static void resolve_symlink(const char *path, char *out, int outsz)
{
    char resolved[256];
    ssize_t n = readlink(path, resolved, sizeof(resolved) - 1);
    if (n <= 0) {
        snprintf(out, (size_t)outsz, "%s", path);
        return;
    }
    resolved[n] = '\0';
    /* readlink gives relative path like ../../ttyUSB0 */
    const char *base = strrchr(resolved, '/');
    snprintf(out, (size_t)outsz, "/dev/%.*s", outsz - 6, base ? base + 1 : resolved);
}

/* ─── Serial helpers ────────────────────────────────────────────────────── */
static int serial_open(const char *dev, int baud)
{
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;

    struct termios tio;
    memset(&tio, 0, sizeof(tio));

    speed_t speed = B9600;
    switch (baud) {
        case 4800:  speed = B4800;  break;
        case 9600:  speed = B9600;  break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
    }

    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);
    tio.c_cflag = CS8 | CLOCAL | CREAD;
    tio.c_iflag = IGNPAR;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    tcflush(fd, TCIFLUSH);
    if (tcsetattr(fd, TCSANOW, &tio) < 0) { close(fd); return -1; }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    return fd;
}

static int serial_cmd(int fd, const char *cmd,
                      char *resp, int resp_sz, int timeout_ms)
{
    tcflush(fd, TCIFLUSH);
    if (write(fd, cmd, strlen(cmd)) < 0) return -1;

    int total = 0;
    long deadline_us = timeout_ms * 1000L;
    struct timeval tv_start, tv_now;
    gettimeofday(&tv_start, NULL);

    while (total < resp_sz - 1) {
        gettimeofday(&tv_now, NULL);
        long elapsed = (tv_now.tv_sec  - tv_start.tv_sec)  * 1000000L
                     + (tv_now.tv_usec - tv_start.tv_usec);
        if (elapsed >= deadline_us) break;

        long remain = deadline_us - elapsed;
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval to;
        to.tv_sec  = remain / 1000000L;
        to.tv_usec = remain % 1000000L;

        int r = select(fd + 1, &rfds, NULL, NULL, &to);
        if (r <= 0) break;

        int n = (int)read(fd, resp + total, resp_sz - 1 - total);
        if (n <= 0) break;
        total += n;
        if (resp[total - 1] == '\r' || resp[total - 1] == '\n') break;
    }

    resp[total] = '\0';
    return total;
}

/* ─── YAESU protocol ─────────────────────────────────────────────────────── */

/*
 * Send "C\r", auto-detect response format on first call:
 *   GS232B:  "AZ=XXX"
 *   GS232A:  "+XXX"
 */
/* ─── YAESU GS-232A/B ────────────────────────────────────────────────────── */
static int yaesu_query_az(Rotor *r, double *az)
{
    char resp[64];
    int n = serial_cmd(r->fd, "C\r", resp, sizeof(resp), SERIAL_TIMEOUT_MS);
    if (n <= 0) return -1;

    char *pa = strstr(resp, "AZ=");
    if (pa) {
        if (r->variant == YAESU_UNKNOWN) r->variant = YAESU_GS232B;
        *az = atof(pa + 3);
        return 0;
    }
    char *pp = strchr(resp, '+');
    if (pp) {
        if (r->variant == YAESU_UNKNOWN) r->variant = YAESU_GS232A;
        *az = atof(pp + 1);
        return 0;
    }
    return -1;
}

static void yaesu_goto_az(Rotor *r, double az)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "M%03d\r", (int)(az + 0.5));
    char resp[64];
    serial_cmd(r->fd, cmd, resp, sizeof(resp), SERIAL_TIMEOUT_MS);
}

static void yaesu_stop(Rotor *r)
{
    char resp[64];
    serial_cmd(r->fd, "S\r", resp, sizeof(resp), SERIAL_TIMEOUT_MS);
}

/* ─── Prosistel CBOX 2003 ────────────────────────────────────────────────── */
/*
 * Frame format:  \x02 <id> <cmd> \r
 * Reply format:  \x02 <id> , <cmd> , <data> \r
 * We use identifier 'A' (azimuth) only.
 */
#define PST_HDR  "\x02"
#define PST_ID   "A"

static void prosistel_disable_cpm(Rotor *r)
{
    /* Send S (Stop CPM) — ignore reply */
    char resp[64];
    serial_cmd(r->fd, PST_HDR PST_ID "S\r", resp, sizeof(resp), SERIAL_TIMEOUT_MS);
}

static int prosistel_query_az(Rotor *r, double *az)
{
    char resp[64];
    int n = serial_cmd(r->fd, PST_HDR PST_ID "?\r", resp, sizeof(resp), SERIAL_TIMEOUT_MS);
    if (n <= 0) return -1;

    /* Reply: \x02A,?,NNN,S\r  — find ",?," then read 3-digit angle */
    char *p = strstr(resp, ",?,");
    if (!p) return -1;
    p += 3;   /* points at first digit of angle */
    *az = atof(p);
    return 0;
}

static void prosistel_goto_az(Rotor *r, double az)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), PST_HDR PST_ID "G%03d\r", (int)(az + 0.5));
    char resp[64];
    serial_cmd(r->fd, cmd, resp, sizeof(resp), SERIAL_TIMEOUT_MS);
}

static void prosistel_stop(Rotor *r)
{
    /* G999 = fast stop */
    char resp[64];
    serial_cmd(r->fd, PST_HDR PST_ID "G999\r", resp, sizeof(resp), SERIAL_TIMEOUT_MS);
}

/* ─── Protocol dispatch ──────────────────────────────────────────────────── */
static int rotor_query_az(Rotor *r, double *az)
{
    return (r->protocol == PROTO_PROSISTEL)
           ? prosistel_query_az(r, az)
           : yaesu_query_az(r, az);
}

static void rotor_goto_az(Rotor *r, double az)
{
    if (az < 0.0)   az = 0.0;
    if (az > 360.0) az = 360.0;
    if (r->protocol == PROTO_PROSISTEL) prosistel_goto_az(r, az);
    else                                yaesu_goto_az(r, az);
}

static void rotor_stop(Rotor *r)
{
    if (r->protocol == PROTO_PROSISTEL) prosistel_stop(r);
    else                                yaesu_stop(r);
}

/* ─── Find rotor by name ────────────────────────────────────────────────── */
static Rotor *find_rotor(const char *name)
{
    for (int i = 0; i < g_num_rotors; i++)
        if (strcasecmp(g_rotors[i].cfg.name, name) == 0)
            return &g_rotors[i];
    return NULL;
}

/* ─── XML parser ─────────────────────────────────────────────────────────── */
static int xml_get(const char *src, const char *tag, char *dst, int dsz)
{
    char open[64], close[64];
    snprintf(open,  sizeof(open),  "<%s>",  tag);
    snprintf(close, sizeof(close), "</%s>", tag);

    const char *s = strstr(src, open);
    if (!s) return 0;
    s += strlen(open);
    const char *e = strstr(s, close);
    if (!e) return 0;

    int len = (int)(e - s);
    if (len >= dsz) len = dsz - 1;
    memcpy(dst, s, (size_t)len);
    dst[len] = '\0';
    return 1;
}

static int xml_has(const char *src, const char *tag)
{
    char open[64];
    snprintf(open, sizeof(open), "<%s>", tag);
    return strstr(src, open) != NULL;
}

/* ─── Handle N1MM command ───────────────────────────────────────────────── */
static void handle_xml(const char *xml, int len)
{
    char buf[4096];
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    memcpy(buf, xml, (size_t)len);
    buf[len] = '\0';

    if (!strstr(buf, "<N1MMRotor>")) return;

    if (xml_has(buf, "stop")) {
        char rname[64] = {0};
        if (!xml_get(buf, "rotor", rname, sizeof(rname))) {
            char stop_block[512] = {0};
            xml_get(buf, "stop", stop_block, sizeof(stop_block));
            xml_get(stop_block, "rotor", rname, sizeof(rname));
        }
        if (!rname[0]) return;
        Rotor *r = find_rotor(rname);
        if (!r) { logmsg("STOP: unknown rotor '%s'", rname); return; }
        pthread_mutex_lock(&r->lock);
        rotor_stop(r);
        r->moving = 0;
        pthread_mutex_unlock(&r->lock);
        logmsg("STOP rotor '%s'", rname);

    } else {
        char rname[64]    = {0};
        char goazi_s[32]  = {0};
        char offset_s[32] = {0};

        if (!xml_get(buf, "rotor", rname,    sizeof(rname)))   return;
        if (!xml_get(buf, "goazi", goazi_s,  sizeof(goazi_s))) return;
        xml_get(buf, "offset", offset_s, sizeof(offset_s));

        double target = atof(goazi_s) + atof(offset_s);
        Rotor *r = find_rotor(rname);
        if (!r) { logmsg("GOTO: unknown rotor '%s'", rname); return; }

        if (r->storm_active && g_cfg.storm_block_manual && !r->cfg.always_controllable) {
            logmsg("GOTO rotor '%s' ignored — storm override active", rname);
            return;
        }

        double actual_az = target + r->cfg.offset;

        pthread_mutex_lock(&r->lock);
        r->target_az    = actual_az;
        r->moving       = 1;
        r->stall_ref_az = r->current_az;
        r->stall_ref_ms = now_ms();
        /* Compute total arc for simulate ramp profile */
        if (r->cfg.simulate) {
            /* Direct distance — no wrap, respects mechanical stop */
            r->sim_dist = fabs(r->target_az - r->current_az);
        }
        r->last_manual_goto_ms = now_ms();
        rotor_goto_az(r, actual_az);
        pthread_mutex_unlock(&r->lock);
        logmsg("GOTO rotor '%s' → %.1f° (goazi=%s n1mm_offset=%s rotor_offset=%d)",
               rname, actual_az, goazi_s, offset_s[0] ? offset_s : "0", r->cfg.offset);
    }
}

/* ─── UDP receive thread ────────────────────────────────────────────────── */
static void *udp_rx_thread(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { logmsg("udp_rx: socket: %s", strerror(errno)); return NULL; }

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)g_cfg.cmd_port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        logmsg("udp_rx: bind port %d: %s", g_cfg.cmd_port, strerror(errno));
        close(sock);
        return NULL;
    }
    logmsg("Listening for N1MM commands on UDP port %d", g_cfg.cmd_port);

    char buf[8192];
    while (g_running) {
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        int n = (int)recvfrom(sock, buf, sizeof(buf) - 1, 0,
                              (struct sockaddr *)&from, &fromlen);
        if (n > 0) {
            buf[n] = '\0';
            if (strncmp(buf, "STORM ON", 8) == 0) {
                g_storm_active      = 1;
                g_cfg.storm_enabled = 1;
                config_write(g_config_path);          /* persist across restarts */
                g_wind_bcast_now    = 1;
                logmsg("Storm mode ACTIVATED (GUI)");
            } else if (strncmp(buf, "STORM OFF", 9) == 0) {
                g_storm_active      = 0;
                g_storm_correcting  = 0;
                g_cfg.storm_enabled = 0;
                for (int i = 0; i < g_num_rotors; i++)
                    g_rotors[i].storm_active = 0;     /* stop all correction */
                config_write(g_config_path);          /* persist across restarts */
                g_wind_bcast_now    = 1;
                logmsg("Storm mode DEACTIVATED (GUI)");
            } else if (strncmp(buf, "STORM FORCE", 11) == 0) {
                /* Force immediate storm correction — bypass sustain timer */
                g_storm_active     = 1;
                g_cfg.storm_enabled = 1;
                g_storm_correcting  = 1;
                for (int i = 0; i < g_num_rotors; i++)
                    g_rotors[i].storm_active = g_cfg.rotors[i].storm_enabled;
                storm_apply_goto();
                config_write(g_config_path);
                g_wind_bcast_now    = 1;
                logmsg("Storm correction FORCED (UDP — bypass sustain)");
            } else if (strncmp(buf, "WIND?", 5) == 0) {
                g_wind_bcast_now  = 1; /* GUI requesting immediate sync       */
            } else {
                handle_xml(buf, n);
            }
        }
    }
    close(sock);
    return NULL;
}

/* ─── Device list types (also used by wizard helpers below) ─────────────── */
#define MAX_DEV_LIST 32
typedef struct {
    char by_path[256];
    char by_path_alt[256];
    char resolved[256];
    char serial[64];
} DevEntry;

static int build_dev_list(DevEntry *list, int maxn);   /* defined below */

/* ─── USB reconnect helper ──────────────────────────────────────────────── */
static void try_reconnect(Rotor *r, long long now)
{
    /* Resolve current device path; fall back to serial number if by-path moved */
    char devpath[512];
    by_path_to_dev(r->cfg.by_path, devpath, sizeof(devpath));

    if (access(devpath, F_OK) != 0 && r->cfg.serial[0]) {
        DevEntry list[MAX_DEV_LIST];
        int ndev = build_dev_list(list, MAX_DEV_LIST);
        for (int j = 0; j < ndev; j++) {
            if (strcmp(list[j].serial, r->cfg.serial) == 0) {
                logmsg("Rotor '%s': by-path changed, found via serial %s → %s",
                       r->cfg.name, r->cfg.serial, list[j].by_path);
                snprintf(r->cfg.by_path, sizeof(r->cfg.by_path), "%s", list[j].by_path);
                by_path_to_dev(r->cfg.by_path, devpath, sizeof(devpath));
                break;
            }
        }
    }

    int fd = serial_open(devpath, r->cfg.baud > 0 ? r->cfg.baud : 9600);
    if (fd < 0) {
        r->next_reconnect_ms = now + RECONNECT_INTERVAL_MS;
        return;
    }

    r->fd            = fd;
    r->consec_errors = 0;
    r->moving        = 0;

    char resolved[256];
    resolve_symlink(devpath, resolved, sizeof(resolved));
    logmsg("Rotor '%s': reconnected on %s", r->cfg.name, resolved);

    if (r->protocol == PROTO_PROSISTEL) {
        prosistel_disable_cpm(r);
        logmsg("Rotor '%s': CPM disabled", r->cfg.name);
    }
}

/* ─── Send a newline-terminated message to all connected GUI clients ─────── */
static void gui_send_all(const char *msg)
{
    size_t len = strlen(msg);
    pthread_mutex_lock(&g_gui_lock);
    for (int i = 0; i < g_num_gui_fds; i++) {
        if (g_gui_fds[i] >= 0)
            send(g_gui_fds[i], msg, len, MSG_NOSIGNAL | MSG_DONTWAIT);
    }
    pthread_mutex_unlock(&g_gui_lock);
}

/* ─── Poll + broadcast thread ───────────────────────────────────────────── */
static void *poll_bcast_thread(void *arg)
{
    (void)arg;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { logmsg("bcast: socket: %s", strerror(errno)); return NULL; }

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    /* Build list of broadcast destinations (primary port) */
    struct sockaddr_in dsts[8];
    int ndsts = 0;
    char addr_buf[256];
    snprintf(addr_buf, sizeof(addr_buf), "%s", g_cfg.bcast_addr);
    char *tok = strtok(addr_buf, ",");
    while (tok && ndsts < 8) {
        while (*tok == ' ') tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && (*end == ' ' || *end == '\r' || *end == '\n')) *end-- = '\0';

        memset(&dsts[ndsts], 0, sizeof(dsts[ndsts]));
        dsts[ndsts].sin_family = AF_INET;
        dsts[ndsts].sin_port   = htons((uint16_t)g_cfg.bcast_port);
        if (inet_pton(AF_INET, tok, &dsts[ndsts].sin_addr) == 1) {
            logmsg("Broadcast target: %s port %d", tok, g_cfg.bcast_port);
            ndsts++;
        } else {
            logmsg("WARNING: invalid broadcast address '%s' — skipped", tok);
        }
        tok = strtok(NULL, ",");
    }
    if (ndsts == 0) {
        logmsg("WARNING: no valid broadcast addresses — using 255.255.255.255");
        memset(&dsts[0], 0, sizeof(dsts[0]));
        dsts[0].sin_family      = AF_INET;
        dsts[0].sin_addr.s_addr = htonl(INADDR_BROADCAST);
        dsts[0].sin_port        = htons((uint16_t)g_cfg.bcast_port);
        ndsts = 1;
    } else {
        /* altijd ook naar 255.255.255.255 sturen zodat de GUI op dezelfde
           machine broadcasts altijd ontvangt, ook als alleen subnet-adressen
           geconfigureerd zijn */
        int has_limited = 0;
        for (int i = 0; i < ndsts; i++)
            if (dsts[i].sin_addr.s_addr == htonl(INADDR_BROADCAST))
                { has_limited = 1; break; }
        if (!has_limited && ndsts < 8) {
            memset(&dsts[ndsts], 0, sizeof(dsts[ndsts]));
            dsts[ndsts].sin_family      = AF_INET;
            dsts[ndsts].sin_addr.s_addr = htonl(INADDR_BROADCAST);
            dsts[ndsts].sin_port        = htons((uint16_t)g_cfg.bcast_port);
            ndsts++;
        }
    }

    /* Build secondary destination list (same addresses, bcast_port2) */
    struct sockaddr_in dsts2[8];
    int ndsts2 = 0;
    if (g_cfg.bcast_port2 > 0) {
        for (int i = 0; i < ndsts; i++) {
            dsts2[i] = dsts[i];
            dsts2[i].sin_port = htons((uint16_t)g_cfg.bcast_port2);
        }
        ndsts2 = ndsts;
        logmsg("Secondary broadcast port: %d", g_cfg.bcast_port2);
    }

    int idle_ms   = g_cfg.idle_ms   > 0 ? g_cfg.idle_ms   : DEFAULT_IDLE_MS;
    int moving_ms = g_cfg.moving_ms > 0 ? g_cfg.moving_ms : DEFAULT_MOVING_MS;

    /* Poll at moving_ms tick rate; broadcast per rotor based on its state */
    while (g_running) {
        long long t = now_ms();

        for (int i = 0; i < g_num_rotors; i++) {
            Rotor *r = &g_rotors[i];

            /* ── Simulate mode: move az toward target, no serial port ── */
            if (r->cfg.simulate) {
                pthread_mutex_lock(&r->lock);
                int sim_interval = r->moving ? moving_ms : idle_ms;
                int sim_due      = (t - r->last_bcast_ms) >= sim_interval;
                if (r->moving) {
                    /* Trapezoidal velocity profile:
                     *   ramp up over first  SIM_RAMP_DEG degrees
                     *   ramp down over last SIM_RAMP_DEG degrees
                     *   full speed in between
                     *   speed range: SIM_MIN_DEG_S .. SIM_MAX_DEG_S  */
#define SIM_MAX_DEG_S  20.0    /* °/s at full speed                    */
#define SIM_MIN_DEG_S   1.5    /* °/s at ramp start/end                */
#define SIM_RAMP_DEG   30.0    /* degrees over which to ramp           */
                    /* Direct path — never cross 0/360 mechanical stop */
                    double diff = r->target_az - r->current_az;
                    double dist_rem = fabs(diff);
                    double dist_done = (r->sim_dist > 0)
                                       ? (r->sim_dist - dist_rem) : 0.0;
                    if (dist_done < 0.0) dist_done = 0.0;
                    double ramp = fmin(1.0, fmin(dist_done   / SIM_RAMP_DEG,
                                                  dist_rem    / SIM_RAMP_DEG));
                    double speed = SIM_MIN_DEG_S
                                 + (SIM_MAX_DEG_S - SIM_MIN_DEG_S) * ramp;
                    double step = speed * sim_interval / 1000.0;
                    if (dist_rem <= step) {
                        r->current_az = r->target_az;
                        r->moving     = 0;
                        r->sim_dist   = 0.0;
                    } else {
                        r->current_az += (diff > 0.0 ? step : -step);
                        /* Clamp — never cross the mechanical stop */
                        if (r->current_az <   0.0) r->current_az = 0.0;
                        if (r->current_az > 360.0) r->current_az = 360.0;
                    }
#undef SIM_MAX_DEG_S
#undef SIM_MIN_DEG_S
#undef SIM_RAMP_DEG
                }
                double sim_az     = r->current_az;
                double sim_target = r->target_az;
                if (sim_due) r->last_bcast_ms = t;
                pthread_mutex_unlock(&r->lock);
                if (!sim_due) continue;
                /* Broadcast simulated position */
                char sim_msg[256];
                double sim_rep_az  = fmod(sim_az     - r->cfg.offset + 360.0, 360.0);
                double sim_rep_tgt = fmod(sim_target  - r->cfg.offset + 360.0, 360.0);
                snprintf(sim_msg, sizeof(sim_msg), "%.63s @ %d / %d",
                         r->cfg.name,
                         (int)(sim_rep_az  * 10 + 0.5),
                         (int)(sim_rep_tgt * 10 + 0.5));
                for (int d = 0; d < ndsts;  d++)
                    sendto(sock, sim_msg, strlen(sim_msg), 0,
                           (struct sockaddr *)&dsts[d],  sizeof(dsts[d]));
                for (int d = 0; d < ndsts2; d++)
                    sendto(sock, sim_msg, strlen(sim_msg), 0,
                           (struct sockaddr *)&dsts2[d], sizeof(dsts2[d]));
                char sim_gmsg[320];
                long long sim_lmg = r->last_manual_goto_ms / 1000LL;
                int gl = snprintf(sim_gmsg, sizeof(sim_gmsg), "%s | %lld\n", sim_msg, sim_lmg);
                if (gl > 0 && gl < (int)sizeof(sim_gmsg)) gui_send_all(sim_gmsg);
                logdbg("Bcast[SIM]%s: %s", r->moving ? " [moving]" : "", sim_msg);
                continue;
            }

            pthread_mutex_lock(&r->lock);
            if (r->fd < 0) {
                if (t >= r->next_reconnect_ms)
                    try_reconnect(r, t);
                pthread_mutex_unlock(&r->lock);
                continue;
            }
            pthread_mutex_unlock(&r->lock);

            pthread_mutex_lock(&r->lock);
            int is_moving = r->moving;
            int interval  = is_moving ? moving_ms : idle_ms;
            int due       = (t - r->last_bcast_ms) >= interval;
            pthread_mutex_unlock(&r->lock);

            if (!due) continue;

            double az = 0.0;
            pthread_mutex_lock(&r->lock);
            int ok = (rotor_query_az(r, &az) == 0);
            if (ok) {
                r->consec_errors = 0;
                r->current_az = az;
                if (r->moving) {
                    if (fabs(az - r->target_az) < 2.0) {
                        r->moving = 0;
                    } else if (fabs(az - r->stall_ref_az) >= STALL_MIN_DEG) {
                        /* rotor is actually moving — refresh stall reference */
                        r->stall_ref_az = az;
                        r->stall_ref_ms = t;
                    } else if ((t - r->stall_ref_ms) >= STALL_TIMEOUT_MS) {
                        logmsg("Rotor '%s': stall — no movement for %d ms (az=%.1f°), back to idle interval",
                               r->cfg.name, STALL_TIMEOUT_MS, az);
                        r->moving = 0;
                    }
                }
            } else {
                az = r->current_az;
                r->consec_errors++;
                if (r->consec_errors >= MAX_CONSEC_ERRORS) {
                    logmsg("Rotor '%s': %d consecutive errors — port closed, retry in %d s",
                           r->cfg.name, MAX_CONSEC_ERRORS, RECONNECT_INTERVAL_MS / 1000);
                    close(r->fd);
                    r->fd               = -1;
                    r->consec_errors    = 0;
                    r->moving           = 0;
                    r->next_reconnect_ms = t + RECONNECT_INTERVAL_MS;
                }
            }
            double target_az_snap = r->target_az;   /* read inside lock */
            r->last_bcast_ms = t;
            pthread_mutex_unlock(&r->lock);

            char msg[256];
            double reported_az     = fmod(az              - r->cfg.offset + 360.0, 360.0);
            double reported_target = fmod(target_az_snap  - r->cfg.offset + 360.0, 360.0);
            /* Format: "NAME @ CURRENT / TARGET"
             * N1MM reads the integer after '@' and stops at the space — backward compatible. */
            snprintf(msg, sizeof(msg), "%.63s @ %d / %d",
                     r->cfg.name,
                     (int)(reported_az     * 10 + 0.5),
                     (int)(reported_target * 10 + 0.5));

            for (int d = 0; d < ndsts; d++)
                sendto(sock, msg, strlen(msg), 0,
                       (struct sockaddr *)&dsts[d], sizeof(dsts[d]));
            for (int d = 0; d < ndsts2; d++)
                sendto(sock, msg, strlen(msg), 0,
                       (struct sockaddr *)&dsts2[d], sizeof(dsts2[d]));

            /* Push to connected GUI clients over Unix socket (extended with last_manual_goto) */
            {
                char gmsg[320];
                long long lmg = r->last_manual_goto_ms / 1000LL;
                int gl = snprintf(gmsg, sizeof(gmsg), "%s | %lld\n", msg, lmg);
                if (gl > 0 && gl < (int)sizeof(gmsg))
                    gui_send_all(gmsg);
            }

            logdbg("Bcast%s%s: %s",
                   ok ? "" : "(cached)",
                   is_moving ? " [moving]" : "",
                   msg);

        }

        usleep((useconds_t)(moving_ms * 1000));
    }
    close(sock);
    return NULL;
}

/* ─── Config file ────────────────────────────────────────────────────────── */
static int config_write(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "Cannot write config: %s\n", path); return -1; }

    fprintf(f, "# N1MM Rotor Server — server settings\n");
    fprintf(f, "cmd_port=%d\n",     g_cfg.cmd_port);
    fprintf(f, "bcast_port=%d\n",   g_cfg.bcast_port);
    fprintf(f, "bcast_port2=%d\n",  g_cfg.bcast_port2);
    fprintf(f, "bcast_addr=%s\n",   g_cfg.bcast_addr);
    fprintf(f, "idle_ms=%d\n",     g_cfg.idle_ms);
    fprintf(f, "moving_ms=%d\n",   g_cfg.moving_ms);

    fprintf(f, "# Wind monitor\n");
    fprintf(f, "wind_enabled=%d\n",        g_cfg.wind_enabled);
    fprintf(f, "wind_source=%s\n",         g_cfg.wind_source[0] ? g_cfg.wind_source : "openmeteo");
    fprintf(f, "wind_lat=%.6f\n",          g_cfg.wind_lat);
    fprintf(f, "wind_lon=%.6f\n",          g_cfg.wind_lon);
    fprintf(f, "wind_interval_min=%d\n",   g_cfg.wind_interval_min);
    fprintf(f, "wind_serial_device=%s\n",  g_cfg.wind_serial_device);
    fprintf(f, "wind_serial_baud=%d\n",    g_cfg.wind_serial_baud);
    fprintf(f, "wind_udp_port=%d\n",       g_cfg.wind_udp_port);
    fprintf(f, "wind_owm_apikey=%s\n",     g_cfg.wind_owm_apikey);
    fprintf(f, "wind_wapi_apikey=%s\n",   g_cfg.wind_wapi_apikey);
    fprintf(f, "wind_ecowitt_port=%d\n",          g_cfg.wind_ecowitt_port);
    fprintf(f, "wind_ecowitt_passkey=%s\n",       g_cfg.wind_ecowitt_passkey);
    fprintf(f, "wind_ecowitt_fallback=%s\n",      g_cfg.wind_ecowitt_fallback);
    fprintf(f, "wind_ecowitt_fallback_min=%d\n",  g_cfg.wind_ecowitt_fallback_min);
    fprintf(f, "wind_ecowitt_fwd_host=%s\n",      g_cfg.wind_ecowitt_fwd_host);
    fprintf(f, "wind_ecowitt_fwd_port=%d\n",      g_cfg.wind_ecowitt_fwd_port);
    fprintf(f, "wind_ecowitt_fwd_path=%s\n",       g_cfg.wind_ecowitt_fwd_path);
    fprintf(f, "wind_ecowitt_correction=%d\n\n",   g_cfg.wind_ecowitt_correction);

    fprintf(f, "# Storm monitor\n");
    fprintf(f, "storm_enabled=%d\n",       g_cfg.storm_enabled);
    fprintf(f, "storm_threshold_bft=%d\n", g_cfg.storm_threshold_bft);
    fprintf(f, "storm_sustain_min=%d\n",   g_cfg.storm_sustain_min);
    fprintf(f, "storm_release_min=%d\n",   g_cfg.storm_release_min);
    fprintf(f, "storm_interval_min=%d\n",  g_cfg.storm_interval_min);
    fprintf(f, "storm_block_manual=%d\n\n",g_cfg.storm_block_manual);

    fprintf(f, "# Web interface\n");
    fprintf(f, "web_enabled=%d\n",       g_cfg.web_enabled);
    fprintf(f, "web_port=%d\n",          g_cfg.web_port);
    fprintf(f, "web_password=%s\n\n",    g_cfg.web_password);

    fprintf(f, "# Rotors\n");
    fprintf(f, "num_rotors=%d\n\n", g_cfg.num_rotors);

    for (int i = 0; i < g_cfg.num_rotors; i++) {
        RotorCfg *c = &g_cfg.rotors[i];
        fprintf(f, "rotor%d_name=%s\n",          i, c->name);
        fprintf(f, "rotor%d_by_path=%s\n",        i, c->by_path);
        fprintf(f, "rotor%d_serial=%s\n",         i, c->serial);
        fprintf(f, "rotor%d_protocol=%s\n",       i, c->protocol[0] ? c->protocol : "YAESU");
        fprintf(f, "rotor%d_baud=%d\n",           i, c->baud);
        fprintf(f, "rotor%d_offset=%d\n",         i, c->offset);
        fprintf(f, "rotor%d_storm_enabled=%d\n",       i, c->storm_enabled);
        fprintf(f, "rotor%d_storm_offset=%d\n",        i, c->storm_offset);
        fprintf(f, "rotor%d_always_controllable=%d\n",    i, c->always_controllable);
        fprintf(f, "rotor%d_return_timeout_min=%d\n",    i, c->return_timeout_min);
        fprintf(f, "rotor%d_simulate=%d\n\n",             i, c->simulate);
    }

    fclose(f);
    /* Maak config lees- en schrijfbaar voor iedereen zodat de GUI
     * (zonder sudo) de instellingen ook kan opslaan. */
    chmod(path, 0666);
    return 0;
}

static int config_read(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Cannot open config: %s\n", path); return -1; }

    /* defaults */
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.cmd_port    = DEFAULT_CMD_PORT;
    g_cfg.bcast_port  = DEFAULT_BCAST_PORT;
    g_cfg.bcast_port2 = DEFAULT_BCAST_PORT2;
    g_cfg.idle_ms     = DEFAULT_IDLE_MS;
    g_cfg.moving_ms  = DEFAULT_MOVING_MS;
    snprintf(g_cfg.bcast_addr, sizeof(g_cfg.bcast_addr), "%s", DEFAULT_BCAST_ADDR);
    g_cfg.wind_enabled      = 0;
    snprintf(g_cfg.wind_source, sizeof(g_cfg.wind_source), "openmeteo");
    g_cfg.wind_lat          = 52.4656;  /* default: IJmuiden */
    g_cfg.wind_lon          = 4.5314;
    g_cfg.wind_interval_min = 15;
    g_cfg.wind_serial_baud  = 19200;
    g_cfg.wind_udp_port     = 0;
    g_cfg.wind_ecowitt_port = 49199;
    g_cfg.wind_ecowitt_passkey[0]     = '\0';
    g_cfg.wind_ecowitt_fallback[0]    = '\0';
    g_cfg.wind_ecowitt_fallback_min   = 5;
    g_cfg.wind_ecowitt_fwd_host[0]    = '\0';
    g_cfg.wind_ecowitt_fwd_port       = 0;
    snprintf(g_cfg.wind_ecowitt_fwd_path, sizeof(g_cfg.wind_ecowitt_fwd_path), "/");
    g_cfg.wind_ecowitt_correction     = 100;
    g_cfg.storm_enabled         = 0;
    g_cfg.storm_threshold_bft   = 7;
    g_cfg.storm_sustain_min     = 5;
    g_cfg.storm_release_min     = 10;
    g_cfg.storm_interval_min    = 15;
    g_cfg.storm_block_manual    = 1;
    g_cfg.web_enabled           = 0;
    g_cfg.web_port              = 80;
    g_cfg.web_password[0]       = '\0';

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        int l = (int)strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r' || line[l-1] == ' '))
            line[--l] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        if      (strcmp(key, "cmd_port")    == 0) g_cfg.cmd_port    = atoi(val);
        else if (strcmp(key, "bcast_port")  == 0) g_cfg.bcast_port  = atoi(val);
        else if (strcmp(key, "bcast_port2") == 0) g_cfg.bcast_port2 = atoi(val);
        else if (strcmp(key, "bcast_addr")  == 0) snprintf(g_cfg.bcast_addr, sizeof(g_cfg.bcast_addr), "%s", val);
        else if (strcmp(key, "idle_ms")    == 0) g_cfg.idle_ms    = atoi(val);
        else if (strcmp(key, "moving_ms")  == 0) g_cfg.moving_ms  = atoi(val);
        /* logfile key ignored — path is fixed to LOG_FILE */
        else if (strcmp(key, "wind_enabled")        == 0) g_cfg.wind_enabled      = atoi(val);
        else if (strcmp(key, "wind_source")         == 0) snprintf(g_cfg.wind_source, sizeof(g_cfg.wind_source), "%s", val);
        else if (strcmp(key, "wind_lat")            == 0) g_cfg.wind_lat          = atof(val);
        else if (strcmp(key, "wind_lon")            == 0) g_cfg.wind_lon          = atof(val);
        else if (strcmp(key, "wind_interval_min")   == 0) g_cfg.wind_interval_min = atoi(val);
        else if (strcmp(key, "wind_serial_device")  == 0) snprintf(g_cfg.wind_serial_device, sizeof(g_cfg.wind_serial_device), "%s", val);
        else if (strcmp(key, "wind_serial_baud")    == 0) g_cfg.wind_serial_baud  = atoi(val);
        else if (strcmp(key, "wind_udp_port")       == 0) g_cfg.wind_udp_port     = atoi(val);
        else if (strcmp(key, "wind_owm_apikey")     == 0) snprintf(g_cfg.wind_owm_apikey,  sizeof(g_cfg.wind_owm_apikey),  "%s", val);
        else if (strcmp(key, "wind_wapi_apikey")    == 0) snprintf(g_cfg.wind_wapi_apikey, sizeof(g_cfg.wind_wapi_apikey), "%s", val);
        else if (strcmp(key, "wind_ecowitt_port")        == 0) g_cfg.wind_ecowitt_port = atoi(val);
        else if (strcmp(key, "wind_ecowitt_passkey")     == 0) snprintf(g_cfg.wind_ecowitt_passkey,    sizeof(g_cfg.wind_ecowitt_passkey),    "%s", val);
        else if (strcmp(key, "wind_ecowitt_fallback")    == 0) snprintf(g_cfg.wind_ecowitt_fallback,    sizeof(g_cfg.wind_ecowitt_fallback),    "%s", val);
        else if (strcmp(key, "wind_ecowitt_fallback_min")== 0) g_cfg.wind_ecowitt_fallback_min = atoi(val);
        else if (strcmp(key, "wind_ecowitt_fwd_host")    == 0) snprintf(g_cfg.wind_ecowitt_fwd_host,    sizeof(g_cfg.wind_ecowitt_fwd_host),    "%s", val);
        else if (strcmp(key, "wind_ecowitt_fwd_port")    == 0) g_cfg.wind_ecowitt_fwd_port = atoi(val);
        else if (strcmp(key, "wind_ecowitt_fwd_path")       == 0) snprintf(g_cfg.wind_ecowitt_fwd_path,    sizeof(g_cfg.wind_ecowitt_fwd_path),    "%s", val);
        else if (strcmp(key, "wind_ecowitt_correction")     == 0) g_cfg.wind_ecowitt_correction = atoi(val);
        else if (strcmp(key, "storm_enabled")            == 0) g_cfg.storm_enabled       = atoi(val);
        else if (strcmp(key, "storm_threshold_bft") == 0) g_cfg.storm_threshold_bft = atoi(val);
        else if (strcmp(key, "storm_sustain_min")   == 0) g_cfg.storm_sustain_min   = atoi(val);
        else if (strcmp(key, "storm_release_min")   == 0) g_cfg.storm_release_min   = atoi(val);
        else if (strcmp(key, "storm_interval_min")  == 0) g_cfg.storm_interval_min  = atoi(val);
        else if (strcmp(key, "storm_block_manual")  == 0) g_cfg.storm_block_manual  = atoi(val);
        else if (strcmp(key, "web_enabled")   == 0) g_cfg.web_enabled   = atoi(val);
        else if (strcmp(key, "web_port")      == 0) g_cfg.web_port      = atoi(val);
        else if (strcmp(key, "web_password")  == 0) snprintf(g_cfg.web_password, sizeof(g_cfg.web_password), "%s", val);
        else if (strcmp(key, "num_rotors") == 0) {
            g_cfg.num_rotors = atoi(val);
            if (g_cfg.num_rotors > MAX_ROTORS) g_cfg.num_rotors = MAX_ROTORS;
        } else {
            int idx = -1; char field[64];
            if (sscanf(key, "rotor%d_%63s", &idx, field) == 2 &&
                idx >= 0 && idx < MAX_ROTORS) {
                RotorCfg *c = &g_cfg.rotors[idx];
                if      (strcmp(field, "name")     == 0) snprintf(c->name,     sizeof(c->name),     "%s", val);
                else if (strcmp(field, "by_path")  == 0) snprintf(c->by_path,  sizeof(c->by_path),  "%s", val);
                else if (strcmp(field, "serial")   == 0) snprintf(c->serial,   sizeof(c->serial),   "%s", val);
                else if (strcmp(field, "protocol") == 0) snprintf(c->protocol, sizeof(c->protocol), "%s", val);
                else if (strcmp(field, "baud")          == 0) c->baud          = atoi(val);
                else if (strcmp(field, "offset")        == 0) c->offset        = atoi(val);
                else if (strcmp(field, "storm_enabled")       == 0) c->storm_enabled       = atoi(val);
                else if (strcmp(field, "storm_offset")        == 0) c->storm_offset        = atoi(val);
                else if (strcmp(field, "always_controllable")  == 0) c->always_controllable  = atoi(val);
                else if (strcmp(field, "simulate")             == 0) c->simulate             = atoi(val);
                else if (strcmp(field, "return_timeout_min")   == 0) c->return_timeout_min   = atoi(val);
            }
        }
    }
    fclose(f);

    /* Remove duplicate rotors (same name or same non-empty by_path) */
    for (int i = 0; i < g_cfg.num_rotors; i++) {
        for (int j = i + 1; j < g_cfg.num_rotors; j++) {
            if (strcasecmp(g_cfg.rotors[i].name, g_cfg.rotors[j].name) == 0 ||
                (g_cfg.rotors[i].by_path[0] &&
                 strcmp(g_cfg.rotors[i].by_path, g_cfg.rotors[j].by_path) == 0)) {
                /* Remove entry j by shifting down */
                for (int k = j; k < g_cfg.num_rotors - 1; k++)
                    g_cfg.rotors[k] = g_cfg.rotors[k + 1];
                g_cfg.num_rotors--;
                j--;
            }
        }
    }

    return 0;
}

/* ─── Wizard helpers ─────────────────────────────────────────────────────── */
static void wprompt(const char *prompt, const char *def, char *out, int outsz)
{
    if (def && def[0]) printf("%s [%s]: ", prompt, def);
    else               printf("%s: ", prompt);
    fflush(stdout);

    if (!fgets(out, outsz, stdin)) { out[0] = '\0'; return; }
    int l = (int)strlen(out);
    while (l > 0 && (out[l-1] == '\n' || out[l-1] == '\r')) out[--l] = '\0';
    if (out[0] == '\0' && def && def[0])
        snprintf(out, (size_t)outsz, "%s", def);
}

/* List /dev/serial/by-path entries, returns count */
static void get_dev_serial(const char *dev, char *out, int outsz)
{
    out[0] = '\0';
    char cmd[320];
    snprintf(cmd, sizeof(cmd),
             "udevadm info -q property -n %s 2>/dev/null | grep ^ID_SERIAL_SHORT=", dev);
    FILE *fp = popen(cmd, "r");
    if (!fp) return;
    char line[128];
    if (fgets(line, sizeof(line), fp)) {
        char *eq = strchr(line, '=');
        if (eq) {
            snprintf(out, (size_t)outsz, "%s", eq + 1);
            int l = (int)strlen(out);
            while (l > 0 && (out[l-1] == '\n' || out[l-1] == '\r')) out[--l] = '\0';
        }
    }
    pclose(fp);
}

static int build_dev_list(DevEntry *list, int maxn)
{
    DIR *d = opendir(BY_PATH_DIR);
    if (!d) return 0;

    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && n < maxn) {
        if (ent->d_name[0] == '.') continue;

        char full[512];
        snprintf(full, sizeof(full), "%s/%s", BY_PATH_DIR, ent->d_name);

        char resolved[256] = "?";
        resolve_symlink(full, resolved, sizeof(resolved));

        /* Skip if this resolved device is already in the list; store alt by-path */
        int dup = 0;
        for (int i = 0; i < n; i++) {
            if (strcmp(list[i].resolved, resolved) == 0) {
                if (list[i].by_path_alt[0] == '\0')
                    snprintf(list[i].by_path_alt, sizeof(list[i].by_path_alt), "%s", ent->d_name);
                dup = 1;
                break;
            }
        }
        if (dup) continue;

        list[n].by_path_alt[0] = '\0';
        snprintf(list[n].by_path,  sizeof(list[n].by_path),  "%s", ent->d_name);
        snprintf(list[n].resolved, sizeof(list[n].resolved), "%s", resolved);
        get_dev_serial(resolved, list[n].serial, sizeof(list[n].serial));
        n++;
    }
    closedir(d);
    return n;
}

static int list_by_path_devices(void)
{
    DevEntry list[MAX_DEV_LIST];
    int n = build_dev_list(list, MAX_DEV_LIST);

    if (n == 0) {
        printf("  (no devices found in %s)\n", BY_PATH_DIR);
        return 0;
    }

    printf("  %-3s  %-12s  %-24s  %-44s  %s\n", "#", "Device", "Serial", "by-path name", "Rotor");
    printf("  %-3s  %-12s  %-24s  %-44s  %s\n", "---", "------------", "------------------------", "--------------------------------------------", "-----");
    for (int i = 0; i < n; i++) {
        /* Match configured rotor: serial first, by-path as fallback */
        const char *rotor_name = "";
        for (int j = 0; j < g_cfg.num_rotors; j++) {
            if (list[i].serial[0] && g_cfg.rotors[j].serial[0] &&
                strcmp(list[i].serial, g_cfg.rotors[j].serial) == 0) {
                rotor_name = g_cfg.rotors[j].name;
                break;
            }
            if (strcmp(g_cfg.rotors[j].by_path, list[i].by_path) == 0 ||
                (list[i].by_path_alt[0] && strcmp(g_cfg.rotors[j].by_path, list[i].by_path_alt) == 0)) {
                rotor_name = g_cfg.rotors[j].name;
                break;
            }
        }
        printf("  %-3d  %-12s  %-24s  %-44s  %s\n",
               i + 1, list[i].resolved,
               list[i].serial[0] ? list[i].serial : "(none)",
               list[i].by_path,
               rotor_name);
    }

    return n;
}

/* Return by-path filename for the nth entry (1-based) */
static int get_by_path_entry(int target_n, char *out, int outsz)
{
    DevEntry list[MAX_DEV_LIST];
    int n = build_dev_list(list, MAX_DEV_LIST);
    if (target_n < 1 || target_n > n) return 0;
    snprintf(out, (size_t)outsz, "%s", list[target_n - 1].by_path);
    return 1;
}

/* ─── --setup ────────────────────────────────────────────────────────────── */
static void cmd_setup(const char *config_path)
{
    /* Load existing config as defaults */
    config_read(config_path);

    printf("\n=== N1MM Rotor Server — Setup ===\n\n");

    char buf[256];
    char def[64];

    snprintf(def, sizeof(def), "%d", g_cfg.cmd_port   > 0 ? g_cfg.cmd_port   : DEFAULT_CMD_PORT);
    wprompt("UDP command port (N1MM → server)", def, buf, sizeof(buf));
    g_cfg.cmd_port = atoi(buf);
    if (g_cfg.cmd_port <= 0) g_cfg.cmd_port = DEFAULT_CMD_PORT;

    snprintf(def, sizeof(def), "%d", g_cfg.bcast_port > 0 ? g_cfg.bcast_port : DEFAULT_BCAST_PORT);
    wprompt("UDP broadcast port (server → N1MM)", def, buf, sizeof(buf));
    g_cfg.bcast_port = atoi(buf);
    if (g_cfg.bcast_port <= 0) g_cfg.bcast_port = DEFAULT_BCAST_PORT;

    {
        static const int port2_opts[] = { 0, 13011, 13012, 13013, 13014, 13015 };
        int cur = 0;
        for (int i = 1; i < 6; i++)
            if (g_cfg.bcast_port2 == port2_opts[i]) { cur = i; break; }
        printf("Secondary broadcast port:\n");
        printf("  0) Off\n");
        for (int i = 1; i < 6; i++)
            printf("  %d) %d\n", i, port2_opts[i]);
        snprintf(def, sizeof(def), "%d", cur);
        wprompt("Choice", def, buf, sizeof(buf));
        int choice = atoi(buf);
        if (choice < 0 || choice > 5) choice = 0;
        g_cfg.bcast_port2 = port2_opts[choice];
    }

    printf("  (multiple addresses: comma-separated, e.g. 192.168.1.255,10.0.0.255)\n");
    wprompt("Broadcast address(es)", g_cfg.bcast_addr[0] ? g_cfg.bcast_addr : DEFAULT_BCAST_ADDR,
            g_cfg.bcast_addr, sizeof(g_cfg.bcast_addr));

    snprintf(def, sizeof(def), "%d", g_cfg.idle_ms > 0 ? g_cfg.idle_ms : DEFAULT_IDLE_MS);
    wprompt("Broadcast interval idle   (ms)", def, buf, sizeof(buf));
    g_cfg.idle_ms = atoi(buf);
    if (g_cfg.idle_ms <= 0) g_cfg.idle_ms = DEFAULT_IDLE_MS;

    snprintf(def, sizeof(def), "%d", g_cfg.moving_ms > 0 ? g_cfg.moving_ms : DEFAULT_MOVING_MS);
    wprompt("Broadcast interval moving (ms)", def, buf, sizeof(buf));
    g_cfg.moving_ms = atoi(buf);
    if (g_cfg.moving_ms <= 0) g_cfg.moving_ms = DEFAULT_MOVING_MS;

    /* log path is fixed — not configurable */

    printf("\n");
    if (config_write(config_path) == 0)
        printf("Server settings saved to: %s\n\n", config_path);
    else
        printf("ERROR: could not save config.\n\n");
}

/* ─── --add ──────────────────────────────────────────────────────────────── */
static void cmd_add(const char *config_path)
{
    config_read(config_path);

    if (g_cfg.num_rotors >= MAX_ROTORS) {
        fprintf(stderr, "Maximum number of rotors (%d) reached.\n", MAX_ROTORS);
        return;
    }

    printf("\n=== Add Rotor ===\n\n");

    printf("Available devices:\n");
    int ndev = list_by_path_devices();
    printf("\n");

    RotorCfg *c = &g_cfg.rotors[g_cfg.num_rotors];
    memset(c, 0, sizeof(*c));

    char buf[256];

    /* Device selection by number */
    if (ndev > 0) {
        char def_dev[8];
        snprintf(def_dev, sizeof(def_dev), "1");
        char prompt[32];
        snprintf(prompt, sizeof(prompt), "Select device [1-%d]", ndev);
        wprompt(prompt, def_dev, buf, sizeof(buf));
    } else {
        wprompt("Device (by-path filename)", "", buf, sizeof(buf));
    }
    if (!buf[0]) { printf("Cancelled.\n"); return; }

    if (ndev > 0 && buf[0] >= '1' && buf[0] <= '9') {
        int sel = atoi(buf);
        if (!get_by_path_entry(sel, c->by_path, sizeof(c->by_path))) {
            fprintf(stderr, "Invalid selection.\n");
            return;
        }
    } else {
        snprintf(c->by_path, sizeof(c->by_path), "%s", buf);
    }

    /* Check by-path before asking anything else */
    for (int i = 0; i < g_cfg.num_rotors; i++) {
        if (strcmp(g_cfg.rotors[i].by_path, c->by_path) == 0) {
            fprintf(stderr, "Device '%s' is already assigned to rotor '%s'.\n",
                    c->by_path, g_cfg.rotors[i].name);
            return;
        }
    }

    wprompt("Name (as used in N1MM)", "", c->name, sizeof(c->name));
    if (!c->name[0]) { printf("Cancelled.\n"); return; }

    /* Check name uniqueness */
    for (int i = 0; i < g_cfg.num_rotors; i++) {
        if (strcasecmp(g_cfg.rotors[i].name, c->name) == 0) {
            fprintf(stderr, "A rotor named '%s' already exists. Use --delete first.\n", c->name);
            return;
        }
    }

    printf("  1)  YAESU      (GS-232A/B auto-detect)\n");
    printf("  2)  PROSISTEL  (CBOX 2003)\n");
    char proto_buf[16];
    wprompt("Protocol [1-2]", "1", proto_buf, sizeof(proto_buf));
    if (strcmp(proto_buf, "2") == 0 || strcasecmp(proto_buf, "PROSISTEL") == 0)
        snprintf(c->protocol, sizeof(c->protocol), "PROSISTEL");
    else
        snprintf(c->protocol, sizeof(c->protocol), "YAESU");

    char def_baud[16] = "9600";
    wprompt("Baud rate", def_baud, buf, sizeof(buf));
    c->baud = atoi(buf);
    if (c->baud <= 0) c->baud = 9600;

    wprompt("Heading offset (-180..180)", "0", buf, sizeof(buf));
    int off = atoi(buf);
    if (off < -180) off = -180;
    if (off >  180) off =  180;
    c->offset = off;

    /* Verify device exists and capture serial for fallback */
    char devpath[512];
    by_path_to_dev(c->by_path, devpath, sizeof(devpath));
    if (access(devpath, F_OK) != 0) {
        printf("  WARNING: %s does not exist — OK if device is not plugged in yet.\n", devpath);
    } else {
        char resolved[256];
        resolve_symlink(devpath, resolved, sizeof(resolved));
        get_dev_serial(resolved, c->serial, sizeof(c->serial));
        printf("  Device: %s  serial: %s\n",
               resolved, c->serial[0] ? c->serial : "(none)");
    }

    g_cfg.num_rotors++;

    printf("\n");
    if (config_write(config_path) == 0)
        printf("Rotor '%s' added. Config saved to: %s\n\n", c->name, config_path);
    else
        printf("ERROR: could not save config.\n\n");
}

/* ─── --delete ───────────────────────────────────────────────────────────── */
static void cmd_delete(const char *config_path, const char *name)
{
    config_read(config_path);

    if (g_cfg.num_rotors == 0) {
        printf("No rotors configured.\n");
        return;
    }

    int found = -1;

    if (name && name[0]) {
        /* Name given on command line */
        for (int i = 0; i < g_cfg.num_rotors; i++)
            if (strcasecmp(g_cfg.rotors[i].name, name) == 0) { found = i; break; }
        if (found < 0) {
            fprintf(stderr, "Rotor '%s' not found in config.\n", name);
            return;
        }
    } else {
        /* Interactive selection */
        printf("\n=== Delete Rotor ===\n\n");
        printf("Configured rotors:\n");
        for (int i = 0; i < g_cfg.num_rotors; i++)
            printf("  %d)  %-20s  %s\n",
                   i + 1, g_cfg.rotors[i].name, g_cfg.rotors[i].by_path);
        printf("\n");

        char buf[16] = {0};
        char prompt[32];
        snprintf(prompt, sizeof(prompt), "Select rotor to delete [1-%d]", g_cfg.num_rotors);
        wprompt(prompt, "", buf, sizeof(buf));
        if (!buf[0]) { printf("Cancelled.\n"); return; }

        int sel = atoi(buf);
        if (sel < 1 || sel > g_cfg.num_rotors) {
            fprintf(stderr, "Invalid selection.\n");
            return;
        }
        found = sel - 1;
    }

    printf("Delete rotor '%s' (%s)? [y/N]: ",
           g_cfg.rotors[found].name, g_cfg.rotors[found].by_path);
    fflush(stdout);

    char buf[8] = {0};
    if (!fgets(buf, sizeof(buf), stdin)) return;
    if (buf[0] != 'y' && buf[0] != 'Y') { printf("Cancelled.\n"); return; }

    char deleted_name[64];
    snprintf(deleted_name, sizeof(deleted_name), "%s", g_cfg.rotors[found].name);

    for (int i = found; i < g_cfg.num_rotors - 1; i++)
        g_cfg.rotors[i] = g_cfg.rotors[i + 1];
    g_cfg.num_rotors--;

    if (config_write(config_path) == 0)
        printf("Rotor '%s' deleted. Config saved to: %s\n", deleted_name, config_path);
    else
        printf("ERROR: could not save config.\n");
}

/* ─── --change ───────────────────────────────────────────────────────────── */
static void cmd_change(const char *config_path)
{
    config_read(config_path);

    if (g_cfg.num_rotors == 0) {
        printf("No rotors configured.\n");
        return;
    }

    printf("\n=== Change Rotor ===\n\n");
    printf("Configured rotors:\n");
    for (int i = 0; i < g_cfg.num_rotors; i++)
        printf("  %d)  %-20s  %s\n",
               i + 1, g_cfg.rotors[i].name, g_cfg.rotors[i].by_path);
    printf("\n");

    char buf[256];
    char prompt[32];
    snprintf(prompt, sizeof(prompt), "Select rotor to change [1-%d]", g_cfg.num_rotors);
    wprompt(prompt, "", buf, sizeof(buf));
    if (!buf[0]) { printf("Cancelled.\n"); return; }

    int sel = atoi(buf);
    if (sel < 1 || sel > g_cfg.num_rotors) {
        fprintf(stderr, "Invalid selection.\n");
        return;
    }

    RotorCfg *c = &g_cfg.rotors[sel - 1];

    printf("\nEditing rotor '%s' — press Enter to keep current value.\n\n", c->name);

    /* Device selection */
    printf("Available devices:\n");
    int ndev = list_by_path_devices();
    printf("\n");

    if (ndev > 0) {
        char dev_prompt[48];
        snprintf(dev_prompt, sizeof(dev_prompt), "Select device [1-%d] or keep current", ndev);
        wprompt(dev_prompt, c->by_path, buf, sizeof(buf));
    } else {
        wprompt("Device (by-path filename)", c->by_path, buf, sizeof(buf));
    }
    if (!buf[0]) { printf("Cancelled.\n"); return; }

    char new_by_path[256];
    if (ndev > 0 && buf[0] >= '1' && buf[0] <= '9') {
        int dev_sel = atoi(buf);
        if (!get_by_path_entry(dev_sel, new_by_path, sizeof(new_by_path))) {
            fprintf(stderr, "Invalid selection.\n");
            return;
        }
    } else {
        snprintf(new_by_path, sizeof(new_by_path), "%s", buf);
    }

    /* Check if new by-path conflicts with another rotor (not this one) */
    for (int i = 0; i < g_cfg.num_rotors; i++) {
        if (i == sel - 1) continue;
        if (strcmp(g_cfg.rotors[i].by_path, new_by_path) == 0) {
            fprintf(stderr, "Device '%s' is already assigned to rotor '%s'.\n",
                    new_by_path, g_cfg.rotors[i].name);
            return;
        }
    }
    snprintf(c->by_path, sizeof(c->by_path), "%s", new_by_path);

    /* Name */
    char old_name[64];
    snprintf(old_name, sizeof(old_name), "%s", c->name);
    wprompt("Name (as used in N1MM)", c->name, buf, sizeof(buf));
    if (!buf[0]) { printf("Cancelled.\n"); return; }
    /* Check name uniqueness against other rotors */
    for (int i = 0; i < g_cfg.num_rotors; i++) {
        if (i == sel - 1) continue;
        if (strcasecmp(g_cfg.rotors[i].name, buf) == 0) {
            fprintf(stderr, "A rotor named '%s' already exists.\n", buf);
            return;
        }
    }
    snprintf(c->name, sizeof(c->name), "%.*s", (int)(sizeof(c->name) - 1), buf);

    /* Protocol */
    printf("  1)  YAESU      (GS-232A/B auto-detect)\n");
    printf("  2)  PROSISTEL  (CBOX 2003)\n");
    char cur_proto_num[4];
    snprintf(cur_proto_num, sizeof(cur_proto_num), "%s",
             (c->protocol[0] && strcasecmp(c->protocol, "PROSISTEL") == 0) ? "2" : "1");
    char proto_buf[16];
    wprompt("Protocol [1-2]", cur_proto_num, proto_buf, sizeof(proto_buf));
    if (strcmp(proto_buf, "2") == 0 || strcasecmp(proto_buf, "PROSISTEL") == 0)
        snprintf(c->protocol, sizeof(c->protocol), "PROSISTEL");
    else
        snprintf(c->protocol, sizeof(c->protocol), "YAESU");

    /* Baud */
    char def_baud[16];
    snprintf(def_baud, sizeof(def_baud), "%d", c->baud > 0 ? c->baud : 9600);
    wprompt("Baud rate", def_baud, buf, sizeof(buf));
    c->baud = atoi(buf);
    if (c->baud <= 0) c->baud = 9600;

    /* Offset */
    char def_off[16];
    snprintf(def_off, sizeof(def_off), "%d", c->offset);
    wprompt("Heading offset (-180..180)", def_off, buf, sizeof(buf));
    int off = atoi(buf);
    if (off < -180) off = -180;
    if (off >  180) off =  180;
    c->offset = off;

    /* Re-capture serial if device changed or not yet captured */
    char devpath[512];
    by_path_to_dev(c->by_path, devpath, sizeof(devpath));
    if (access(devpath, F_OK) != 0) {
        printf("  WARNING: %s does not exist — OK if device is not plugged in yet.\n", devpath);
    } else {
        char resolved[256];
        resolve_symlink(devpath, resolved, sizeof(resolved));
        get_dev_serial(resolved, c->serial, sizeof(c->serial));
        printf("  Device: %s  serial: %s\n",
               resolved, c->serial[0] ? c->serial : "(none)");
    }

    printf("\n");
    if (config_write(config_path) == 0)
        printf("Rotor '%s' updated. Config saved to: %s\n\n", c->name, config_path);
    else
        printf("ERROR: could not save config.\n\n");
}

/* ─── --list ─────────────────────────────────────────────────────────────── */
static void cmd_list(const char *config_path)
{
    if (config_read(config_path) < 0) return;

    printf("\n=== Configured Rotors (%s) ===\n\n", config_path);

    if (g_cfg.num_rotors == 0) {
        printf("  (none)\n\n");
        return;
    }

    printf("  %-3s  %-20s  %-9s  %-4s  %-6s  %-60s  %s\n", "#", "Name", "Protocol", "Baud", "Offset", "by-path", "Device");
    printf("  %-3s  %-20s  %-9s  %-4s  %-6s  %-60s  %s\n", "---", "--------------------",
           "---------", "----", "------", "------------------------------------------------------------", "------");

    for (int i = 0; i < g_cfg.num_rotors; i++) {
        RotorCfg *c = &g_cfg.rotors[i];
        char devpath[512];
        by_path_to_dev(c->by_path, devpath, sizeof(devpath));
        char resolved[256] = "not found";
        if (access(devpath, F_OK) == 0)
            resolve_symlink(devpath, resolved, sizeof(resolved));

        printf("  %-3d  %-20s  %-9s  %-4d  %-6d  %-60s  (%s)\n",
               i + 1, c->name,
               c->protocol[0] ? c->protocol : "YAESU",
               c->baud, c->offset, c->by_path, resolved);
    }
    printf("\n");

    printf("Server settings:\n");
    printf("  UDP command port : %d\n", g_cfg.cmd_port);
    printf("  UDP bcast port   : %d\n", g_cfg.bcast_port);
    if (g_cfg.bcast_port2 > 0)
        printf("  UDP bcast port 2 : %d\n", g_cfg.bcast_port2);
    else
        printf("  UDP bcast port 2 : off\n");
    printf("  Bcast address(es): %s\n", g_cfg.bcast_addr);
    printf("  Interval idle    : %d ms\n", g_cfg.idle_ms);
    printf("  Interval moving  : %d ms\n", g_cfg.moving_ms);
    printf("  Log file         : %s\n\n", LOG_FILE);
}

/* ─── Signal handler ─────────────────────────────────────────────────────── */
static void sig_handler(int sig) { (void)sig; g_running = 0; }

/* ═══════════════════════════════════════════════════════════════════════════
 * Wind monitor
 * Fetches / receives wind data, stores in g_wind, broadcasts every 60 s on
 * port 55555: "WIND @ direction_deg/bft/gust_ms/storm_active/last_fetch_unix"
 *
 * Sources:
 *   openmeteo — HTTP fetch via curl (api.open-meteo.com)
 *   yrno      — HTTP fetch via curl (api.met.no, requires User-Agent)
 *   owm       — HTTP fetch via curl (api.openweathermap.org, requires API key)
 *   wapi      — HTTP fetch via curl (api.weatherapi.com, requires API key)
 *   serial    — serial weather station (future)
 *   udp       — external UDP feed (future)
 * ═══════════════════════════════════════════════════════════════════════════ */

static int wind_ms_to_bft(double ms)
{
    static const double lim[] =
        { 0.3,1.6,3.4,5.5,8.0,10.8,13.9,17.2,20.8,24.5,28.5,32.7,1e9 };
    for (int i = 0; i <= 12; i++)
        if (ms < lim[i]) return i;
    return 12;
}

/* Helper: extract named JSON number; returns NaN if not found */
static double json_num(const char *json, const char *key, const char *fence)
{
    char needle[80];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p || (fence && p >= fence)) return (double)__builtin_nan("");
    p = strchr(p, ':');
    if (!p) return (double)__builtin_nan("");
    return atof(p + 1);
}
#define JNUM(json,key,fence) json_num((json),(key),(fence))
#define JISNAN(v)            (!((v)==(v)))   /* true when NaN */

/* Open-Meteo "current" block — wind + extended meteo */
static int wind_parse_json(const char *json,
                            double *dir, int *bft, double *gust_ms,
                            WindData *extra)
{
    const char *cur = strstr(json, "\"current\":");
    if (!cur) return -1;
    const char *lim = cur + 8000;
    double spd_kmh  = JNUM(cur, "wind_speed_10m",    lim);
    double gust_kmh = JNUM(cur, "wind_gusts_10m",    lim);
    *dir    = JNUM(cur, "wind_direction_10m",         lim);
    if (JISNAN(spd_kmh))  spd_kmh  = 0;
    if (JISNAN(gust_kmh)) gust_kmh = 0;
    if (JISNAN(*dir))     *dir      = 0;
    *bft     = wind_ms_to_bft(spd_kmh / 3.6);
    *gust_ms = gust_kmh / 3.6;
    if (extra) {
        extra->temp_c       = JNUM(cur, "temperature_2m",         lim);
        extra->humidity_pct = JNUM(cur, "relative_humidity_2m",   lim);
        extra->baro_hpa     = JNUM(cur, "pressure_msl",           lim);
        extra->precip_mm    = JNUM(cur, "rain",                   lim);
        extra->uv_index     = JNUM(cur, "uv_index",               lim);
        extra->solar_wm2    = JNUM(cur, "shortwave_radiation",    lim);
        extra->feels_like_c = JNUM(cur, "apparent_temperature",   lim);
    }
    return 0;
}

/* Yr.no (api.met.no) — instant.details + next_1_hours.details for precip */
static int wind_parse_json_yrno(const char *json,
                                double *dir, int *bft, double *gust_ms,
                                WindData *extra)
{
    const char *ts   = strstr(json, "\"timeseries\":");
    if (!ts) return -1;
    const char *inst = strstr(ts,   "\"instant\":");
    if (!inst) return -1;
    const char *det  = strstr(inst, "\"details\":");
    if (!det) return -1;
    const char *lim  = det + 4000;
    double spd_ms = JNUM(det, "wind_speed",             lim);
    double gust   = JNUM(det, "wind_speed_of_gust",     lim);
    *dir = JNUM(det, "wind_from_direction",              lim);
    if (JISNAN(spd_ms)) spd_ms = 0;
    if (JISNAN(gust))   gust   = 0;
    if (JISNAN(*dir))   *dir   = 0;
    *bft     = wind_ms_to_bft(spd_ms);
    *gust_ms = gust;
    if (extra) {
        extra->temp_c       = JNUM(det, "air_temperature",           lim);
        extra->humidity_pct = JNUM(det, "relative_humidity",         lim);
        extra->baro_hpa     = JNUM(det, "air_pressure_at_sea_level", lim);
        extra->uv_index     = JNUM(det, "ultraviolet_index_clear_sky",lim);
        /* precipitation is in next_1_hours.details */
        const char *nx = strstr(det, "\"next_1_hours\":");
        if (nx) {
            const char *nd = strstr(nx, "\"details\":");
            if (nd) extra->precip_mm = JNUM(nd, "precipitation_amount", nd + 500);
        }
    }
    return 0;
}

/* OpenWeatherMap — units=metric (speed/gust m/s, temp °C) */
static int wind_parse_json_owm(const char *json,
                               double *dir, int *bft, double *gust_ms,
                               WindData *extra)
{
    const char *w   = strstr(json, "\"wind\":");
    if (!w) return -1;
    const char *wlim = w + 500;
    double spd_ms = JNUM(w, "speed", wlim);
    double gust   = JNUM(w, "gust",  wlim);
    *dir = JNUM(w, "deg", wlim);
    if (JISNAN(spd_ms)) spd_ms = 0;
    if (JISNAN(gust))   gust   = 0;
    if (JISNAN(*dir))   *dir   = 0;
    *bft     = wind_ms_to_bft(spd_ms);
    *gust_ms = gust;
    if (extra) {
        const char *main = strstr(json, "\"main\":");
        const char *mlim = main ? main + 1000 : json + strlen(json);
        extra->temp_c       = main ? JNUM(main, "temp",       mlim) : (double)__builtin_nan("");
        extra->feels_like_c = main ? JNUM(main, "feels_like", mlim) : (double)__builtin_nan("");
        extra->baro_hpa     = main ? JNUM(main, "pressure",   mlim) : (double)__builtin_nan("");
        extra->humidity_pct = main ? JNUM(main, "humidity",   mlim) : (double)__builtin_nan("");
        const char *rain = strstr(json, "\"rain\":");
        extra->precip_mm = rain ? JNUM(rain, "1h", rain + 200) : (double)__builtin_nan("");
        extra->uv_index  = (double)__builtin_nan(""); /* needs separate OWM call */
    }
    return 0;
}

/* WeatherAPI (weatherapi.com) current.json — speed in km/h */
static int wind_parse_json_wapi(const char *json,
                                double *dir, int *bft, double *gust_ms,
                                WindData *extra)
{
    const char *cur = strstr(json, "\"current\":");
    if (!cur) return -1;
    const char *lim = cur + 4000;
    double spd_kmh  = JNUM(cur, "wind_kph",  lim);
    double gust_kmh = JNUM(cur, "gust_kph",  lim);
    *dir = JNUM(cur, "wind_degree", lim);
    if (JISNAN(spd_kmh))  spd_kmh  = 0;
    if (JISNAN(gust_kmh)) gust_kmh = 0;
    if (JISNAN(*dir))     *dir      = 0;
    *bft     = wind_ms_to_bft(spd_kmh / 3.6);
    *gust_ms = gust_kmh / 3.6;
    if (extra) {
        extra->temp_c       = JNUM(cur, "temp_c",      lim);
        extra->feels_like_c = JNUM(cur, "feelslike_c", lim);
        extra->baro_hpa     = JNUM(cur, "pressure_mb", lim);
        extra->humidity_pct = JNUM(cur, "humidity",    lim);
        extra->precip_mm    = JNUM(cur, "precip_mm",   lim);
        extra->uv_index     = JNUM(cur, "uv",          lim);
        extra->solar_wm2    = (double)__builtin_nan("");
    }
    return 0;
}

/* Parse a numeric field from URL-encoded form data or query string.
 * Finds "key=VALUE" and returns VALUE as double.
 * Returns 0 on success, -1 if field not found. */
static int wind_parse_http_field(const char *data, const char *key, double *out)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "%s=", key);
    const char *p = strstr(data, needle);
    if (!p) return -1;
    p += strlen(needle);
    *out = atof(p);
    return 0;
}

/* Extract a string field from URL-encoded form data (e.g. Ecowitt POST body).
 * Finds "key=VALUE&..." and copies VALUE (URL-decoded) to out[sz].
 * Decodes '+' → space and %XX hex escapes.
 * Returns 0 on success, -1 if field not found. */
static int http_str_field(const char *data, const char *key, char *out, size_t sz)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "%s=", key);
    const char *p = strstr(data, needle);
    if (!p) { out[0] = '\0'; return -1; }
    p += strlen(needle);
    size_t j = 0;
    while (*p && *p != '&' && j + 1 < sz) {
        if (*p == '+') {
            out[j++] = ' ';
            p++;
        } else if (*p == '%' &&
                   isxdigit((unsigned char)p[1]) &&
                   isxdigit((unsigned char)p[2])) {
            char hex[3] = { p[1], p[2], '\0' };
            out[j++] = (char)strtol(hex, NULL, 16);
            p += 3;
        } else {
            out[j++] = *p++;
        }
    }
    out[j] = '\0';
    return 0;
}

/* Strip non-alphanumeric chars from API key to prevent shell injection */
static void sanitize_apikey(const char *src, char *dst, size_t sz)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < sz; i++) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c == '-' || c == '_')
            dst[j++] = src[i];
    }
    dst[j] = '\0';
}

/* Sanitize a URL for use as a single-quoted shell argument.
 * Single quotes inside the URL are replaced with the shell sequence '\''
 * (close quote, literal apostrophe, reopen quote). */
static void sanitize_url(const char *src, char *dst, size_t sz)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < sz; i++) {
        if ((unsigned char)src[i] == '\'') {
            /* Requires 4 bytes: '  \  '  ' */
            if (j + 4 < sz) {
                dst[j++] = '\''; dst[j++] = '\\';
                dst[j++] = '\''; dst[j++] = '\'';
            }
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

static void *wind_thread(void *arg)
{
    (void)arg;

    /* UDP broadcast socket */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        logmsg("wind_thread: socket: %s", strerror(errno));
        return NULL;
    }
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    /* Build destination list from bcast_addr + WIND_BCAST_PORT */
    struct sockaddr_in dsts[8];
    int ndsts = 0;
    {
        char addr_buf[256];
        snprintf(addr_buf, sizeof(addr_buf), "%s", g_cfg.bcast_addr);
        char *tok = strtok(addr_buf, ",");
        while (tok && ndsts < 8) {
            while (*tok == ' ') tok++;
            char *end = tok + strlen(tok) - 1;
            while (end > tok && (*end == ' ' || *end == '\r' || *end == '\n'))
                *end-- = '\0';
            memset(&dsts[ndsts], 0, sizeof(dsts[ndsts]));
            dsts[ndsts].sin_family = AF_INET;
            dsts[ndsts].sin_port   = htons(WIND_BCAST_PORT);
            if (inet_pton(AF_INET, tok, &dsts[ndsts].sin_addr) == 1)
                ndsts++;
            tok = strtok(NULL, ",");
        }
    }
    /* Always include 255.255.255.255 so local GUI receives it */
    {
        int has_bcast = 0;
        for (int i = 0; i < ndsts; i++)
            if (dsts[i].sin_addr.s_addr == htonl(INADDR_BROADCAST))
                { has_bcast = 1; break; }
        if (!has_bcast && ndsts < 8) {
            memset(&dsts[ndsts], 0, sizeof(dsts[ndsts]));
            dsts[ndsts].sin_family      = AF_INET;
            dsts[ndsts].sin_addr.s_addr = htonl(INADDR_BROADCAST);
            dsts[ndsts].sin_port        = htons(WIND_BCAST_PORT);
            ndsts++;
        }
    }
    if (ndsts == 0) {
        memset(&dsts[0], 0, sizeof(dsts[0]));
        dsts[0].sin_family      = AF_INET;
        dsts[0].sin_addr.s_addr = htonl(INADDR_BROADCAST);
        dsts[0].sin_port        = htons(WIND_BCAST_PORT);
        ndsts = 1;
    }

    WindSource wsrc = WIND_SRC_OPENMETEO;
    if      (strcmp(g_cfg.wind_source, "yrno")        == 0) wsrc = WIND_SRC_YRNO;
    else if (strcmp(g_cfg.wind_source, "owm")         == 0) wsrc = WIND_SRC_OWM;
    else if (strcmp(g_cfg.wind_source, "wapi")        == 0) wsrc = WIND_SRC_WAPI;
    else if (strcmp(g_cfg.wind_source, "serial")      == 0) wsrc = WIND_SRC_SERIAL;
    else if (strcmp(g_cfg.wind_source, "udp")         == 0) wsrc = WIND_SRC_UDP;
    else if (strcmp(g_cfg.wind_source, "ecowitt")     == 0) wsrc = WIND_SRC_ECOWITT;

    logmsg("Wind monitor: source=%s lat=%.4f lon=%.4f  interval=%d min  port=%d",
           g_cfg.wind_source, g_cfg.wind_lat, g_cfg.wind_lon,
           g_cfg.wind_interval_min, WIND_BCAST_PORT);

    /* Resolve fallback source for Ecowitt (if configured) */
    WindSource fallback_wsrc = WIND_SRC_OPENMETEO; /* default fallback */
    int has_fallback = (wsrc == WIND_SRC_ECOWITT && g_cfg.wind_ecowitt_fallback[0] != '\0');
    if (has_fallback) {
        if      (strcmp(g_cfg.wind_ecowitt_fallback, "yrno") == 0) fallback_wsrc = WIND_SRC_YRNO;
        else if (strcmp(g_cfg.wind_ecowitt_fallback, "owm")  == 0) fallback_wsrc = WIND_SRC_OWM;
        else if (strcmp(g_cfg.wind_ecowitt_fallback, "wapi") == 0) fallback_wsrc = WIND_SRC_WAPI;
        else                                                         fallback_wsrc = WIND_SRC_OPENMETEO;
        logmsg("Wind Ecowitt: fallback configured → %s (timeout %d min)",
               g_cfg.wind_ecowitt_fallback, g_cfg.wind_ecowitt_fallback_min);
    }
    int fallback_active = 0; /* local shadow of g_wind_fallback_active */
    long long next_fallback_fetch = 0; /* fetch fallback immediately if/when activated */

    /* Warn early if curl is missing — avoids a cryptic "parse failed" later */
    if (wsrc == WIND_SRC_OPENMETEO || wsrc == WIND_SRC_YRNO ||
        wsrc == WIND_SRC_OWM       || wsrc == WIND_SRC_WAPI || has_fallback) {
        if (access("/usr/bin/curl", X_OK) != 0 &&
            access("/usr/local/bin/curl", X_OK) != 0)
            logmsg("Wind: WARNING — 'curl' not found. Install with: sudo apt install curl");
    }

    long long interval_ms  = (long long)g_cfg.wind_interval_min * 60000LL;
    long long next_fetch   = 0;          /* fetch immediately on startup */
    long long next_rebcast = LLONG_MAX;  /* set after first successful fetch */
#define WIND_REBCAST_MS   1000LL         /* re-broadcast last known data every 1 s  */

    /* Last known wind data — re-broadcast while waiting for next fetch */
    double    last_dir        = 0.0;
    int       last_bft        = 0;    /* 0 = wind monitor active, no data yet */
    double    last_gust       = 0.0;
    time_t    last_fetch_time = 0;    /* Unix time of last successful data fetch */
    time_t    wind_thread_start = time(NULL); /* for fallback timeout from server start */

    /* HTTP server socket for Ecowitt push source */
    int http_srv_fd = -1;
    if (wsrc == WIND_SRC_ECOWITT) {
        int hport = g_cfg.wind_ecowitt_port;
        http_srv_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (http_srv_fd >= 0) {
            int opt = 1;
            setsockopt(http_srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            struct sockaddr_in hsa;
            memset(&hsa, 0, sizeof(hsa));
            hsa.sin_family      = AF_INET;
            hsa.sin_addr.s_addr = INADDR_ANY;
            hsa.sin_port        = htons((uint16_t)hport);
            if (bind(http_srv_fd, (struct sockaddr *)&hsa, sizeof(hsa)) < 0) {
                logmsg("Wind Ecowitt: bind port %d failed: %s",
                       hport, strerror(errno));
                close(http_srv_fd);
                http_srv_fd = -1;
            } else {
                listen(http_srv_fd, 5);
                logmsg("Wind Ecowitt: HTTP server listening on port %d", hport);
            }
        }
    }

    while (g_running) {
        long long t = now_ms();

        /* ── Ecowitt fallback state machine ──────────────────────────────── */
        if (has_fallback) {
            time_t now_t = time(NULL);
            long   timeout_sec = (long)g_cfg.wind_ecowitt_fallback_min * 60;
            if (!fallback_active) {
                /* Reference point: last push time, or server start if no push yet */
                time_t ref = (g_last_ecowitt_rx > 0) ? g_last_ecowitt_rx : wind_thread_start;
                /* Diagnostic: log state every 60 s while waiting for fallback */
                {
                    static time_t last_diag = 0;
                    if (now_t - last_diag >= 60) {
                        last_diag = now_t;
                        logmsg("Wind Ecowitt fallback: elapsed=%lds timeout=%lds "
                               "last_rx=%s active=0",
                               (long)(now_t - ref), timeout_sec,
                               g_last_ecowitt_rx > 0 ? "yes" : "never");
                    }
                }
                if ((now_t - ref) > timeout_sec) {
                    fallback_active = 1;
                    g_wind_fallback_active = 1;
                    next_fallback_fetch = 0; /* fetch fallback immediately */
                    logmsg("Wind Ecowitt: no data for %ld min — activating fallback (%s)",
                           (now_t - ref) / 60,
                           g_cfg.wind_ecowitt_fallback);
                    gui_send_all("ECOWITT_FALLBACK @ 1\n");
                }
            } else {
                /* Return to primary if Ecowitt push was received recently */
                if (g_last_ecowitt_rx > 0 &&
                    (now_t - g_last_ecowitt_rx) <= timeout_sec) {
                    fallback_active = 0;
                    g_wind_fallback_active = 0;
                    logmsg("Wind Ecowitt: data resumed — deactivating fallback");
                    gui_send_all("ECOWITT_FALLBACK @ 0\n");
                }
            }
        }

        /* ── Fallback internet fetch (only when fallback is active) ─────── */
        if (fallback_active && t >= next_fallback_fetch) {
            char url[640], curlcmd[760];
            if (fallback_wsrc == WIND_SRC_YRNO) {
                snprintf(url, sizeof(url),
                         "https://api.met.no/weatherapi/locationforecast/2.0/compact"
                         "?lat=%.4f&lon=%.4f",
                         g_cfg.wind_lat, g_cfg.wind_lon);
                snprintf(curlcmd, sizeof(curlcmd),
                         "curl -s --max-time 10"
                         " -H 'User-Agent: N1MMRotorServer/" VERSION " https://pd5dj.nl'"
                         " '%s' 2>/dev/null", url);
            } else if (fallback_wsrc == WIND_SRC_OWM) {
                char safe_key[64];
                sanitize_apikey(g_cfg.wind_owm_apikey, safe_key, sizeof(safe_key));
                snprintf(url, sizeof(url),
                         "https://api.openweathermap.org/data/2.5/weather"
                         "?lat=%.4f&lon=%.4f&appid=%s&units=metric",
                         g_cfg.wind_lat, g_cfg.wind_lon, safe_key);
                snprintf(curlcmd, sizeof(curlcmd),
                         "curl -s --max-time 10 '%s' 2>/dev/null", url);
            } else if (fallback_wsrc == WIND_SRC_WAPI) {
                char safe_key[64];
                sanitize_apikey(g_cfg.wind_wapi_apikey, safe_key, sizeof(safe_key));
                snprintf(url, sizeof(url),
                         "https://api.weatherapi.com/v1/current.json"
                         "?key=%s&q=%.4f,%.4f",
                         safe_key, g_cfg.wind_lat, g_cfg.wind_lon);
                snprintf(curlcmd, sizeof(curlcmd),
                         "curl -s --max-time 10 '%s' 2>/dev/null", url);
            } else { /* WIND_SRC_OPENMETEO */
                snprintf(url, sizeof(url),
                         "https://api.open-meteo.com/v1/forecast"
                         "?latitude=%.6f&longitude=%.6f"
                         "&current=wind_speed_10m,wind_direction_10m,wind_gusts_10m"
                         "&wind_speed_unit=kmh",
                         g_cfg.wind_lat, g_cfg.wind_lon);
                snprintf(curlcmd, sizeof(curlcmd),
                         "curl -s --max-time 10 '%s' 2>/dev/null", url);
            }
            logmsg("Wind fallback: fetching %s (lat=%.4f lon=%.4f)",
                   g_cfg.wind_ecowitt_fallback, g_cfg.wind_lat, g_cfg.wind_lon);
            FILE *fp = popen(curlcmd, "r");
            if (fp) {
                char json[4096] = "";
                size_t nr = fread(json, 1, sizeof(json) - 1, fp);
                pclose(fp);
                json[nr] = '\0';
                if (nr > 0) {
                    double dir = 0, gust_ms = 0; int bft = 0;
                    WindData fextra; wind_data_init(&fextra);
                    int ok = 0;
                    if      (fallback_wsrc == WIND_SRC_YRNO) ok = wind_parse_json_yrno(json, &dir, &bft, &gust_ms, &fextra);
                    else if (fallback_wsrc == WIND_SRC_OWM)  ok = wind_parse_json_owm (json, &dir, &bft, &gust_ms, &fextra);
                    else if (fallback_wsrc == WIND_SRC_WAPI) ok = wind_parse_json_wapi(json, &dir, &bft, &gust_ms, &fextra);
                    else                                      ok = wind_parse_json     (json, &dir, &bft, &gust_ms, &fextra);
                    if (ok == 0) {  /* 0 = success, -1 = failure */
                        last_dir        = dir;
                        last_bft        = bft;
                        last_gust       = gust_ms;
                        last_fetch_time = time(NULL);
                        pthread_mutex_lock(&g_wind_lock);
                        g_wind.direction_deg = dir;
                        wind_dir_push(dir);
                        g_wind.force_bft     = bft;
                        g_wind.gust_ms       = gust_ms;
                        g_wind.has_data      = 1;
                        g_wind.temp_c        = fextra.temp_c;
                        g_wind.humidity_pct  = fextra.humidity_pct;
                        g_wind.baro_hpa      = fextra.baro_hpa;
                        g_wind.precip_mm     = fextra.precip_mm;
                        g_wind.uv_index      = fextra.uv_index;
                        g_wind.solar_wm2     = fextra.solar_wm2;
                        g_wind.feels_like_c  = fextra.feels_like_c;
                        pthread_mutex_unlock(&g_wind_lock);
                        next_rebcast = 0;
                        logmsg("Wind fallback (%s): dir=%.0f\302\260 bft=%d gust=%.1f m/s",
                               g_cfg.wind_ecowitt_fallback, dir, bft, gust_ms);
                    } else {
                        logmsg("Wind fallback: JSON parse failed. Response: %.300s", json);
                    }
                } else {
                    logmsg("Wind fallback: curl returned no data (network OK? curl installed?)");
                }
            } else {
                logmsg("Wind fallback: popen(curl) failed: %s", strerror(errno));
            }
            next_fallback_fetch = t + interval_ms;
        }

        /* ── Fetch new data ───────────────────────────────────────────────── */
        if (t >= next_fetch) {
            if (wsrc == WIND_SRC_OPENMETEO || wsrc == WIND_SRC_YRNO ||
                wsrc == WIND_SRC_OWM       || wsrc == WIND_SRC_WAPI) {
                char url[640];
                char curlcmd[760];
                if (wsrc == WIND_SRC_YRNO) {
                    snprintf(url, sizeof(url),
                             "https://api.met.no/weatherapi/locationforecast/2.0/compact"
                             "?lat=%.4f&lon=%.4f",
                             g_cfg.wind_lat, g_cfg.wind_lon);
                    /* User-Agent is mandatory per api.met.no terms of service */
                    snprintf(curlcmd, sizeof(curlcmd),
                             "curl -s --max-time 10"
                             " -H 'User-Agent: N1MMRotorServer/2.5 https://github.com/pd5dj'"
                             " '%s' 2>/dev/null", url);
                } else if (wsrc == WIND_SRC_OWM) {
                    char safe_key[64];
                    sanitize_apikey(g_cfg.wind_owm_apikey, safe_key, sizeof(safe_key));
                    snprintf(url, sizeof(url),
                             "https://api.openweathermap.org/data/2.5/weather"
                             "?lat=%.4f&lon=%.4f&appid=%s&units=metric",
                             g_cfg.wind_lat, g_cfg.wind_lon, safe_key);
                    snprintf(curlcmd, sizeof(curlcmd),
                             "curl -s --max-time 10 '%s' 2>/dev/null", url);
                } else if (wsrc == WIND_SRC_WAPI) {
                    char safe_key[64];
                    sanitize_apikey(g_cfg.wind_wapi_apikey, safe_key, sizeof(safe_key));
                    snprintf(url, sizeof(url),
                             "https://api.weatherapi.com/v1/current.json"
                             "?key=%s&q=%.4f,%.4f",
                             safe_key, g_cfg.wind_lat, g_cfg.wind_lon);
                    snprintf(curlcmd, sizeof(curlcmd),
                             "curl -s --max-time 10 '%s' 2>/dev/null", url);
                } else {
                    snprintf(url, sizeof(url),
                             "https://api.open-meteo.com/v1/forecast"
                             "?latitude=%.6f&longitude=%.6f"
                             "&current=wind_speed_10m,wind_direction_10m,wind_gusts_10m"
                             ",temperature_2m,relative_humidity_2m,pressure_msl"
                             ",rain,uv_index,shortwave_radiation,apparent_temperature"
                             "&wind_speed_unit=kmh",
                             g_cfg.wind_lat, g_cfg.wind_lon);
                    snprintf(curlcmd, sizeof(curlcmd),
                             "curl -s --max-time 10 '%s' 2>/dev/null", url);
                }

                FILE *fp = popen(curlcmd, "r");
                if (fp) {
                    char json[8192];
                    size_t len = fread(json, 1, sizeof(json) - 1, fp);
                    pclose(fp);
                    json[len] = '\0';

                    if (len == 0) {
                        logmsg("Wind: curl returned no data (curl installed? network OK? check: which curl)");
                    } else {
                        double dir; int bft; double gust_ms;
                        WindData extra; wind_data_init(&extra);
                        int ok;
                        if      (wsrc == WIND_SRC_YRNO) ok = wind_parse_json_yrno(json, &dir, &bft, &gust_ms, &extra);
                        else if (wsrc == WIND_SRC_OWM)  ok = wind_parse_json_owm (json, &dir, &bft, &gust_ms, &extra);
                        else if (wsrc == WIND_SRC_WAPI) ok = wind_parse_json_wapi(json, &dir, &bft, &gust_ms, &extra);
                        else                            ok = wind_parse_json     (json, &dir, &bft, &gust_ms, &extra);
                        if (ok == 0) {
                            last_dir        = dir;
                            last_bft        = bft;
                            last_gust       = gust_ms;
                            last_fetch_time = time(NULL);
                            pthread_mutex_lock(&g_wind_lock);
                            g_wind.direction_deg = dir;
                            wind_dir_push(dir);
                            g_wind.force_bft     = bft;
                            g_wind.gust_ms       = gust_ms;
                            g_wind.has_data      = 1;
                            g_wind.temp_c        = extra.temp_c;
                            g_wind.humidity_pct  = extra.humidity_pct;
                            g_wind.baro_hpa      = extra.baro_hpa;
                            g_wind.precip_mm     = extra.precip_mm;
                            g_wind.uv_index      = extra.uv_index;
                            g_wind.solar_wm2     = extra.solar_wm2;
                            g_wind.feels_like_c  = extra.feels_like_c;
                            pthread_mutex_unlock(&g_wind_lock);
                            next_rebcast = 0; /* broadcast immediately after fetch */
                            logmsg("Wind: dir=%.0f\302\260 bft=%d gust=%.1f m/s temp=%.1f\302\260C — fetched",
                                   dir, bft, gust_ms,
                                   JISNAN(extra.temp_c) ? -999.0 : extra.temp_c);
                        } else {
                            logmsg("Wind: JSON parse failed (unexpected response format)");
                        }
                    }
                } else {
                    logmsg("Wind: popen(curl) failed: %s", strerror(errno));
                }
            }

            /* No data yet (network not up at boot?) — retry sooner */
            next_fetch = now_ms() + (last_fetch_time == 0 ? 10000LL : interval_ms);
        }

        /* ── Immediate broadcast requested (WIND? or storm state change) ─── */
        if (g_wind_bcast_now) {
            g_wind_bcast_now = 0;
            if (last_bft >= 0) next_rebcast = 0; /* trigger broadcast if data available */
        }

        /* ── Re-broadcast last known data (interval = WIND_REBCAST_MS) ─────── */
        if (last_bft >= 0 && t >= next_rebcast) {
            /* Core packet (backward compatible with GUI/N1MM) */
            char pkt[160];
            snprintf(pkt, sizeof(pkt), "WIND @ %.1f/%d/%.1f/%d/%lld/%d/%d/%d",
                     last_dir, last_bft, last_gust, g_storm_active,
                     (long long)last_fetch_time, g_storm_correcting,
                     g_storm_timer_secs, g_wind_fallback_active);
            for (int d = 0; d < ndsts; d++)
                sendto(sock, pkt, strlen(pkt), 0,
                       (struct sockaddr *)&dsts[d], sizeof(dsts[d]));

            /* Push to connected GUI/web clients over Unix socket.
             * Extended packet appends extra meteo fields as key=value pairs.
             * NaN fields are omitted so old clients are unaffected. */
            {
                char gpkt[512];
                int gl = snprintf(gpkt, sizeof(gpkt), "%s\n", pkt);
                /* Append extra meteo after the WIND line as "METEO @ key=val..." */
                pthread_mutex_lock(&g_wind_lock);
                WindData wd = g_wind;
                pthread_mutex_unlock(&g_wind_lock);
                char mpkt[256]; int ml = 0;
                ml += snprintf(mpkt+ml, sizeof(mpkt)-ml, "METEO @");
                if (!JISNAN(wd.temp_c))       ml += snprintf(mpkt+ml,sizeof(mpkt)-ml," temp=%.1f",       wd.temp_c);
                if (!JISNAN(wd.feels_like_c)) ml += snprintf(mpkt+ml,sizeof(mpkt)-ml," feels=%.1f",      wd.feels_like_c);
                if (!JISNAN(wd.humidity_pct)) ml += snprintf(mpkt+ml,sizeof(mpkt)-ml," hum=%.0f",        wd.humidity_pct);
                if (!JISNAN(wd.baro_hpa))     ml += snprintf(mpkt+ml,sizeof(mpkt)-ml," baro=%.1f",       wd.baro_hpa);
                if (!JISNAN(wd.precip_mm))    ml += snprintf(mpkt+ml,sizeof(mpkt)-ml," precip=%.1f",     wd.precip_mm);
                if (!JISNAN(wd.uv_index))     ml += snprintf(mpkt+ml,sizeof(mpkt)-ml," uv=%.0f",         wd.uv_index);
                if (!JISNAN(wd.solar_wm2))    ml += snprintf(mpkt+ml,sizeof(mpkt)-ml," solar=%.0f",      wd.solar_wm2);
                if (ml > 7 && gl > 0 && gl+(int)strlen(mpkt)+2 < (int)sizeof(gpkt)) {
                    gl += snprintf(gpkt+gl, sizeof(gpkt)-gl, "%s\n", mpkt);
                }
                if (gl > 0 && gl < (int)sizeof(gpkt))
                    gui_send_all(gpkt);
            }

            logdbg("Wind: broadcast dir=%.0f\302\260 bft=%d gust=%.1f m/s storm=%d",
                   last_dir, last_bft, last_gust, g_storm_active);
            next_rebcast = now_ms() + WIND_REBCAST_MS;
        }

        /* ── Sleep / HTTP poll ───────────────────────────────────────────────── */
        if (http_srv_fd >= 0) {
            /* For HTTP push sources: use select() so we wake up on new data
             * while still honouring the 1-second re-broadcast interval. */
            long long sleep_ms = WIND_REBCAST_MS;
            long long tr = now_ms();
            if (next_rebcast != LLONG_MAX && next_rebcast > tr)
                sleep_ms = next_rebcast - tr;
            if (sleep_ms < 10)   sleep_ms = 10;
            if (sleep_ms > 1000) sleep_ms = 1000;
            struct timeval htv;
            htv.tv_sec  = sleep_ms / 1000;
            htv.tv_usec = (sleep_ms % 1000) * 1000;
            fd_set hrfds;
            FD_ZERO(&hrfds);
            FD_SET(http_srv_fd, &hrfds);
            if (select(http_srv_fd + 1, &hrfds, NULL, NULL, &htv) > 0) {
                int cli = accept(http_srv_fd, NULL, NULL);
                if (cli >= 0) {
                    struct timeval rto; rto.tv_sec = 2; rto.tv_usec = 0;
                    setsockopt(cli, SOL_SOCKET, SO_RCVTIMEO, &rto, sizeof(rto));

                    char req[4096];
                    memset(req, 0, sizeof(req));
                    int   nr          = (int)recv(cli, req, sizeof(req) - 1, 0);
                    char *eco_body    = NULL; /* Ecowitt POST body ptr — for forwarding */
                    int   passkey_ok  = 1;   /* 0 = PASSKEY rejected                  */
                    if (nr > 0) {
                        double h_dir = 0, h_speed = 0, h_gust = 0;
                        int    h_ok  = 0;

                        if (wsrc == WIND_SRC_ECOWITT) {
                            /* POST body is after the blank line \r\n\r\n */
                            char *body = strstr(req, "\r\n\r\n");
                            if (body) {
                                body += 4;
                                eco_body = body; /* keep reference for forwarding */

                                /* Extract PASSKEY — store for GUI display regardless of validation */
                                {
                                    char pk[64] = "";
                                    if (http_str_field(body, "PASSKEY", pk, sizeof(pk)) == 0 && pk[0]) {
                                        if (strcmp(pk, g_ecowitt_last_pk) != 0) {
                                            snprintf(g_ecowitt_last_pk, sizeof(g_ecowitt_last_pk), "%s", pk);
                                            char pkmsg[96];
                                            snprintf(pkmsg, sizeof(pkmsg), "ECOWITT_PK @ %s\n", pk);
                                            gui_send_all(pkmsg);
                                            logmsg("Wind Ecowitt: station PASSKEY stored: %s", pk);
                                        }
                                    } else {
                                        logmsg("Wind Ecowitt: no PASSKEY field found in POST body");
                                    }
                                    /* Validate if a local passkey is configured */
                                    if (g_cfg.wind_ecowitt_passkey[0]) {
                                        if (!pk[0] || strcmp(pk, g_cfg.wind_ecowitt_passkey) != 0) {
                                            passkey_ok = 0;
                                            logmsg("Wind Ecowitt: PASSKEY mismatch — "
                                                   "request ignored");
                                        }
                                    }
                                }

                                if (passkey_ok) {
                                    /* Ecowitt always sends windspeedmph / windgustmph
                                     * regardless of the display unit setting in the app */
                                    double spd_mph = 0, gust_mph = 0;
                                    if (wind_parse_http_field(body, "winddir",      &h_dir)   == 0 &&
                                        wind_parse_http_field(body, "windspeedmph", &spd_mph) == 0) {
                                        wind_parse_http_field(body, "windgustmph", &gust_mph);
                                        h_speed = spd_mph  * 0.44704; /* mph → m/s */
                                        h_gust  = gust_mph * 0.44704;
                                        h_ok = 1;
                                    } else {
                                        logmsg("Wind Ecowitt: POST received — "
                                               "winddir/windspeedmph not found. "
                                               "Full body: %s", body);
                                    }
                                }
                            } else {
                                logmsg("Wind Ecowitt: POST received — no HTTP body");
                            }
                        }
                        if (h_ok) {
                            /* Apply wind force correction (100% = no change) */
                            if (g_cfg.wind_ecowitt_correction != 100 && g_cfg.wind_ecowitt_correction > 0) {
                                double factor = g_cfg.wind_ecowitt_correction / 100.0;
                                h_speed *= factor;
                                h_gust  *= factor;
                            }
                            int bft = wind_ms_to_bft(h_speed);
                            last_dir        = h_dir;
                            last_bft        = bft;
                            last_gust       = h_gust;
                            last_fetch_time = time(NULL);
                            pthread_mutex_lock(&g_wind_lock);
                            g_wind.direction_deg = h_dir;
                            wind_dir_push(h_dir);
                            g_wind.force_bft     = bft;
                            g_wind.gust_ms       = h_gust;
                            g_wind.has_data      = 1;
                            /* Extended Ecowitt fields (all in URL-encoded body) */
                            {
                                double v;
                                char *b = eco_body;
                                double tempf = (wind_parse_http_field(b,"tempf",&v)==0) ? v : (double)__builtin_nan("");
                                g_wind.temp_c       = JISNAN(tempf) ? (double)__builtin_nan("") : (tempf-32.0)*5.0/9.0;
                                g_wind.feels_like_c = (wind_parse_http_field(b,"feelslikef",&v)==0) ? (v-32.0)*5.0/9.0 : (double)__builtin_nan("");
                                g_wind.humidity_pct = (wind_parse_http_field(b,"humidity",&v)==0)   ? v : (double)__builtin_nan("");
                                g_wind.baro_hpa     = (wind_parse_http_field(b,"baromrelin",&v)==0) ? v*33.8639 : (double)__builtin_nan("");
                                double rr = (wind_parse_http_field(b,"rainratein",&v)==0) ? v*25.4 : (double)__builtin_nan("");
                                g_wind.precip_mm    = rr;
                                g_wind.uv_index     = (wind_parse_http_field(b,"uv",&v)==0)         ? v : (double)__builtin_nan("");
                                g_wind.solar_wm2    = (wind_parse_http_field(b,"solarradiation",&v)==0) ? v : (double)__builtin_nan("");
                            }
                            pthread_mutex_unlock(&g_wind_lock);
                            g_last_ecowitt_rx = time(NULL);
                            next_rebcast = 0;
                            logmsg("Wind: dir=%.0f\302\260 bft=%d gust=%.1f m/s — Ecowitt push",
                                   h_dir, bft, h_gust);
                        }
                    }
                    /* Send minimal HTTP 200 response, then close client socket */
                    const char *hresp = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
                    send(cli, hresp, strlen(hresp), MSG_NOSIGNAL);
                    close(cli);

                    /* ── Ecowitt forwarding (after client ACK'd) ──────────────
                     * Forward the original POST body unchanged.
                     * eco_body still points into req[] which is still in scope. */
                    {
                        int do_fwd;
                        do_fwd = (wsrc == WIND_SRC_ECOWITT) && (eco_body != NULL);
                        if (do_fwd) do_fwd = passkey_ok;
                        if (do_fwd) do_fwd = (g_cfg.wind_ecowitt_fwd_host[0] != '\0' &&
                                               g_cfg.wind_ecowitt_fwd_port > 0);
                        if (do_fwd) {
                            const char *fwd_body = eco_body;
                            const char *fwd_path = (g_cfg.wind_ecowitt_fwd_path[0] == '/')
                                                   ? g_cfg.wind_ecowitt_fwd_path : "/";
                            /* strip any protocol prefix the user may have typed */
                            const char *fwd_host = g_cfg.wind_ecowitt_fwd_host;
                            if (strncmp(fwd_host, "https://", 8) == 0) fwd_host += 8;
                            else if (strncmp(fwd_host, "http://",  7) == 0) fwd_host += 7;
                            const char *fwd_scheme =
                                (g_cfg.wind_ecowitt_fwd_port == 443) ? "https" : "http";
                            char url[512];
                            snprintf(url, sizeof(url), "%s://%s:%d%s",
                                     fwd_scheme, fwd_host,
                                     g_cfg.wind_ecowitt_fwd_port,
                                     fwd_path);
                            char safe_url[512];
                            sanitize_url(url, safe_url, sizeof(safe_url));
                            char fwdcmd[768];
                            snprintf(fwdcmd, sizeof(fwdcmd),
                                     "curl -s -X POST"
                                     " -H 'Content-Type:"
                                     " application/x-www-form-urlencoded'"
                                     " --connect-timeout 3 --max-time 5"
                                     " --data-binary @-"
                                     " '%s' >/dev/null 2>&1",
                                     safe_url);
                            FILE *ffp = popen(fwdcmd, "w");
                            if (ffp) {
                                fputs(fwd_body, ffp);
                                pclose(ffp);
                                logmsg("Wind Ecowitt: forwarded to %s", url);
                            } else {
                                logmsg("Wind Ecowitt: forward failed (popen): %s",
                                       strerror(errno));
                            }
                        }
                    }
                }
            }
        } else {
            usleep(1000000); /* 1 s tick — keeps shutdown responsive */
        }
    }

    if (http_srv_fd >= 0) close(http_srv_fd);
    close(sock);
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Storm monitor
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Drive all storm-enabled rotors to safe position relative to current wind */
static void storm_apply_goto(void)
{
    pthread_mutex_lock(&g_wind_lock);
    double dir_instant = g_wind.direction_deg;   /* raw, for logging only */
    double dir         = wind_dir_smooth();       /* circular mean — used for GOTO */
    int    has         = g_wind.has_data;
    int    n_smooth    = g_wind_dir_count;
    pthread_mutex_unlock(&g_wind_lock);

    if (!has || dir_instant < 0.5) {
        logmsg("Storm: no wind direction data, skipping GOTO");
        return;
    }

    for (int i = 0; i < g_num_rotors; i++) {
        Rotor *r = &g_rotors[i];
        if (!r->cfg.storm_enabled) continue;

        double primary  = fmod(dir + r->cfg.storm_offset + 360.0, 360.0);
        double fallback = fmod(dir + 180.0 + r->cfg.storm_offset + 360.0, 360.0);

        pthread_mutex_lock(&r->lock);
        double cur_az = r->current_az;
        pthread_mutex_unlock(&r->lock);

        double target = (fabs(cur_az - primary) > 180.0) ? fallback : primary;
        if (target < 0.5) target = 360.0;  /* 0° = no data; N = 360 */

        pthread_mutex_lock(&r->lock);
        r->storm_active = 1;
        r->target_az    = target;
        r->moving       = 1;
        r->stall_ref_az = r->current_az;
        r->stall_ref_ms = now_ms();
        if (r->cfg.simulate)
            r->sim_dist = fabs(target - r->current_az);
        rotor_goto_az(r, target);
        pthread_mutex_unlock(&r->lock);
        logmsg("Storm GOTO rotor '%s' → %.1f° (wind_smooth=%.0f° [n=%d] instant=%.0f° offset=%d°)",
               r->cfg.name, target, dir, n_smooth, dir_instant, r->cfg.storm_offset);
    }
}

static void *storm_thread(void *arg)
{
    (void)arg;

    long long sustain_ms  = (long long)g_cfg.storm_sustain_min  * 60000LL;
    long long release_ms  = (long long)g_cfg.storm_release_min  * 60000LL;
    long long interval_ms = (long long)g_cfg.storm_interval_min * 60000LL;

    long long above_since  = 0;
    long long below_since  = 0;
    long long next_goto_ms = 0;

    logmsg("Storm thread started (threshold=%d Bft, sustain=%d min, "
           "release=%d min, interval=%d min)",
           g_cfg.storm_threshold_bft, g_cfg.storm_sustain_min,
           g_cfg.storm_release_min,   g_cfg.storm_interval_min);

    while (g_running) {
        usleep(5000000); /* 5 s tick */
        if (!g_running) break;

        long long now = now_ms();

        /* ── Feature off (button = green): reset timers, ensure no correction ── */
        if (!g_storm_active) {
            above_since = 0; below_since = 0; next_goto_ms = 0;
            g_storm_timer_secs = 0;
            for (int i = 0; i < g_num_rotors; i++) {
                if (g_rotors[i].storm_active) {
                    pthread_mutex_lock(&g_rotors[i].lock);
                    g_rotors[i].storm_active = 0;
                    pthread_mutex_unlock(&g_rotors[i].lock);
                }
            }
            continue;
        }

        /* ── Feature on (button = red): run threshold / sustain logic ─────── */
        pthread_mutex_lock(&g_wind_lock);
        int    bft = g_wind.force_bft;
        double dir = g_wind.direction_deg;
        int    has = g_wind.has_data;
        pthread_mutex_unlock(&g_wind_lock);

        if (!has || dir < 0.5) {
            /* No wind data — keep waiting, don't reset timers */
            g_storm_timer_secs = 0;
            continue;
        }

        /* Correction active = any storm-enabled rotor currently being driven */
        int correction_active = 0;
        for (int i = 0; i < g_num_rotors; i++)
            if (g_rotors[i].storm_active) { correction_active = 1; break; }

        if (bft >= g_cfg.storm_threshold_bft) {
            below_since = 0;
            if (above_since == 0) above_since = now;
            /* Sustain timer: positive seconds elapsed above threshold */
            g_storm_timer_secs = correction_active ? 0
                                : (int)((now - above_since) / 1000LL);
            if (!correction_active && (now - above_since) >= sustain_ms) {
                /* Sustained threshold reached — start driving rotors */
                for (int i = 0; i < g_num_rotors; i++)
                    g_rotors[i].storm_active = g_cfg.rotors[i].storm_enabled;
                g_storm_timer_secs = 0;
                next_goto_ms = 0; /* drive immediately */
                logmsg("Storm correction START (wind=%d Bft >= %d Bft, sustain=%d min)",
                       bft, g_cfg.storm_threshold_bft, g_cfg.storm_sustain_min);
            }
        } else {
            above_since = 0;
            if (correction_active) {
                if (below_since == 0) below_since = now;
                /* Release timer: negative seconds elapsed below threshold */
                g_storm_timer_secs = -(int)((now - below_since) / 1000LL);
                if ((now - below_since) >= release_ms) {
                    for (int i = 0; i < g_num_rotors; i++)
                        g_rotors[i].storm_active = 0;
                    below_since = 0;
                    next_goto_ms = 0;
                    g_storm_timer_secs = 0;
                    logmsg("Storm correction STOP (wind=%d Bft < %d Bft, release=%d min)",
                           bft, g_cfg.storm_threshold_bft, g_cfg.storm_release_min);
                }
            } else {
                g_storm_timer_secs = 0;
            }
        }

        /* Re-check correction_active after possible state change */
        correction_active = 0;
        for (int i = 0; i < g_num_rotors; i++)
            if (g_rotors[i].storm_active) { correction_active = 1; break; }
        g_storm_correcting = correction_active;

        if (correction_active && now >= next_goto_ms) {
            storm_apply_goto();
            next_goto_ms = now + interval_ms;
        }

        /* ── Return-to-storm timer for always_controllable rotors ── */
        if (correction_active) {
            pthread_mutex_lock(&g_wind_lock);
            double ret_dir = wind_dir_smooth();
            int    ret_has = g_wind.has_data;
            pthread_mutex_unlock(&g_wind_lock);

            for (int i = 0; i < g_num_rotors; i++) {
                Rotor *r = &g_rotors[i];
                if (!r->cfg.always_controllable)    continue;
                if (!r->storm_active)               continue;
                if (r->cfg.return_timeout_min <= 0) continue;
                pthread_mutex_lock(&r->lock);
                long long last_goto = r->last_manual_goto_ms;
                pthread_mutex_unlock(&r->lock);
                if (last_goto <= 0) continue;
                long long timeout_ms = (long long)r->cfg.return_timeout_min * 60000LL;
                if ((now - last_goto) < timeout_ms) continue;
                if (!ret_has || ret_dir < 0.5) continue;

                double primary  = fmod(ret_dir + r->cfg.storm_offset + 360.0, 360.0);
                double fallback = fmod(ret_dir + 180.0 + r->cfg.storm_offset + 360.0, 360.0);
                pthread_mutex_lock(&r->lock);
                double cur_az = r->current_az;
                double target = (fabs(cur_az - primary) > 180.0) ? fallback : primary;
                if (target < 0.5) target = 360.0;
                r->target_az           = target;
                r->moving              = 1;
                r->stall_ref_az        = r->current_az;
                r->stall_ref_ms        = now;
                r->last_manual_goto_ms = 0;
                if (r->cfg.simulate)
                    r->sim_dist = fabs(target - r->current_az);
                rotor_goto_az(r, target);
                pthread_mutex_unlock(&r->lock);
                logmsg("Return-to-storm rotor '%s' → %.1f° (idle %d min, wind=%.0f°)",
                       r->cfg.name, target, r->cfg.return_timeout_min, ret_dir);
            }
        }
    }

    return NULL;
}

/* ─── GUI Unix socket server ─────────────────────────────────────────────── */
/*
 * Accepts connections from the GUI on GUI_SOCK_PATH (SOCK_STREAM).
 * Pushes rotor and wind data (via gui_send_all) to all connected clients.
 * Receives commands from clients: STORM ON / STORM OFF / WIND?
 */
static void *gui_server_thread(void *arg)
{
    (void)arg;

    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) { logmsg("gui_sock: socket: %s", strerror(errno)); return NULL; }

    unlink(GUI_SOCK_PATH);

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, GUI_SOCK_PATH, sizeof(sa.sun_path) - 1);

    if (bind(srv, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        logmsg("gui_sock: bind %s: %s", GUI_SOCK_PATH, strerror(errno));
        close(srv);
        return NULL;
    }
    chmod(GUI_SOCK_PATH, 0666);   /* allow GUI process (same user) to connect */
    listen(srv, 4);
    logmsg("GUI socket listening: %s", GUI_SOCK_PATH);

    int clients[MAX_GUI_CLIENTS];
    int nclients = 0;
    for (int i = 0; i < MAX_GUI_CLIENTS; i++) clients[i] = -1;

    while (g_running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(srv, &rfds);
        int maxfd = srv;
        for (int i = 0; i < nclients; i++) {
            if (clients[i] >= 0) {
                FD_SET(clients[i], &rfds);
                if (clients[i] > maxfd) maxfd = clients[i];
            }
        }

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int r = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (r == 0) continue;

        /* ── Accept new connection ── */
        if (FD_ISSET(srv, &rfds)) {
            int cli = accept(srv, NULL, NULL);
            if (cli >= 0) {
                if (nclients < MAX_GUI_CLIENTS) {
                    /* Non-blocking so gui_send_all never stalls on a slow GUI */
                    int fl = fcntl(cli, F_GETFL, 0);
                    fcntl(cli, F_SETFL, fl | O_NONBLOCK);
                    clients[nclients++] = cli;

                    pthread_mutex_lock(&g_gui_lock);
                    g_num_gui_fds = 0;
                    for (int i = 0; i < nclients; i++)
                        if (clients[i] >= 0) g_gui_fds[g_num_gui_fds++] = clients[i];
                    pthread_mutex_unlock(&g_gui_lock);

                    logmsg("GUI client connected (%d active)", nclients);
                } else {
                    close(cli);
                    logmsg("GUI: too many clients, connection refused");
                }
            }
        }

        /* ── Read commands from each connected client ── */
        int changed = 0;
        for (int i = 0; i < nclients; i++) {
            if (clients[i] < 0 || !FD_ISSET(clients[i], &rfds)) continue;

            char buf[256];
            ssize_t n = recv(clients[i], buf, sizeof(buf) - 1, 0);
            if (n <= 0) {
                logmsg("GUI client disconnected");
                close(clients[i]);
                clients[i] = -1;
                changed = 1;
                continue;
            }
            buf[n] = '\0';
            /* strip trailing newline / whitespace */
            while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' '))
                buf[--n] = '\0';

            if      (strncmp(buf, "STORM ON",  8) == 0) {
                g_storm_active      = 1;
                g_cfg.storm_enabled = 1;
                config_write(g_config_path);
                g_wind_bcast_now    = 1;
                logmsg("Storm mode ACTIVATED (GUI)");
            } else if (strncmp(buf, "STORM OFF", 9) == 0) {
                g_storm_active      = 0;
                g_storm_correcting  = 0;
                g_cfg.storm_enabled = 0;
                for (int j = 0; j < g_num_rotors; j++)
                    g_rotors[j].storm_active = 0;
                config_write(g_config_path);
                g_wind_bcast_now    = 1;
                logmsg("Storm mode DEACTIVATED (GUI)");
            } else if (strncmp(buf, "STORM FORCE", 11) == 0) {
                g_storm_active      = 1;
                g_cfg.storm_enabled = 1;
                g_storm_correcting  = 1;
                for (int j = 0; j < g_num_rotors; j++)
                    g_rotors[j].storm_active = g_cfg.rotors[j].storm_enabled;
                storm_apply_goto();
                config_write(g_config_path);
                g_wind_bcast_now    = 1;
                logmsg("Storm correction FORCED (web — bypass sustain)");
            } else if (strncmp(buf, "WIND?", 5) == 0) {
                g_wind_bcast_now = 1;
                /* Send last known Ecowitt PASSKEY to the requesting GUI client */
                if (g_ecowitt_last_pk[0]) {
                    char pkmsg[96];
                    snprintf(pkmsg, sizeof(pkmsg), "ECOWITT_PK @ %s\n", g_ecowitt_last_pk);
                    send(clients[i], pkmsg, strlen(pkmsg), MSG_NOSIGNAL);
                }
            }
        }

        /* ── Compact client array after any disconnections ── */
        if (changed) {
            int new_n = 0;
            for (int i = 0; i < nclients; i++)
                if (clients[i] >= 0) clients[new_n++] = clients[i];
            nclients = new_n;

            pthread_mutex_lock(&g_gui_lock);
            g_num_gui_fds = 0;
            for (int i = 0; i < nclients; i++)
                g_gui_fds[g_num_gui_fds++] = clients[i];
            pthread_mutex_unlock(&g_gui_lock);

            logmsg("GUI clients: %d active", nclients);
        }
    }

    for (int i = 0; i < nclients; i++)
        if (clients[i] >= 0) close(clients[i]);
    close(srv);
    unlink(GUI_SOCK_PATH);
    return NULL;
}

/* ─── --run ──────────────────────────────────────────────────────────────── */
static int cmd_run(const char *config_path)
{
    if (config_read(config_path) < 0) {
        fprintf(stderr, "Run setup first: n1mm_rotor_server --setup\n");
        return 1;
    }
    /* Open fixed log file */
    g_logfp = fopen(LOG_FILE, "a");
    if (g_logfp) {
        fprintf(stdout, "Logging to: %s\n", LOG_FILE);
    } else {
        fprintf(stderr, "Warning: cannot open log file %s: %s\n"
                        "         Run 'sudo make install' to create it, or logging goes to stdout only.\n",
                LOG_FILE, strerror(errno));
    }

    logmsg("N1MM Rotor Server v%s starting — built by PD5DJ", VERSION);
    logmsg("Config: %s | %d rotor(s)", config_path, g_cfg.num_rotors);
    if (g_cfg.num_rotors == 0)
        logmsg("WARNING: no rotors configured — server will run but do nothing");

    g_num_rotors = g_cfg.num_rotors;
    for (int i = 0; i < g_num_rotors; i++) {
        Rotor *r = &g_rotors[i];
        memcpy(&r->cfg, &g_cfg.rotors[i], sizeof(RotorCfg));
        r->current_az    = 0.0;
        r->target_az     = 0.0;
        r->moving        = 0;
        r->protocol      = (strcasecmp(r->cfg.protocol, "PROSISTEL") == 0)
                           ? PROTO_PROSISTEL : PROTO_YAESU;
        r->variant       = YAESU_UNKNOWN;
        r->last_bcast_ms    = 0;
        r->stall_ref_az     = 0.0;
        r->stall_ref_ms     = 0;
        r->consec_errors    = 0;
        r->next_reconnect_ms = 0;
        pthread_mutex_init(&r->lock, NULL);

        if (r->cfg.simulate) {
            logmsg("Rotor '%s': simulate mode — no serial port, movement simulated",
                   r->cfg.name);
            r->fd = -1; /* ensure closed — simulate uses no port */
            continue;
        }

        char devpath[512];
        by_path_to_dev(r->cfg.by_path, devpath, sizeof(devpath));

        /* If by-path no longer exists, search by serial number */
        if (access(devpath, F_OK) != 0 && r->cfg.serial[0]) {
            DevEntry list[MAX_DEV_LIST];
            int ndev = build_dev_list(list, MAX_DEV_LIST);
            for (int j = 0; j < ndev; j++) {
                if (strcmp(list[j].serial, r->cfg.serial) == 0) {
                    logmsg("Rotor '%s': by-path changed, found via serial %s → %s",
                           r->cfg.name, r->cfg.serial, list[j].by_path);
                    by_path_to_dev(list[j].by_path, devpath, sizeof(devpath));
                    snprintf(r->cfg.by_path, sizeof(r->cfg.by_path), "%s", list[j].by_path);
                    break;
                }
            }
        }

        r->fd = serial_open(devpath, r->cfg.baud > 0 ? r->cfg.baud : 9600);
        if (r->fd < 0) {
            logmsg("WARNING: cannot open %s for rotor '%s': %s",
                   devpath, r->cfg.name, strerror(errno));
        } else {
            char resolved[256];
            resolve_symlink(devpath, resolved, sizeof(resolved));
            logmsg("Rotor '%s' on %s (%s, %d baud) — OK",
                   r->cfg.name, resolved,
                   r->protocol == PROTO_PROSISTEL ? "PROSISTEL" : "YAESU",
                   r->cfg.baud > 0 ? r->cfg.baud : 9600);

            if (r->protocol == PROTO_PROSISTEL) {
                prosistel_disable_cpm(r);
                logmsg("Rotor '%s': CPM disabled", r->cfg.name);
            }
        }
    }

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    /* Save runtime config path for use by udp_rx_thread (STORM ON/OFF persist) */
    snprintf(g_config_path, sizeof(g_config_path), "%s", config_path);

    /* Restore storm mode state that was persisted by a previous run */
    g_storm_active = g_cfg.storm_enabled;
    if (g_storm_active)
        logmsg("Storm mode restored: ON (last saved state)");

    pthread_t rx_tid, bcast_tid;
    if (pthread_create(&rx_tid,    NULL, udp_rx_thread,    NULL) != 0 ||
        pthread_create(&bcast_tid, NULL, poll_bcast_thread, NULL) != 0) {
        logmsg("FATAL: cannot create threads");
        return 1;
    }

    /* GUI Unix socket server — always started so the GUI can connect */
    pthread_t gui_tid;
    int gui_started = 0;
    if (pthread_create(&gui_tid, NULL, gui_server_thread, NULL) == 0)
        gui_started = 1;
    else
        logmsg("WARNING: could not start GUI socket thread");

    pthread_t wind_tid;
    int wind_started = 0;
    if (g_cfg.wind_enabled) {
        if (pthread_create(&wind_tid, NULL, wind_thread, NULL) == 0)
            wind_started = 1;
        else
            logmsg("WARNING: could not start wind monitor thread");
    }

    /* Storm thread starts whenever wind is enabled — it sleeps internally when
     * storm mode is OFF; activates the moment the GUI button is pressed. */
    pthread_t storm_tid;
    int storm_started = 0;
    if (g_cfg.wind_enabled) {
        if (pthread_create(&storm_tid, NULL, storm_thread, NULL) == 0)
            storm_started = 1;
        else
            logmsg("WARNING: could not start storm thread");
    }

    logmsg("Server running. Press Ctrl+C to stop.");
    logmsg("Broadcast: idle=%dms  moving=%dms", g_cfg.idle_ms, g_cfg.moving_ms);

    pthread_join(rx_tid,    NULL);
    pthread_join(bcast_tid, NULL);
    if (gui_started)   pthread_join(gui_tid,   NULL);
    if (wind_started)  pthread_join(wind_tid,  NULL);
    if (storm_started) pthread_join(storm_tid, NULL);

    for (int i = 0; i < g_num_rotors; i++) {
        if (g_rotors[i].fd >= 0) close(g_rotors[i].fd);
        pthread_mutex_destroy(&g_rotors[i].lock);
    }
    logmsg("Server stopped.");
    if (g_logfp) fclose(g_logfp);
    return 0;
}

/* ─── Main ───────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    const char *config_path = DEFAULT_CONFIG;
    const char *delete_name = NULL;
    int do_setup = 0, do_add = 0, do_change = 0, do_delete = 0, do_list = 0, do_run = 0;

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--setup")  == 0) do_setup  = 1;
        else if (strcmp(argv[i], "--add")    == 0) do_add    = 1;
        else if (strcmp(argv[i], "--change") == 0) do_change = 1;
        else if (strcmp(argv[i], "--list")   == 0) do_list   = 1;
        else if (strcmp(argv[i], "--run")    == 0) do_run    = 1;
        else if (strcmp(argv[i], "--delete") == 0) {
            do_delete = 1;
            if (i + 1 < argc) delete_name = argv[++i];
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "--ver") == 0) {
            printf("n1mm_rotor_server v%s — built by PD5DJ\n", VERSION);
            return 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage:\n");
            printf("  %s --setup              Configure server settings\n", argv[0]);
            printf("  %s --add               Add a rotor\n",               argv[0]);
            printf("  %s --change            Edit an existing rotor\n",    argv[0]);
            printf("  %s --delete <name>     Delete a rotor by name\n",    argv[0]);
            printf("  %s --list              List configured rotors\n",    argv[0]);
            printf("  %s --run               Start the server\n",          argv[0]);
            printf("  Config: %s\n", DEFAULT_CONFIG);
            printf("  %s --ver                            Show version\n",              argv[0]);
            return 0;
        }
    }

    if (!do_setup && !do_add && !do_change && !do_delete && !do_list && !do_run) {
        fprintf(stderr, "Usage: %s --setup | --add | --change | --delete <name> | --list | --run\n", argv[0]);
        fprintf(stderr, "       Use --help for details.\n");
        return 1;
    }

    if (do_setup)  { cmd_setup(config_path);  return 0; }
    if (do_add)    { cmd_add(config_path);    return 0; }
    if (do_change) { cmd_change(config_path); return 0; }
    if (do_delete) {
        cmd_delete(config_path, delete_name);  /* name may be NULL → interactive */
        return 0;
    }
    if (do_list)   { cmd_list(config_path);   return 0; }
    if (do_run)    { return cmd_run(config_path); }

    return 0;
}
