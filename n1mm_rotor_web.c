/*
 * n1mm_rotor_web.c — Web interface for N1MM Rotor Server
 *
 * Connects to n1mm_rotor_server via Unix socket (same as the GUI).
 * Serves a web UI on a configurable HTTP port (default 80).
 * Forwards GOTO/STORM commands to the server via UDP (same as the GUI).
 *
 * Tabs:
 *   Rotors — compass cards, click to send GOTO (password-protected)
 *   Meteo  — weather cards, wind rose, wind history graph
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
#include <dirent.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <math.h>
#include <limits.h>
#include <sys/reboot.h>

/* ─── Constants ─────────────────────────────────────────────────────────── */
#define VERSION          "2.10.3"
#define DEFAULT_CONFIG   "/etc/n1mm_rotor_server/n1mm_rotor_server.conf"
#define GUI_SOCK_PATH    "/tmp/n1mm_rotor_server.sock"
#define WWW_DIR          "/etc/n1mm_rotor_server/www"
#define MAX_ROTORS       16
#define MAX_LINE         512
#define MAX_SSE_CLIENTS  16
#define TOKEN_LEN        32
#define MAX_TOKENS       8

#define WIND_HIST_DAT         "/etc/n1mm_rotor_server/wind_hist.dat"
#define WIND_HIST_DAT_HOURS   48
#define WIND_HIST_DAT_INTVL_S 300   /* one record per 5 minutes */

#define JISNAN(v)  (!((v)==(v)))

/* ─── JSON helpers (used by config parser — must precede it) ─────────────── */
static double json_field_d(const char *json, const char *key)
{
    char needle[64]; snprintf(needle,sizeof(needle),"\"%s\":",key);
    const char *p = strstr(json,needle); if (!p) return (double)__builtin_nan("");
    p = strchr(p,':'); if (!p) return (double)__builtin_nan("");
    return atof(p+1);
}
static int json_field_s(const char *json, const char *key, char *out, size_t sz)
{
    char needle[64]; snprintf(needle,sizeof(needle),"\"%s\":\"",key);
    const char *p = strstr(json,needle); if (!p) { out[0]='\0'; return -1; }
    p += strlen(needle);
    size_t i = 0;
    while (*p && *p != '"' && i+1 < sz) out[i++] = *p++;
    out[i] = '\0'; return 0;
}

/* ─── Full config (mirrors server Config + RotorCfg) ────────────────────── */
typedef struct {
    char name[64];
    char by_path[256];
    char serial[64];
    char protocol[16];   /* "YAESU" | "PROSISTEL" */
    int  baud;
    int  offset;         /* heading offset -180..180 */
    int  storm_enabled;
    int  storm_offset;
    int  always_controllable;
    int  simulate;
    int  return_timeout_min;
} WebRotorCfg;

typedef struct {
    /* Server */
    int  cmd_port, bcast_port, bcast_port2;
    char bcast_addr[256];
    int  idle_ms, moving_ms;
    char logfile[256];
    /* Rotors */
    int         num_rotors;
    WebRotorCfg rotors[MAX_ROTORS];
    /* Wind */
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
    int    wind_ecowitt_correction;
    char   wind_ecowitt_fwd_passkey[64];
    /* Storm */
    int    storm_enabled;
    int    storm_threshold_bft;
    int    storm_sustain_min;
    int    storm_release_min;
    int    storm_interval_min;
    int    storm_block_manual;
    /* Web */
    int  web_port;
    int  session_timeout_min;    /* inactivity timeout in minutes (default 60) */
    char web_password[64];       /* admin: storm control, config, settings */
    char web_view_password[64];  /* control: send GOTO / STOP commands */
} WebConfig;

static WebConfig g_cfg;
static char      g_config_path[512];

/* ─── Runtime state ──────────────────────────────────────────────────────── */
typedef struct {
    char   name[64];
    double current_az;
    double target_az;
    int    moving;
    int    has_data;
    int    storm_active;
    time_t last_rx;
    time_t last_manual_goto;   /* unix time of last manual GOTO (0 = none) */
} RotorState;

typedef struct {
    double dir;
    int    bft;
    double gust;
    double temp_c;
    double feels_like_c;
    double humidity_pct;
    double baro_hpa;
    double precip_mm;
    double uv_index;
    double solar_wm2;
    int    storm_active;
    int    correcting;
    int    timer_secs;
    int    fallback;
    time_t fetch_time;
    int    has_data;
} WindState;

/* Wind history ring buffer */
#define WIND_HIST_MAX 360
typedef struct { time_t ts; double dir; int bft; double gust; } WindHist;

static RotorState  g_rotors[MAX_ROTORS];
static int         g_num_rotors = 0;
static WindState   g_wind;
static WindHist    g_wind_hist[WIND_HIST_MAX];
static int         g_wind_hist_head = 0;
static int         g_wind_hist_count = 0;
static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;

static volatile int g_running = 1;

/* Persistent wind/meteo history */
static time_t g_hist_dat_last  = 0;
static int    g_hist_dat_count = 0;

/* ─── Auth tokens ────────────────────────────────────────────────────────── */
#define TOKEN_IDLE_DEFAULT 3600
#define token_idle_secs() (g_cfg.session_timeout_min > 0 ? g_cfg.session_timeout_min * 60 : TOKEN_IDLE_DEFAULT)
typedef struct { char tok[TOKEN_LEN+1]; time_t last_used; } AuthToken;
static AuthToken       g_tokens[MAX_TOKENS];
static pthread_mutex_t g_token_lock = PTHREAD_MUTEX_INITIALIZER;

static void token_generate(char *out)
{
    static const char ch[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    for (int i = 0; i < TOKEN_LEN; i++)
        out[i] = ch[rand() % (int)(sizeof(ch)-1)];
    out[TOKEN_LEN] = '\0';
}

static int token_store(const char *tok)
{
    pthread_mutex_lock(&g_token_lock);
    time_t now = time(NULL);
    /* Expire idle tokens and find a free slot */
    int slot = -1;
    for (int i = 0; i < MAX_TOKENS; i++) {
        if (!g_tokens[i].tok[0] || (now - g_tokens[i].last_used) > token_idle_secs())
            { g_tokens[i].tok[0] = '\0'; if (slot < 0) slot = i; }
    }
    if (slot >= 0) {
        snprintf(g_tokens[slot].tok, sizeof(g_tokens[slot].tok), "%s", tok);
        g_tokens[slot].last_used = now;
    }
    pthread_mutex_unlock(&g_token_lock);
    return slot >= 0 ? 0 : -1;
}

static int token_valid(const char *tok)
{
    if (!tok || !tok[0]) return 0;
    if (!g_cfg.web_password[0]) return 1; /* no password set → always valid */
    pthread_mutex_lock(&g_token_lock);
    time_t now = time(NULL);
    int ok = 0;
    for (int i = 0; i < MAX_TOKENS; i++) {
        if (g_tokens[i].tok[0] &&
            (now - g_tokens[i].last_used) <= token_idle_secs() &&
            strcmp(g_tokens[i].tok, tok) == 0)
            { ok = 1; g_tokens[i].last_used = now; break; } /* refresh on use */
    }
    pthread_mutex_unlock(&g_token_lock);
    return ok;
}

/* ─── SSE client list ────────────────────────────────────────────────────── */
typedef struct { int fd; int active; } SseClient;
static SseClient       g_sse[MAX_SSE_CLIENTS];
static pthread_mutex_t g_sse_lock = PTHREAD_MUTEX_INITIALIZER;

static void sse_add(int fd)
{
    /* Cap send() blocking time so one dead/slow client cannot stall all
     * broadcasts while g_sse_lock is held. 2 s is generous for LAN use. */
    struct timeval tv = {2, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    pthread_mutex_lock(&g_sse_lock);
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (!g_sse[i].active) { g_sse[i].fd = fd; g_sse[i].active = 1; break; }
    }
    pthread_mutex_unlock(&g_sse_lock);
}

static void sse_remove(int fd)
{
    pthread_mutex_lock(&g_sse_lock);
    for (int i = 0; i < MAX_SSE_CLIENTS; i++)
        if (g_sse[i].active && g_sse[i].fd == fd)
            { g_sse[i].active = 0; break; }
    pthread_mutex_unlock(&g_sse_lock);
}

static void sse_broadcast(const char *event, const char *data)
{
    char buf[2048];
    int  len = snprintf(buf, sizeof(buf), "event: %s\ndata: %s\n\n", event, data);
    if (len <= 0 || len >= (int)sizeof(buf)) return;
    pthread_mutex_lock(&g_sse_lock);
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (g_sse[i].active) {
            if (send(g_sse[i].fd, buf, (size_t)len, MSG_NOSIGNAL) < 0) {
                close(g_sse[i].fd);
                g_sse[i].active = 0;
            }
        }
    }
    pthread_mutex_unlock(&g_sse_lock);
}

/* ─── Config reader ──────────────────────────────────────────────────────── */
/* Escape a string for JSON output */
static void json_escape(const char *src, char *dst, size_t sz)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j+2 < sz; i++) {
        unsigned char c = (unsigned char)src[i];
        if      (c == '"')  { if (j+2<sz){dst[j++]='\\'; dst[j++]='"'; } }
        else if (c == '\\') { if (j+2<sz){dst[j++]='\\'; dst[j++]='\\'; } }
        else if (c < 0x20)  { /* skip control chars */ }
        else                { dst[j++] = src[i]; }
    }
    dst[j] = '\0';
}

static void cfg_defaults(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.cmd_port         = 12040;
    g_cfg.bcast_port       = 13010;
    g_cfg.bcast_port2      = 0;
    snprintf(g_cfg.bcast_addr, sizeof(g_cfg.bcast_addr), "255.255.255.255");
    g_cfg.idle_ms          = 1000;
    g_cfg.moving_ms        = 200;
    snprintf(g_cfg.logfile, sizeof(g_cfg.logfile), "/var/log/n1mm_rotor_server.log");
    g_cfg.wind_enabled     = 0;
    snprintf(g_cfg.wind_source, sizeof(g_cfg.wind_source), "openmeteo");
    g_cfg.wind_lat         = 52.4656;
    g_cfg.wind_lon         = 4.5314;
    g_cfg.wind_interval_min = 15;
    g_cfg.wind_serial_baud = 19200;
    g_cfg.wind_ecowitt_port = 49199;
    g_cfg.wind_ecowitt_fallback_min = 5;
    snprintf(g_cfg.wind_ecowitt_fwd_path, sizeof(g_cfg.wind_ecowitt_fwd_path), "/");
    g_cfg.wind_ecowitt_correction     = 100;
    g_cfg.storm_threshold_bft = 7;
    g_cfg.storm_sustain_min   = 5;
    g_cfg.storm_release_min   = 10;
    g_cfg.storm_interval_min  = 15;
    g_cfg.storm_block_manual  = 1;
    g_cfg.web_port             = 80;
    g_cfg.session_timeout_min  = 60;
}

static void cfg_read(const char *path)
{
    snprintf(g_config_path, sizeof(g_config_path), "%s", path);
    cfg_defaults();

    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        int l = (int)strlen(line);
        while (l > 0 && (line[l-1]=='\n'||line[l-1]=='\r'||line[l-1]==' ')) line[--l]='\0';
        if (line[0]=='#' || line[0]=='\0') continue;
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = '\0';
        const char *k = line, *v = eq+1;
        if      (!strcmp(k,"cmd_port"))     g_cfg.cmd_port     = atoi(v);
        else if (!strcmp(k,"bcast_port"))   g_cfg.bcast_port   = atoi(v);
        else if (!strcmp(k,"bcast_port2"))  g_cfg.bcast_port2  = atoi(v);
        else if (!strcmp(k,"bcast_addr"))   snprintf(g_cfg.bcast_addr,  sizeof(g_cfg.bcast_addr),  "%s",v);
        else if (!strcmp(k,"idle_ms"))      g_cfg.idle_ms      = atoi(v);
        else if (!strcmp(k,"moving_ms"))    g_cfg.moving_ms    = atoi(v);
        else if (!strcmp(k,"logfile"))      snprintf(g_cfg.logfile,      sizeof(g_cfg.logfile),      "%s",v);
        else if (!strcmp(k,"wind_enabled")) g_cfg.wind_enabled = atoi(v);
        else if (!strcmp(k,"wind_source"))  snprintf(g_cfg.wind_source,  sizeof(g_cfg.wind_source),  "%s",v);
        else if (!strcmp(k,"wind_lat"))     g_cfg.wind_lat     = atof(v);
        else if (!strcmp(k,"wind_lon"))     g_cfg.wind_lon     = atof(v);
        else if (!strcmp(k,"wind_interval_min"))   g_cfg.wind_interval_min  = atoi(v);
        else if (!strcmp(k,"wind_serial_device"))  snprintf(g_cfg.wind_serial_device, sizeof(g_cfg.wind_serial_device), "%s",v);
        else if (!strcmp(k,"wind_serial_baud"))    g_cfg.wind_serial_baud   = atoi(v);
        else if (!strcmp(k,"wind_udp_port"))       g_cfg.wind_udp_port      = atoi(v);
        else if (!strcmp(k,"wind_owm_apikey"))     snprintf(g_cfg.wind_owm_apikey,    sizeof(g_cfg.wind_owm_apikey),    "%s",v);
        else if (!strcmp(k,"wind_wapi_apikey"))    snprintf(g_cfg.wind_wapi_apikey,   sizeof(g_cfg.wind_wapi_apikey),   "%s",v);
        else if (!strcmp(k,"wind_ecowitt_port"))        g_cfg.wind_ecowitt_port      = atoi(v);
        else if (!strcmp(k,"wind_ecowitt_passkey"))     snprintf(g_cfg.wind_ecowitt_passkey,    sizeof(g_cfg.wind_ecowitt_passkey),    "%s",v);
        else if (!strcmp(k,"wind_ecowitt_fallback"))    snprintf(g_cfg.wind_ecowitt_fallback,   sizeof(g_cfg.wind_ecowitt_fallback),   "%s",v);
        else if (!strcmp(k,"wind_ecowitt_fallback_min"))g_cfg.wind_ecowitt_fallback_min = atoi(v);
        else if (!strcmp(k,"wind_ecowitt_fwd_host"))    snprintf(g_cfg.wind_ecowitt_fwd_host,   sizeof(g_cfg.wind_ecowitt_fwd_host),   "%s",v);
        else if (!strcmp(k,"wind_ecowitt_fwd_port"))    g_cfg.wind_ecowitt_fwd_port  = atoi(v);
        else if (!strcmp(k,"wind_ecowitt_fwd_path"))       snprintf(g_cfg.wind_ecowitt_fwd_path,   sizeof(g_cfg.wind_ecowitt_fwd_path),   "%s",v);
        else if (!strcmp(k,"wind_ecowitt_correction"))     g_cfg.wind_ecowitt_correction = atoi(v);
        else if (!strcmp(k,"wind_ecowitt_fwd_passkey")) snprintf(g_cfg.wind_ecowitt_fwd_passkey,sizeof(g_cfg.wind_ecowitt_fwd_passkey),"%s",v);
        else if (!strcmp(k,"storm_enabled"))        g_cfg.storm_enabled       = atoi(v);
        else if (!strcmp(k,"storm_threshold_bft"))  g_cfg.storm_threshold_bft = atoi(v);
        else if (!strcmp(k,"storm_sustain_min"))    g_cfg.storm_sustain_min   = atoi(v);
        else if (!strcmp(k,"storm_release_min"))    g_cfg.storm_release_min   = atoi(v);
        else if (!strcmp(k,"storm_interval_min"))   g_cfg.storm_interval_min  = atoi(v);
        else if (!strcmp(k,"storm_block_manual"))   g_cfg.storm_block_manual  = atoi(v);
        else if (!strcmp(k,"web_port"))             g_cfg.web_port            = atoi(v);
        else if (!strcmp(k,"session_timeout_min"))  g_cfg.session_timeout_min = atoi(v);
        else if (!strcmp(k,"web_password"))      snprintf(g_cfg.web_password,      sizeof(g_cfg.web_password),      "%s",v);
        else if (!strcmp(k,"web_view_password")) snprintf(g_cfg.web_view_password, sizeof(g_cfg.web_view_password), "%s",v);
        else if (!strcmp(k,"num_rotors")) {
            g_cfg.num_rotors = atoi(v);
            if (g_cfg.num_rotors > MAX_ROTORS) g_cfg.num_rotors = MAX_ROTORS;
        } else {
            int idx = -1; char field[64];
            if (sscanf(k,"rotor%d_%63s",&idx,field)==2 && idx>=0 && idx<MAX_ROTORS) {
                WebRotorCfg *r = &g_cfg.rotors[idx];
                if      (!strcmp(field,"name"))     snprintf(r->name,     sizeof(r->name),     "%s",v);
                else if (!strcmp(field,"by_path"))  snprintf(r->by_path,  sizeof(r->by_path),  "%s",v);
                else if (!strcmp(field,"serial"))   snprintf(r->serial,   sizeof(r->serial),   "%s",v);
                else if (!strcmp(field,"protocol")) snprintf(r->protocol, sizeof(r->protocol), "%s",v);
                else if (!strcmp(field,"baud"))              r->baud              = atoi(v);
                else if (!strcmp(field,"offset"))            r->offset            = atoi(v);
                else if (!strcmp(field,"storm_enabled"))     r->storm_enabled     = atoi(v);
                else if (!strcmp(field,"storm_offset"))      r->storm_offset      = atoi(v);
                else if (!strcmp(field,"always_controllable")) r->always_controllable  = atoi(v);
                else if (!strcmp(field,"simulate"))            r->simulate             = atoi(v);
                else if (!strcmp(field,"return_timeout_min"))  r->return_timeout_min   = atoi(v);
            }
        }
    }
    fclose(f);
    /* Seed rotor state from config */
    pthread_mutex_lock(&g_state_lock);
    g_num_rotors = g_cfg.num_rotors;
    for (int i = 0; i < g_num_rotors; i++) {
        snprintf(g_rotors[i].name, sizeof(g_rotors[i].name), "%s", g_cfg.rotors[i].name);
        /* Simulate rotors start with has_data=1 at az=0 so compass shows immediately */
        g_rotors[i].has_data = g_cfg.rotors[i].simulate ? 1 : 0;
        if (!g_cfg.rotors[i].simulate) {
            g_rotors[i].current_az = 0;
            g_rotors[i].target_az  = 0;
        }
    }
    pthread_mutex_unlock(&g_state_lock);
}

/* Write full config file — same format as server */
static int cfg_write(void)
{
    FILE *f = fopen(g_config_path, "w");
    if (!f) return -1;
    fprintf(f, "# N1MM Rotor Server — server settings\n");
    fprintf(f, "cmd_port=%d\n",     g_cfg.cmd_port);
    fprintf(f, "bcast_port=%d\n",   g_cfg.bcast_port);
    fprintf(f, "bcast_port2=%d\n",  g_cfg.bcast_port2);
    fprintf(f, "bcast_addr=%s\n",   g_cfg.bcast_addr);
    fprintf(f, "idle_ms=%d\n",      g_cfg.idle_ms);
    fprintf(f, "moving_ms=%d\n",    g_cfg.moving_ms);
    fprintf(f, "logfile=%s\n\n",    g_cfg.logfile);
    fprintf(f, "# Wind monitor\n");
    fprintf(f, "wind_enabled=%d\n",         g_cfg.wind_enabled);
    fprintf(f, "wind_source=%s\n",          g_cfg.wind_source[0] ? g_cfg.wind_source : "openmeteo");
    fprintf(f, "wind_lat=%.6f\n",           g_cfg.wind_lat);
    fprintf(f, "wind_lon=%.6f\n",           g_cfg.wind_lon);
    fprintf(f, "wind_interval_min=%d\n",    g_cfg.wind_interval_min);
    fprintf(f, "wind_serial_device=%s\n",   g_cfg.wind_serial_device);
    fprintf(f, "wind_serial_baud=%d\n",     g_cfg.wind_serial_baud);
    fprintf(f, "wind_udp_port=%d\n",        g_cfg.wind_udp_port);
    fprintf(f, "wind_owm_apikey=%s\n",      g_cfg.wind_owm_apikey);
    fprintf(f, "wind_wapi_apikey=%s\n",     g_cfg.wind_wapi_apikey);
    fprintf(f, "wind_ecowitt_port=%d\n",         g_cfg.wind_ecowitt_port);
    fprintf(f, "wind_ecowitt_passkey=%s\n",      g_cfg.wind_ecowitt_passkey);
    fprintf(f, "wind_ecowitt_fallback=%s\n",     g_cfg.wind_ecowitt_fallback);
    fprintf(f, "wind_ecowitt_fallback_min=%d\n", g_cfg.wind_ecowitt_fallback_min);
    fprintf(f, "wind_ecowitt_fwd_host=%s\n",     g_cfg.wind_ecowitt_fwd_host);
    fprintf(f, "wind_ecowitt_fwd_port=%d\n",     g_cfg.wind_ecowitt_fwd_port);
    fprintf(f, "wind_ecowitt_fwd_path=%s\n",       g_cfg.wind_ecowitt_fwd_path);
    fprintf(f, "wind_ecowitt_correction=%d\n",     g_cfg.wind_ecowitt_correction);
    fprintf(f, "wind_ecowitt_fwd_passkey=%s\n\n",g_cfg.wind_ecowitt_fwd_passkey);
    fprintf(f, "# Storm mode\n");
    fprintf(f, "storm_enabled=%d\n",        g_cfg.storm_enabled);
    fprintf(f, "storm_threshold_bft=%d\n",  g_cfg.storm_threshold_bft);
    fprintf(f, "storm_sustain_min=%d\n",    g_cfg.storm_sustain_min);
    fprintf(f, "storm_release_min=%d\n",    g_cfg.storm_release_min);
    fprintf(f, "storm_interval_min=%d\n",   g_cfg.storm_interval_min);
    fprintf(f, "storm_block_manual=%d\n\n", g_cfg.storm_block_manual);
    fprintf(f, "# Web interface\n");
    fprintf(f, "web_enabled=1\n");
    fprintf(f, "web_port=%d\n",             g_cfg.web_port);
    fprintf(f, "session_timeout_min=%d\n",  g_cfg.session_timeout_min);
    fprintf(f, "web_password=%s\n",        g_cfg.web_password);
    fprintf(f, "web_view_password=%s\n\n", g_cfg.web_view_password);
    fprintf(f, "# Rotors\n");
    fprintf(f, "num_rotors=%d\n\n", g_cfg.num_rotors);
    for (int i = 0; i < g_cfg.num_rotors; i++) {
        WebRotorCfg *r = &g_cfg.rotors[i];
        fprintf(f, "rotor%d_name=%s\n",             i, r->name);
        fprintf(f, "rotor%d_by_path=%s\n",           i, r->by_path);
        fprintf(f, "rotor%d_serial=%s\n",            i, r->serial);
        fprintf(f, "rotor%d_protocol=%s\n",          i, r->protocol[0] ? r->protocol : "YAESU");
        fprintf(f, "rotor%d_baud=%d\n",              i, r->baud > 0 ? r->baud : 9600);
        fprintf(f, "rotor%d_offset=%d\n",            i, r->offset);
        fprintf(f, "rotor%d_storm_enabled=%d\n",     i, r->storm_enabled);
        fprintf(f, "rotor%d_storm_offset=%d\n",      i, r->storm_offset);
        fprintf(f, "rotor%d_always_controllable=%d\n",    i, r->always_controllable);
        fprintf(f, "rotor%d_return_timeout_min=%d\n",    i, r->return_timeout_min);
        fprintf(f, "rotor%d_simulate=%d\n\n",             i, r->simulate);
    }
    fclose(f);
    return 0;
}

/* Serialize full config to JSON */
static int config_to_json(char *buf, size_t sz)
{
    int len = 0;
    char esc[512];
#define JS(key,val)     do { json_escape(val,esc,sizeof(esc)); len+=snprintf(buf+len,sz-len,"\"%s\":\"%s\",",key,esc); } while(0)
#define JI(key,val)     len+=snprintf(buf+len,sz-len,"\"%s\":%d,",key,val)
#define JF(key,val)     len+=snprintf(buf+len,sz-len,"\"%s\":%.6f,",key,val)

    len += snprintf(buf+len, sz-len, "{");
    JI("cmd_port",      g_cfg.cmd_port);
    JI("bcast_port",    g_cfg.bcast_port);
    JI("bcast_port2",   g_cfg.bcast_port2);
    JS("bcast_addr",    g_cfg.bcast_addr);
    JI("idle_ms",       g_cfg.idle_ms);
    JI("moving_ms",     g_cfg.moving_ms);
    JS("logfile",       g_cfg.logfile);
    JI("wind_enabled",  g_cfg.wind_enabled);
    JS("wind_source",   g_cfg.wind_source);
    JF("wind_lat",      g_cfg.wind_lat);
    JF("wind_lon",      g_cfg.wind_lon);
    JI("wind_interval_min",        g_cfg.wind_interval_min);
    JS("wind_serial_device",       g_cfg.wind_serial_device);
    JI("wind_serial_baud",         g_cfg.wind_serial_baud);
    JI("wind_udp_port",            g_cfg.wind_udp_port);
    JS("wind_owm_apikey",          g_cfg.wind_owm_apikey);
    JS("wind_wapi_apikey",         g_cfg.wind_wapi_apikey);
    JI("wind_ecowitt_port",        g_cfg.wind_ecowitt_port);
    JS("wind_ecowitt_passkey",     g_cfg.wind_ecowitt_passkey);
    JS("wind_ecowitt_fallback",    g_cfg.wind_ecowitt_fallback);
    JI("wind_ecowitt_fallback_min",g_cfg.wind_ecowitt_fallback_min);
    JS("wind_ecowitt_fwd_host",    g_cfg.wind_ecowitt_fwd_host);
    JI("wind_ecowitt_fwd_port",    g_cfg.wind_ecowitt_fwd_port);
    JS("wind_ecowitt_fwd_path",    g_cfg.wind_ecowitt_fwd_path);
    JI("wind_ecowitt_correction",  g_cfg.wind_ecowitt_correction);
    JS("wind_ecowitt_fwd_passkey", g_cfg.wind_ecowitt_fwd_passkey);
    JI("storm_enabled",        g_cfg.storm_enabled);
    JI("storm_threshold_bft",  g_cfg.storm_threshold_bft);
    JI("storm_sustain_min",    g_cfg.storm_sustain_min);
    JI("storm_release_min",    g_cfg.storm_release_min);
    JI("storm_interval_min",   g_cfg.storm_interval_min);
    JI("storm_block_manual",   g_cfg.storm_block_manual);
    JI("web_port",             g_cfg.web_port);
    JI("session_timeout_min",  g_cfg.session_timeout_min);
    /* Never send actual passwords to client — send set/not-set flags only */
    JI("web_password_set",      g_cfg.web_password[0]      ? 1 : 0);
    JI("web_view_password_set", g_cfg.web_view_password[0] ? 1 : 0);
    JI("num_rotors",           g_cfg.num_rotors);
    /* Rotors array */
    len += snprintf(buf+len, sz-len, "\"rotors\":[");
    for (int i = 0; i < g_cfg.num_rotors && len < (int)sz-200; i++) {
        WebRotorCfg *r = &g_cfg.rotors[i];
        if (i) len += snprintf(buf+len, sz-len, ",");
        char ename[80], ebp[512], eprot[24];
        json_escape(r->name,    ename, sizeof(ename));
        json_escape(r->by_path, ebp,   sizeof(ebp));
        json_escape(r->protocol[0] ? r->protocol : "YAESU", eprot, sizeof(eprot));
        len += snprintf(buf+len, sz-len,
            "{\"name\":\"%s\",\"by_path\":\"%s\",\"protocol\":\"%s\","
            "\"baud\":%d,\"offset\":%d,\"storm_enabled\":%d,"
            "\"storm_offset\":%d,\"always_controllable\":%d,\"simulate\":%d,\"return_timeout_min\":%d}",
            ename, ebp, eprot,
            r->baud > 0 ? r->baud : 9600, r->offset,
            r->storm_enabled, r->storm_offset, r->always_controllable, r->simulate, r->return_timeout_min);
    }
    len += snprintf(buf+len, sz-len, "]}");
    /* Remove last trailing comma before rotors (from the JI/JS macros) */
    /* Actually there's no trailing comma since rotors array ends with ]} */
#undef JS
#undef JI
#undef JF
    return len;
}

/* Parse config from JSON body (POST /api/config) */
static void json_to_config(const char *json, WebConfig *c)
{
    char s[256];
    double d;
#define GS(key,dst,sz) if(json_field_s(json,key,s,sizeof(s))==0) snprintf(dst,sz,"%s",s)
#define GI(key,dst)    do { d=json_field_d(json,key); if(!JISNAN(d)) dst=(int)d; } while(0)
#define GF(key,dst)    do { d=json_field_d(json,key); if(!JISNAN(d)) dst=d; } while(0)

    GI("cmd_port",       c->cmd_port);
    GI("bcast_port2",    c->bcast_port2);
    GS("bcast_addr",     c->bcast_addr,  sizeof(c->bcast_addr));
    GI("idle_ms",        c->idle_ms);
    GI("moving_ms",      c->moving_ms);
    GS("logfile",        c->logfile,     sizeof(c->logfile));
    GI("wind_enabled",   c->wind_enabled);
    GS("wind_source",    c->wind_source, sizeof(c->wind_source));
    GF("wind_lat",       c->wind_lat);
    GF("wind_lon",       c->wind_lon);
    GI("wind_interval_min",        c->wind_interval_min);
    GS("wind_serial_device",       c->wind_serial_device,      sizeof(c->wind_serial_device));
    GI("wind_serial_baud",         c->wind_serial_baud);
    GI("wind_udp_port",            c->wind_udp_port);
    GS("wind_owm_apikey",          c->wind_owm_apikey,         sizeof(c->wind_owm_apikey));
    GS("wind_wapi_apikey",         c->wind_wapi_apikey,        sizeof(c->wind_wapi_apikey));
    GI("wind_ecowitt_port",        c->wind_ecowitt_port);
    GS("wind_ecowitt_passkey",     c->wind_ecowitt_passkey,    sizeof(c->wind_ecowitt_passkey));
    GS("wind_ecowitt_fallback",    c->wind_ecowitt_fallback,   sizeof(c->wind_ecowitt_fallback));
    GI("wind_ecowitt_fallback_min",c->wind_ecowitt_fallback_min);
    GS("wind_ecowitt_fwd_host",    c->wind_ecowitt_fwd_host,   sizeof(c->wind_ecowitt_fwd_host));
    GI("wind_ecowitt_fwd_port",    c->wind_ecowitt_fwd_port);
    GS("wind_ecowitt_fwd_path",    c->wind_ecowitt_fwd_path,   sizeof(c->wind_ecowitt_fwd_path));
    GI("wind_ecowitt_correction",  c->wind_ecowitt_correction);
    GS("wind_ecowitt_fwd_passkey", c->wind_ecowitt_fwd_passkey,sizeof(c->wind_ecowitt_fwd_passkey));
    GI("storm_threshold_bft",  c->storm_threshold_bft);
    GI("storm_sustain_min",    c->storm_sustain_min);
    GI("storm_release_min",    c->storm_release_min);
    GI("storm_interval_min",   c->storm_interval_min);
    GI("storm_block_manual",   c->storm_block_manual);
    GI("web_port",             c->web_port);
    GI("session_timeout_min",  c->session_timeout_min);
    /* Only overwrite password fields if a non-empty value was sent */
    { char pw[64]="";
      if (json_field_s(json,"web_password",     pw,sizeof(pw))==0 && pw[0])
          snprintf(c->web_password,      sizeof(c->web_password),      "%s",pw); }
    { char pw[64]="";
      if (json_field_s(json,"web_view_password",pw,sizeof(pw))==0 && pw[0])
          snprintf(c->web_view_password, sizeof(c->web_view_password), "%s",pw); }
    /* Allow explicitly clearing a password by sending "__clear__" */
    { char pw[64]="";
      if (json_field_s(json,"web_password",     pw,sizeof(pw))==0 && !strcmp(pw,"__clear__"))
          c->web_password[0]      = '\0'; }
    { char pw[64]="";
      if (json_field_s(json,"web_view_password",pw,sizeof(pw))==0 && !strcmp(pw,"__clear__"))
          c->web_view_password[0] = '\0'; }

    /* Parse rotors array */
    const char *arr = strstr(json, "\"rotors\":[");
    if (arr) {
        arr = strchr(arr, '[') + 1;
        int ri = 0;
        while (*arr && *arr != ']' && ri < MAX_ROTORS) {
            const char *ob = strchr(arr, '{');
            if (!ob) break;
            /* Find matching } */
            int depth = 0; const char *oe = ob;
            while (*oe) { if (*oe=='{') depth++; else if (*oe=='}') { depth--; if (depth==0) break; } oe++; }
            /* Parse rotor object */
            size_t objlen = (size_t)(oe - ob + 1) + 1;
            char *obj = malloc(objlen);
            if (obj) {
                memcpy(obj, ob, objlen-1); obj[objlen-1]='\0';
                WebRotorCfg *r = &c->rotors[ri];
                memset(r, 0, sizeof(*r));
                json_field_s(obj,"name",    r->name,     sizeof(r->name));
                json_field_s(obj,"by_path", r->by_path,  sizeof(r->by_path));
                json_field_s(obj,"protocol",r->protocol, sizeof(r->protocol));
                d=json_field_d(obj,"baud");            if(!JISNAN(d)) r->baud=(int)d;
                d=json_field_d(obj,"offset");          if(!JISNAN(d)) r->offset=(int)d;
                d=json_field_d(obj,"storm_enabled");   if(!JISNAN(d)) r->storm_enabled=(int)d;
                d=json_field_d(obj,"storm_offset");    if(!JISNAN(d)) r->storm_offset=(int)d;
                d=json_field_d(obj,"always_controllable"); if(!JISNAN(d)) r->always_controllable=(int)d;
                d=json_field_d(obj,"simulate");            if(!JISNAN(d)) r->simulate=(int)d;
                d=json_field_d(obj,"return_timeout_min");  if(!JISNAN(d)) r->return_timeout_min=(int)d;
                free(obj);
                ri++;
            }
            arr = oe + 1;
        }
        c->num_rotors = ri;
    }
#undef GS
#undef GI
#undef GF
}

/* List serial devices — universal across Linux platforms.
 *
 * Returns all entries from /dev/serial/by-path/ without deduplication so that
 * config-stored paths (which may include usbv2 variants) always match exactly.
 * Falls back to /dev/ttyUSB* and /dev/ttyACM* if by-path is unavailable.
 */
#define LS_MAX 64
typedef struct { char name[256]; char tty[260]; int prio; } LsEntry;

/* Add entry; deduplicate by tty. Lower prio wins; on tie, shorter name wins. */
static void ls_add(LsEntry *list, int *n, const char *name, const char *tty, int prio)
{
    for (int i = 0; i < *n; i++) {
        if (strcmp(list[i].tty, tty) == 0) {
            if (prio < list[i].prio ||
                (prio == list[i].prio && strlen(name) < strlen(list[i].name))) {
                snprintf(list[i].name, sizeof(list[i].name), "%s", name);
                list[i].prio = prio;
            }
            return;
        }
    }
    if (*n >= LS_MAX) return;
    snprintf(list[*n].name, sizeof(list[*n].name), "%s", name);
    snprintf(list[*n].tty,  sizeof(list[*n].tty),  "%.63s", tty);
    list[*n].prio = prio;
    (*n)++;
}

static int list_serial_devices(char *buf, size_t sz)
{
    LsEntry list[LS_MAX];
    int n = 0;

    /* Scan /dev/serial/by-path — deduplicate by resolved tty device.
     * Two symlinks pointing to the same ttyUSBx (e.g. usb and usbv2 variants)
     * are collapsed; the shorter name wins. */
    DIR *dp = opendir("/dev/serial/by-path");
    if (dp) {
        struct dirent *de;
        while ((de = readdir(dp)) != NULL && n < LS_MAX) {
            if (de->d_name[0] == '.') continue;
            char full[512];
            snprintf(full, sizeof(full), "/dev/serial/by-path/%s", de->d_name);
            char target[256] = "";
            ssize_t rl = readlink(full, target, sizeof(target)-1);
            if (rl > 0) {
                target[rl] = '\0';
                const char *base = strrchr(target, '/');
                base = base ? base+1 : target;
                if (strncmp(base, "tty", 3) == 0) {
                    ls_add(list, &n, de->d_name, base, 0);
                    continue;
                }
            }
            /* readlink failed — add unconditionally */
            ls_add(list, &n, de->d_name, de->d_name, 0);
        }
        closedir(dp);
    }

    /* Fallback: ttyUSB* and ttyACM* in /dev — only if by-path gave nothing */
    if (n == 0) {
        DIR *d = opendir("/dev");
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL && n < LS_MAX) {
                if (strncmp(de->d_name, "ttyUSB", 6) != 0 &&
                    strncmp(de->d_name, "ttyACM", 6) != 0) continue;
                ls_add(list, &n, de->d_name, de->d_name, 2);
            }
            closedir(d);
        }
    }

    /* Sort alphabetically by name for consistent display */
    for (int i = 0; i < n-1; i++)
        for (int j = i+1; j < n; j++)
            if (strcmp(list[i].name, list[j].name) > 0) {
                LsEntry tmp = list[i]; list[i] = list[j]; list[j] = tmp;
            }

    int len = 0;
    len += snprintf(buf+len, sz-len, "[");
    for (int i = 0; i < n; i++) {
        if (i) len += snprintf(buf+len, sz-len, ",");
        char ename[512]; json_escape(list[i].name, ename, sizeof(ename));
        len += snprintf(buf+len, sz-len, "\"%s\"", ename);
    }
    len += snprintf(buf+len, sz-len, "]");
    return len;
}

/* ─── Unix socket → server thread ───────────────────────────────────────── */
static void parse_wind_line(const char *line)
{
    /* WIND @ dir/bft/gust/storm/fetch/correcting/timer/fallback */
    if (strncmp(line,"WIND @ ",7)) return;
    const char *p = line+7;
    double dir=0,gust=0; int bft=0,storm=0,corr=0,timer=0,fb=0;
    long long fetch=0;
    sscanf(p,"%lf/%d/%lf/%d/%lld/%d/%d/%d",&dir,&bft,&gust,&storm,&fetch,&corr,&timer,&fb);
    pthread_mutex_lock(&g_state_lock);
    g_wind.dir         = dir;
    g_wind.bft         = bft;
    g_wind.gust        = gust;
    g_wind.storm_active = storm;
    g_wind.correcting  = corr;
    g_wind.timer_secs  = timer;
    g_wind.fallback    = fb;
    g_wind.fetch_time  = (time_t)fetch;
    g_wind.has_data    = 1;
    /* Record history every ~60 s */
    time_t now = time(NULL);
    int last = (g_wind_hist_head + WIND_HIST_MAX - 1) % WIND_HIST_MAX;
    if (g_wind_hist_count == 0 || (now - g_wind_hist[last].ts) >= 60) {
        g_wind_hist[g_wind_hist_head] = (WindHist){now, dir, bft, gust};
        g_wind_hist_head = (g_wind_hist_head+1) % WIND_HIST_MAX;
        if (g_wind_hist_count < WIND_HIST_MAX) g_wind_hist_count++;
    }
    pthread_mutex_unlock(&g_state_lock);
}

static void parse_meteo_line(const char *line)
{
    /* METEO @ key=val key=val ... */
    if (strncmp(line,"METEO @ ",8)) return;
    const char *p = line+8;
    pthread_mutex_lock(&g_state_lock);
    /* Reset to NaN then fill what's present */
    g_wind.temp_c = g_wind.feels_like_c = g_wind.humidity_pct =
    g_wind.baro_hpa = g_wind.precip_mm = g_wind.uv_index = g_wind.solar_wm2 =
        (double)__builtin_nan("");
    while (p && *p) {
        while (*p == ' ') p++;
        char key[32]; double val;
        if (sscanf(p, "%31[^=]=%lf", key, &val) == 2) {
            if (!strcmp(key,"temp"))    g_wind.temp_c       = val;
            else if (!strcmp(key,"feels"))  g_wind.feels_like_c = val;
            else if (!strcmp(key,"hum"))    g_wind.humidity_pct = val;
            else if (!strcmp(key,"baro"))   g_wind.baro_hpa     = val;
            else if (!strcmp(key,"precip")) g_wind.precip_mm    = val;
            else if (!strcmp(key,"uv"))     g_wind.uv_index     = val;
            else if (!strcmp(key,"solar"))  g_wind.solar_wm2    = val;
        }
        p = strchr(p, ' ');
    }
    pthread_mutex_unlock(&g_state_lock);
}

static void parse_rotor_line(const char *line)
{
    /* "NAME @ az_times_10 / target_times_10 | last_goto_unix"
     * Target and last_goto parts are optional for backward compat. */
    const char *at = strstr(line, " @ ");
    if (!at) return;
    char name[64]; int head, tgt_head = -1;
    size_t nlen = (size_t)(at - line);
    if (nlen >= sizeof(name)) return;
    memcpy(name, line, nlen); name[nlen] = '\0';
    /* Try to parse az and optional target */
    int n = sscanf(at+3, "%d / %d", &head, &tgt_head);
    if (n < 1) return;
    double az  = head / 10.0;
    double tgt = (n >= 2 && tgt_head >= 0) ? tgt_head / 10.0 : -1.0;
    /* Parse optional | last_goto_unix */
    long long last_goto = 0;
    const char *pipe = strstr(at+3, " | ");
    if (pipe) sscanf(pipe+3, "%lld", &last_goto);
    pthread_mutex_lock(&g_state_lock);
    for (int i = 0; i < g_num_rotors; i++) {
        if (!strcmp(g_rotors[i].name, name)) {
            int was_moving = g_rotors[i].moving;
            g_rotors[i].current_az = az;
            g_rotors[i].has_data   = 1;
            g_rotors[i].last_rx    = time(NULL);
            if (last_goto > 0) g_rotors[i].last_manual_goto = (time_t)last_goto;
            /* Update target from broadcast — catches N1MM GOTO and server simulate */
            if (tgt >= 0.0) g_rotors[i].target_az = tgt;
            if (fabs(az - g_rotors[i].target_az) < 2.0)
                g_rotors[i].moving = 0;
            else if (tgt >= 0.0 && fabs(tgt - az) > 2.0)
                g_rotors[i].moving = 1;
            if (g_rotors[i].moving != was_moving || !g_rotors[i].moving) {
                /* trigger SSE push below */
            }
            break;
        }
    }
    pthread_mutex_unlock(&g_state_lock);
}

/* Build SSE JSON for all rotors */
static void sse_push_rotors(void)
{
    char buf[2048]; int len = 0;
    pthread_mutex_lock(&g_state_lock);
    len += snprintf(buf+len, sizeof(buf)-len, "[");
    for (int i = 0; i < g_num_rotors; i++) {
        if (i) len += snprintf(buf+len, sizeof(buf)-len, ",");
        len += snprintf(buf+len, sizeof(buf)-len,
            "{\"name\":\"%s\",\"az\":%.1f,\"target\":%.1f,\"moving\":%d,\"has_data\":%d,\"storm_active\":%d,\"last_goto\":%lld}",
            g_rotors[i].name, g_rotors[i].current_az, g_rotors[i].target_az,
            g_rotors[i].moving, g_rotors[i].has_data, g_rotors[i].storm_active,
            (long long)g_rotors[i].last_manual_goto);
    }
    len += snprintf(buf+len, sizeof(buf)-len, "]");
    pthread_mutex_unlock(&g_state_lock);
    sse_broadcast("rotors", buf);
}

static void sse_push_wind(void)
{
    char buf[512]; int len = 0;
    pthread_mutex_lock(&g_state_lock);
    WindState w = g_wind;
    pthread_mutex_unlock(&g_state_lock);
    if (!w.has_data) return;
    len += snprintf(buf+len, sizeof(buf)-len,
        "{\"dir\":%.1f,\"bft\":%d,\"gust\":%.1f"
        ",\"storm\":%d,\"correcting\":%d,\"timer\":%d,\"fallback\":%d"
        ",\"fetch\":%lld",
        w.dir, w.bft, w.gust,
        w.storm_active, w.correcting, w.timer_secs, w.fallback,
        (long long)w.fetch_time);
    /* optional fields */
    if (!JISNAN(w.temp_c))       len += snprintf(buf+len,sizeof(buf)-len,",\"temp\":%.1f",       w.temp_c);
    if (!JISNAN(w.feels_like_c)) len += snprintf(buf+len,sizeof(buf)-len,",\"feels\":%.1f",      w.feels_like_c);
    if (!JISNAN(w.humidity_pct)) len += snprintf(buf+len,sizeof(buf)-len,",\"hum\":%.0f",        w.humidity_pct);
    if (!JISNAN(w.baro_hpa))     len += snprintf(buf+len,sizeof(buf)-len,",\"baro\":%.1f",       w.baro_hpa);
    if (!JISNAN(w.precip_mm))    len += snprintf(buf+len,sizeof(buf)-len,",\"precip\":%.1f",     w.precip_mm);
    if (!JISNAN(w.uv_index))     len += snprintf(buf+len,sizeof(buf)-len,",\"uv\":%.0f",         w.uv_index);
    if (!JISNAN(w.solar_wm2))    len += snprintf(buf+len,sizeof(buf)-len,",\"solar\":%.0f",      w.solar_wm2);
    len += snprintf(buf+len, sizeof(buf)-len, "}");
    sse_broadcast("wind", buf);
}

static void sse_push_hist(void)
{
    char buf[8192]; int len = 0;
    pthread_mutex_lock(&g_state_lock);
    int cnt = g_wind_hist_count;
    int head = g_wind_hist_head;
    WindHist hist[WIND_HIST_MAX];
    memcpy(hist, g_wind_hist, sizeof(hist));
    pthread_mutex_unlock(&g_state_lock);
    len += snprintf(buf+len, sizeof(buf)-len, "[");
    int start = (head - cnt + WIND_HIST_MAX) % WIND_HIST_MAX;
    for (int i = 0; i < cnt; i++) {
        int idx = (start + i) % WIND_HIST_MAX;
        if (i) len += snprintf(buf+len, sizeof(buf)-len, ",");
        len += snprintf(buf+len, sizeof(buf)-len,
            "{\"ts\":%lld,\"dir\":%.1f,\"bft\":%d,\"gust\":%.1f}",
            (long long)hist[idx].ts, hist[idx].dir, hist[idx].bft, hist[idx].gust);
        if (len > (int)sizeof(buf) - 100) break;
    }
    len += snprintf(buf+len, sizeof(buf)-len, "]");
    sse_broadcast("hist", buf);
}

/* ─── Staleness / SSE-tick thread ───────────────────────────────────────
 * Checks for stale non-simulated rotors and pushes SSE updates.
 * Simulated rotors are moved by n1mm_rotor_server; their positions
 * arrive via the Unix socket and are forwarded to SSE clients directly.
 * ─────────────────────────────────────────────────────────────────────── */
#define WEB_SIM_INTERVAL_MS  200     /* ms between ticks             */

#define ROTOR_STALE_S 8   /* seconds without broadcast → show NO DATA */

static void *sim_thread(void *arg)
{
    (void)arg;
    while (g_running) {
        usleep(WEB_SIM_INTERVAL_MS * 1000);
        int any = 0;
        time_t now = time(NULL);
        pthread_mutex_lock(&g_state_lock);

        /* Staleness check for non-simulated rotors */
        for (int i = 0; i < g_num_rotors; i++) {
            if (g_cfg.rotors[i].simulate) continue;
            RotorState *r = &g_rotors[i];
            if (r->has_data && r->last_rx > 0 &&
                (now - r->last_rx) > ROTOR_STALE_S) {
                r->has_data = 0;
                r->moving   = 0;
                any = 1;
            }
        }

        /* Simulated rotors are moved by the server; no interpolation here. */
        pthread_mutex_unlock(&g_state_lock);
        if (any) sse_push_rotors();
    }
    return NULL;
}

/* ─── Persistent wind/meteo history (wind_hist.dat) ─────────────────────── */

/* Parse one CSV line: ts,dir,bft,gust,temp,feels,hum,baro,precip,uv,solar */
static int hist_csv_parse(const char *line,
    long long *ts, double *dir, int *bft, double *gust,
    double *temp, double *feels, double *hum, double *baro,
    double *precip, double *uv, double *solar)
{
    *temp = *feels = *hum = *baro = *precip = *uv = *solar =
        (double)__builtin_nan("");
    char buf[256];
    size_t l = strlen(line);
    if (l >= sizeof(buf)) return -1;
    memcpy(buf, line, l+1);
    while (l > 0 && (buf[l-1]=='\n'||buf[l-1]=='\r')) buf[--l]='\0';
    char *f[11]; int n = 0, fcount;
    char *p = buf;
    while (n < 11) {
        f[n++] = p;
        char *c = strchr(p, ',');
        if (!c) break;
        *c = '\0'; p = c+1;
    }
    fcount = n;
    if (fcount < 4 || !f[0][0]) return -1;
    *ts   = atoll(f[0]);
    *dir  = atof(f[1]);
    *bft  = atoi(f[2]);
    *gust = atof(f[3]);
    if (fcount > 4  && f[4][0])  *temp   = atof(f[4]);
    if (fcount > 5  && f[5][0])  *feels  = atof(f[5]);
    if (fcount > 6  && f[6][0])  *hum    = atof(f[6]);
    if (fcount > 7  && f[7][0])  *baro   = atof(f[7]);
    if (fcount > 8  && f[8][0])  *precip = atof(f[8]);
    if (fcount > 9  && f[9][0])  *uv     = atof(f[9]);
    if (fcount > 10 && f[10][0]) *solar  = atof(f[10]);
    return 0;
}

/* Remove records older than WIND_HIST_DAT_HOURS (rewrites file) */
static void hist_dat_compact(void)
{
    time_t cutoff = time(NULL) - ((time_t)WIND_HIST_DAT_HOURS * 3600);
    char tmp[300];
    snprintf(tmp, sizeof(tmp), "%s.tmp", WIND_HIST_DAT);
    FILE *in = fopen(WIND_HIST_DAT, "r");
    if (!in) return;
    FILE *out = fopen(tmp, "w");
    if (!out) { fclose(in); return; }
    char line[256];
    while (fgets(line, sizeof(line), in)) {
        long long ts = atoll(line);
        if (ts >= (long long)cutoff) fputs(line, out);
    }
    fclose(in); fclose(out);
    rename(tmp, WIND_HIST_DAT);
}

/* Append one record to wind_hist.dat (throttled to WIND_HIST_DAT_INTVL_S) */
static void hist_dat_write(void)
{
    time_t now = time(NULL);
    if (now - g_hist_dat_last < WIND_HIST_DAT_INTVL_S) return;
    g_hist_dat_last = now;

    pthread_mutex_lock(&g_state_lock);
    WindState w = g_wind;
    pthread_mutex_unlock(&g_state_lock);
    if (!w.has_data) return;

    FILE *f = fopen(WIND_HIST_DAT, "a");
    if (!f) return;

    char t[16],fe[16],h[16],b[16],pr[16],uv[16],so[16];
#define HFMT1(buf,v) do{if(JISNAN(v))buf[0]='\0';else snprintf(buf,sizeof(buf),"%.1f",v);}while(0)
#define HFMT0(buf,v) do{if(JISNAN(v))buf[0]='\0';else snprintf(buf,sizeof(buf),"%.0f",v);}while(0)
    HFMT1(t,  w.temp_c);       HFMT1(fe, w.feels_like_c);
    HFMT0(h,  w.humidity_pct); HFMT1(b,  w.baro_hpa);
    HFMT1(pr, w.precip_mm);    HFMT0(uv, w.uv_index); HFMT0(so, w.solar_wm2);
#undef HFMT1
#undef HFMT0

    fprintf(f, "%lld,%.1f,%d,%.1f,%s,%s,%s,%s,%s,%s,%s\n",
        (long long)now, w.dir, w.bft, w.gust, t, fe, h, b, pr, uv, so);
    fclose(f);

    /* Compact every 12 writes (~1 h) to keep file within 48 h window */
    if (++g_hist_dat_count % 12 == 0) hist_dat_compact();
}

/* GET /api/hist-meteo — return last 48 h from wind_hist.dat as JSON */
static void api_hist_meteo(int fd)
{
    time_t cutoff = time(NULL) - ((time_t)WIND_HIST_DAT_HOURS * 3600);
    FILE *f = fopen(WIND_HIST_DAT, "r");
    if (!f) {
        dprintf(fd, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                    "Content-Length: 2\r\nAccess-Control-Allow-Origin: *\r\n\r\n[]");
        return;
    }

    /* 192 KB — ample for 576 entries × ~250 bytes */
    size_t bufsz = 196608;
    char *buf = malloc(bufsz);
    if (!buf) { fclose(f);
        dprintf(fd,"HTTP/1.1 500\r\nContent-Length: 0\r\n\r\n"); return; }

    int len = 0, first = 1;
    len += snprintf(buf+len, bufsz-len, "[");

    char line[256];
    while (fgets(line, sizeof(line), f) && len < (int)bufsz - 512) {
        long long ts; double dir,gust,temp,feels,hum,baro,precip,uv,solar; int bft;
        if (hist_csv_parse(line,&ts,&dir,&bft,&gust,
                           &temp,&feels,&hum,&baro,&precip,&uv,&solar) < 0) continue;
        if (ts < (long long)cutoff) continue;
        if (!first) len += snprintf(buf+len, bufsz-len, ",");
        first = 0;
        len += snprintf(buf+len, bufsz-len,
            "{\"ts\":%lld,\"dir\":%.1f,\"bft\":%d,\"gust\":%.1f",
            ts, dir, bft, gust);
        if (!JISNAN(temp))   len += snprintf(buf+len,bufsz-len,",\"temp\":%.1f",   temp);
        if (!JISNAN(feels))  len += snprintf(buf+len,bufsz-len,",\"feels\":%.1f",  feels);
        if (!JISNAN(hum))    len += snprintf(buf+len,bufsz-len,",\"hum\":%.0f",    hum);
        if (!JISNAN(baro))   len += snprintf(buf+len,bufsz-len,",\"baro\":%.1f",   baro);
        if (!JISNAN(precip)) len += snprintf(buf+len,bufsz-len,",\"precip\":%.1f", precip);
        if (!JISNAN(uv))     len += snprintf(buf+len,bufsz-len,",\"uv\":%.0f",     uv);
        if (!JISNAN(solar))  len += snprintf(buf+len,bufsz-len,",\"solar\":%.0f",  solar);
        len += snprintf(buf+len, bufsz-len, "}");
    }
    fclose(f);
    len += snprintf(buf+len, bufsz-len, "]");

    dprintf(fd, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                "Content-Length: %d\r\nAccess-Control-Allow-Origin: *\r\n\r\n", len);
    send(fd, buf, (size_t)len, MSG_NOSIGNAL);
    free(buf);
}

static void *unix_rx_thread(void *arg)
{
    (void)arg;
    while (g_running) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) { sleep(2); continue; }
        struct sockaddr_un sa; memset(&sa,0,sizeof(sa));
        sa.sun_family = AF_UNIX;
        snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", GUI_SOCK_PATH);
        if (connect(fd,(struct sockaddr *)&sa,sizeof(sa)) < 0) {
            close(fd); sleep(2); continue;
        }
        /* Request immediate wind data */
        send(fd, "WIND?\n", 6, MSG_NOSIGNAL);

        char buf[4096]; int pos = 0;
        while (g_running) {
            int n = (int)recv(fd, buf+pos, sizeof(buf)-pos-1, 0);
            if (n <= 0) break;
            pos += n; buf[pos] = '\0';
            char *nl;
            while ((nl = strchr(buf, '\n')) != NULL) {
                *nl = '\0';
                char *line = buf;
                if (!strncmp(line,"WIND @ ",7)) {
                    parse_wind_line(line);
                    sse_push_wind();
                    sse_push_rotors();
                    hist_dat_write();
                } else if (!strncmp(line,"METEO @ ",8)) {
                    parse_meteo_line(line);
                    /* wind SSE will be pushed on next WIND line */
                } else if (strstr(line," @ ")) {
                    parse_rotor_line(line);
                    sse_push_rotors();
                }
                int rem = (int)(buf+pos - (nl+1));
                memmove(buf, nl+1, (size_t)rem);
                pos = rem; buf[pos] = '\0';
            }
            if (pos >= (int)sizeof(buf)-1) pos = 0; /* overflow guard */
        }
        close(fd);
        /* Mark all non-simulated rotors as stale — server connection lost */
        pthread_mutex_lock(&g_state_lock);
        for (int i = 0; i < g_num_rotors; i++) {
            if (!g_cfg.rotors[i].simulate) {
                g_rotors[i].has_data = 0;
                g_rotors[i].moving   = 0;
            }
        }
        pthread_mutex_unlock(&g_state_lock);
        sse_push_rotors();
        sleep(2);
    }
    return NULL;
}

/* ─── UDP command senders ────────────────────────────────────────────────── */
static void udp_send_local(const char *msg)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return;
    struct sockaddr_in dst; memset(&dst,0,sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = htons((uint16_t)g_cfg.cmd_port);
    dst.sin_addr.s_addr = inet_addr("127.0.0.1");
    sendto(s, msg, strlen(msg), 0, (struct sockaddr *)&dst, sizeof(dst));
    close(s);
}

static void send_goto(const char *rotor_name, double az)
{
    char xml[256];
    snprintf(xml, sizeof(xml),
        "<N1MMRotor><rotor>%s</rotor><goazi>%.1f</goazi><offset>0</offset></N1MMRotor>",
        rotor_name, az);
    udp_send_local(xml);
}

static void send_stop(const char *rotor_name)
{
    char xml[256];
    snprintf(xml, sizeof(xml),
        "<N1MMRotor><rotor>%s</rotor><stop>1</stop></N1MMRotor>",
        rotor_name);
    udp_send_local(xml);
}

static void send_storm(int on)
{
    udp_send_local(on ? "STORM ON" : "STORM OFF");
}

/* ─── Minimal HTTP server ────────────────────────────────────────────────── */

/* Read token from Authorization header or ?token= query string */
static const char *http_bearer(const char *req)
{
    static char tok[TOKEN_LEN+1];
    /* Try Authorization: Bearer header first */
    const char *p = strstr(req, "Authorization: Bearer ");
    if (p) {
        p += strlen("Authorization: Bearer ");
        int i = 0;
        while (*p && *p != '\r' && *p != '\n' && i < TOKEN_LEN)
            tok[i++] = *p++;
        tok[i] = '\0';
        return tok;
    }
    /* Fall back to ?token=<tok> query string (used by EventSource) */
    p = strstr(req, "?token=");
    if (!p) p = strstr(req, "&token=");
    if (p) {
        p += 7;
        int i = 0;
        while (*p && *p != ' ' && *p != '&' && *p != '\r' && *p != '\n' && i < TOKEN_LEN)
            tok[i++] = *p++;
        tok[i] = '\0';
        return tok;
    }
    tok[0] = '\0'; return tok;
}

/* Read POST body */
static const char *http_body(const char *req)
{
    const char *p = strstr(req, "\r\n\r\n");
    return p ? p+4 : NULL;
}


/* Serve a file from WWW_DIR */
static void serve_file(int fd, const char *path)
{
    char full[512]; snprintf(full,sizeof(full),"%s%s",WWW_DIR,path);
    /* Prevent path traversal */
    if (strstr(path,"..")) { dprintf(fd,"HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n"); return; }
    FILE *f = fopen(full,"rb");
    if (!f) { dprintf(fd,"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"); return; }
    fseek(f,0,SEEK_END); long sz = ftell(f); rewind(f);
    const char *ct = "application/octet-stream";
    if (strstr(path,".html")) ct="text/html; charset=utf-8";
    else if (strstr(path,".css"))  ct="text/css";
    else if (strstr(path,".js"))   ct="application/javascript";
    else if (strstr(path,".svg"))  ct="image/svg+xml";
    dprintf(fd,"HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n"
               "Cache-Control: no-cache\r\n\r\n",ct,sz);
    char fbuf[4096]; size_t nr;
    while ((nr = fread(fbuf,1,sizeof(fbuf),f)) > 0)
        send(fd,fbuf,nr,MSG_NOSIGNAL);
    fclose(f);
}

/* GET /api/status → JSON */
static void api_status(int fd)
{
    char buf[4096]; int len = 0;
    pthread_mutex_lock(&g_state_lock);
    WindState w = g_wind;
    len += snprintf(buf+len,sizeof(buf)-len,
        "{\"rotors\":[");
    for (int i = 0; i < g_num_rotors; i++) {
        if (i) len+=snprintf(buf+len,sizeof(buf)-len,",");
        len+=snprintf(buf+len,sizeof(buf)-len,
            "{\"name\":\"%s\",\"az\":%.1f,\"target\":%.1f,\"moving\":%d,\"has_data\":%d}",
            g_rotors[i].name,g_rotors[i].current_az,g_rotors[i].target_az,
            g_rotors[i].moving,g_rotors[i].has_data);
    }
    len+=snprintf(buf+len,sizeof(buf)-len,"],\"wind\":");
    if (w.has_data) {
        len+=snprintf(buf+len,sizeof(buf)-len,
            "{\"dir\":%.1f,\"bft\":%d,\"gust\":%.1f"
            ",\"storm\":%d,\"correcting\":%d,\"timer\":%d",
            w.dir,w.bft,w.gust,w.storm_active,w.correcting,w.timer_secs);
        if (!JISNAN(w.temp_c))       len+=snprintf(buf+len,sizeof(buf)-len,",\"temp\":%.1f",w.temp_c);
        if (!JISNAN(w.feels_like_c)) len+=snprintf(buf+len,sizeof(buf)-len,",\"feels\":%.1f",w.feels_like_c);
        if (!JISNAN(w.humidity_pct)) len+=snprintf(buf+len,sizeof(buf)-len,",\"hum\":%.0f",w.humidity_pct);
        if (!JISNAN(w.baro_hpa))     len+=snprintf(buf+len,sizeof(buf)-len,",\"baro\":%.1f",w.baro_hpa);
        if (!JISNAN(w.precip_mm))    len+=snprintf(buf+len,sizeof(buf)-len,",\"precip\":%.1f",w.precip_mm);
        if (!JISNAN(w.uv_index))     len+=snprintf(buf+len,sizeof(buf)-len,",\"uv\":%.0f",w.uv_index);
        if (!JISNAN(w.solar_wm2))    len+=snprintf(buf+len,sizeof(buf)-len,",\"solar\":%.0f",w.solar_wm2);
        len+=snprintf(buf+len,sizeof(buf)-len,"}");
    } else {
        len+=snprintf(buf+len,sizeof(buf)-len,"null");
    }
    len+=snprintf(buf+len,sizeof(buf)-len,
                  ",\"auth_required\":%s,\"control_required\":%s"
                  ",\"version\":\"%s\",\"author\":\"PD5DJ\"}",
                  g_cfg.web_password[0]      ? "true":"false",
                  g_cfg.web_view_password[0] ? "true":"false",
                  VERSION);
    pthread_mutex_unlock(&g_state_lock);
    dprintf(fd,
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Content-Length: %d\r\nAccess-Control-Allow-Origin: *\r\n\r\n%s",
        len, buf);
}

/* GET /api/hist → wind history JSON */
static void api_hist(int fd)
{
    char buf[16384]; int len = 0;
    pthread_mutex_lock(&g_state_lock);
    int cnt = g_wind_hist_count, head = g_wind_hist_head;
    WindHist hist[WIND_HIST_MAX];
    memcpy(hist,g_wind_hist,sizeof(hist));
    pthread_mutex_unlock(&g_state_lock);
    len+=snprintf(buf+len,sizeof(buf)-len,"[");
    int start=(head-cnt+WIND_HIST_MAX)%WIND_HIST_MAX;
    for (int i=0;i<cnt;i++) {
        int idx=(start+i)%WIND_HIST_MAX;
        if (i) len+=snprintf(buf+len,sizeof(buf)-len,",");
        len+=snprintf(buf+len,sizeof(buf)-len,
            "{\"ts\":%lld,\"dir\":%.1f,\"bft\":%d,\"gust\":%.1f}",
            (long long)hist[idx].ts,hist[idx].dir,hist[idx].bft,hist[idx].gust);
        if (len>(int)sizeof(buf)-100) break;
    }
    len+=snprintf(buf+len,sizeof(buf)-len,"]");
    dprintf(fd,
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Content-Length: %d\r\nAccess-Control-Allow-Origin: *\r\n\r\n%s",
        len,buf);
}

/* POST /api/login
 * Accepts view password OR operator password.
 * Returns token + "operator":1 when operator password matched. */
static void api_login(int fd, const char *body)
{
    if (!body) { dprintf(fd,"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"); return; }
    char pw[64]; json_field_s(body,"password",pw,sizeof(pw));

    int is_operator = 0;
    int ok = 0;

    /* No passwords set → always OK as operator */
    if (!g_cfg.web_password[0] && !g_cfg.web_view_password[0]) { ok = 1; is_operator = 1; }
    /* Operator password match */
    else if (g_cfg.web_password[0] && !strcmp(pw, g_cfg.web_password))      { ok = 1; is_operator = 1; }
    /* View-only password match */
    else if (g_cfg.web_view_password[0] && !strcmp(pw, g_cfg.web_view_password)) { ok = 1; is_operator = 0; }
    /* No view password set but operator password matches → operator */
    else if (!g_cfg.web_view_password[0] && g_cfg.web_password[0] && !strcmp(pw, g_cfg.web_password)) { ok = 1; is_operator = 1; }

    if (ok) {
        char tok[TOKEN_LEN+1]; token_generate(tok); token_store(tok);
        char resp[128];
        int l = snprintf(resp, sizeof(resp), "{\"token\":\"%s\",\"operator\":%d}", tok, is_operator);
        dprintf(fd,"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%s",l,resp);
    } else {
        const char *resp="{\"error\":\"wrong password\"}";
        dprintf(fd,"HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",strlen(resp),resp);
    }
}

/* POST /api/goto — always allowed, no auth required */
static void api_goto(int fd, const char *req)
{
    const char *body = http_body(req);
    if (!body) { dprintf(fd,"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"); return; }
    char name[64]; double az = json_field_d(body,"az");
    json_field_s(body,"rotor",name,sizeof(name));
    if (!name[0] || JISNAN(az)) {
        dprintf(fd,"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"); return;
    }
    /* Update local target; start web-side simulate if applicable */
    pthread_mutex_lock(&g_state_lock);
    for (int i=0;i<g_num_rotors;i++) {
        if (!strcmp(g_rotors[i].name,name)) {
            g_rotors[i].target_az        = az;
            g_rotors[i].moving           = 1;
            g_rotors[i].has_data         = 1;
            g_rotors[i].last_manual_goto = time(NULL);
            break;
        }
    }
    pthread_mutex_unlock(&g_state_lock);
    send_goto(name,az);
    sse_push_rotors();
    dprintf(fd,"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK");
}

/* POST /api/stop — always allowed, no auth required */
static void api_stop(int fd, const char *req)
{
    const char *body = http_body(req);
    if (!body) { dprintf(fd,"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"); return; }
    char name[64]; name[0] = '\0';
    json_field_s(body,"rotor",name,sizeof(name));
    if (!name[0]) {
        dprintf(fd,"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"); return;
    }
    pthread_mutex_lock(&g_state_lock);
    for (int i=0;i<g_num_rotors;i++) {
        if (!strcmp(g_rotors[i].name,name)) {
            g_rotors[i].moving = 0;
            break;
        }
    }
    pthread_mutex_unlock(&g_state_lock);
    send_stop(name);
    sse_push_rotors();
    dprintf(fd,"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK");
}

/* POST /api/heartbeat — refreshes session token (returns 200 or 401) */
static void api_heartbeat(int fd, const char *req)
{
    const char *tok = http_bearer(req);
    if (g_cfg.web_password[0] && !token_valid(tok)) {
        dprintf(fd,"HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n"); return;
    }
    dprintf(fd,"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK");
}

/* POST /api/storm
 * Body: {"on":1} | {"on":0} | {"force":1}
 * force=1 → immediately apply storm correction, bypassing sustain timer */
static void api_storm(int fd, const char *req)
{
    const char *tok = http_bearer(req);
    if (g_cfg.web_password[0] && !token_valid(tok)) {
        dprintf(fd,"HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n"); return;
    }
    const char *body = http_body(req);
    if (!body) { dprintf(fd,"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"); return; }

    double force_val = json_field_d(body, "force");
    if (!JISNAN(force_val) && (int)force_val == 1) {
        udp_send_local("STORM FORCE");
        dprintf(fd,"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK");
        return;
    }

    int on = (int)json_field_d(body,"on");
    send_storm(on);
    dprintf(fd,"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK");
}

/* POST /api/set-password  { "field": "web_password"|"web_view_password", "value": "..." }
 * Saves a single password field without restarting the server. */
static void api_set_password(int fd, const char *req)
{
    const char *tok = http_bearer(req);
    if (g_cfg.web_password[0] && !token_valid(tok)) {
        dprintf(fd,"HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n"); return;
    }
    const char *body = http_body(req);
    if (!body) { dprintf(fd,"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"); return; }
    char field[32]="", value[64]="";
    json_field_s(body, "field", field, sizeof(field));
    json_field_s(body, "value", value, sizeof(value));
    int clear = !strcmp(value, "__clear__");
    if (!strcmp(field,"web_password")) {
        if (clear) g_cfg.web_password[0]      = '\0';
        else if (value[0]) snprintf(g_cfg.web_password,      sizeof(g_cfg.web_password),      "%s",value);
    } else if (!strcmp(field,"web_view_password")) {
        if (clear) g_cfg.web_view_password[0] = '\0';
        else if (value[0]) snprintf(g_cfg.web_view_password, sizeof(g_cfg.web_view_password), "%s",value);
    } else {
        dprintf(fd,"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"); return;
    }
    cfg_write();
    dprintf(fd,"HTTP/1.1 200 OK\r\nContent-Length: 8\r\n\r\n{\"ok\":1}");
}

/* POST /api/reboot */
static void api_reboot(int fd, const char *req)
{
    const char *tok = http_bearer(req);
    if (g_cfg.web_password[0] && !token_valid(tok)) {
        dprintf(fd,"HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n"); return;
    }
    dprintf(fd,"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK");
    close(fd); fd = -1;
    sleep(1);
    sync();
    if (reboot(RB_AUTOBOOT) < 0)
        system("systemctl reboot");
}

/* POST /api/shutdown */
static void api_shutdown(int fd, const char *req)
{
    const char *tok = http_bearer(req);
    if (g_cfg.web_password[0] && !token_valid(tok)) {
        dprintf(fd,"HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n"); return;
    }
    dprintf(fd,"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK");
    close(fd); fd = -1;
    sleep(1);
    sync();
    if (reboot(RB_POWER_OFF) < 0)
        system("systemctl poweroff");
}

/* GET /api/events  — SSE stream */
typedef struct { int fd; } SseArg;
static void *sse_client_thread(void *arg)
{
    SseArg *a = (SseArg *)arg;
    int fd = a->fd; free(a);
    dprintf(fd,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n\r\n");
    /* Register client BEFORE pushing — otherwise broadcasts miss this fd */
    sse_add(fd);
    /* Send current state to all registered clients (includes this new one) */
    sse_push_rotors();
    sse_push_wind();
    sse_push_hist();
    /* Keep thread alive; SSE broadcasts come from unix_rx_thread.
     * Detect client disconnect via heartbeat. */
    while (g_running) {
        /* send keepalive comment every 15 s */
        sleep(15);
        if (send(fd, ": ka\n\n", 6, MSG_NOSIGNAL) < 0) break;
    }
    sse_remove(fd);
    close(fd);
    return NULL;
}

/* Refresh config from disk WITHOUT touching g_rotors live state.
 * Only updates g_cfg scalar fields (storm_enabled etc.) and preserves
 * the current rotor runtime state (has_data, current_az, target_az). */
static void cfg_refresh_from_disk(void)
{
    FILE *f = fopen(g_config_path, "r");
    if (!f) return;
    char line[MAX_LINE];
    int disk_num_rotors = -1;
    while (fgets(line, sizeof(line), f)) {
        int l = (int)strlen(line);
        while (l > 0 && (line[l-1]=='\n'||line[l-1]=='\r'||line[l-1]==' ')) line[--l]='\0';
        if (line[0]=='#' || line[0]=='\0') continue;
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = '\0';
        const char *k = line, *v = eq+1;
        if (!strcmp(k,"storm_enabled")) {
            g_cfg.storm_enabled = atoi(v);
        } else if (!strcmp(k,"num_rotors")) {
            disk_num_rotors = atoi(v);
            if (disk_num_rotors > MAX_ROTORS) disk_num_rotors = MAX_ROTORS;
            g_cfg.num_rotors = disk_num_rotors;
        } else {
            int idx = -1; char field[64];
            if (sscanf(k,"rotor%d_%63s",&idx,field)==2 && idx>=0 && idx<MAX_ROTORS) {
                WebRotorCfg *r = &g_cfg.rotors[idx];
                if (!strcmp(field,"name"))      snprintf(r->name,    sizeof(r->name),    "%s",v);
                if (!strcmp(field,"by_path"))   snprintf(r->by_path, sizeof(r->by_path), "%s",v);
                if (!strcmp(field,"protocol"))  snprintf(r->protocol,sizeof(r->protocol),"%s",v);
                if (!strcmp(field,"baud"))           r->baud            = atoi(v);
                if (!strcmp(field,"offset"))         r->offset          = atoi(v);
                if (!strcmp(field,"simulate"))       r->simulate        = atoi(v);
                if (!strcmp(field,"storm_enabled"))  r->storm_enabled   = atoi(v);
                if (!strcmp(field,"storm_offset"))   r->storm_offset    = atoi(v);
                if (!strcmp(field,"always_controllable")) r->always_controllable = atoi(v);
                if (!strcmp(field,"return_timeout_min"))  r->return_timeout_min  = atoi(v);
            }
        }
    }
    fclose(f);

    /* Extend g_num_rotors if disk has more rotors than runtime currently knows */
    if (disk_num_rotors > g_num_rotors) {
        pthread_mutex_lock(&g_state_lock);
        for (int i = g_num_rotors; i < disk_num_rotors; i++) {
            snprintf(g_rotors[i].name, sizeof(g_rotors[i].name),
                     "%s", g_cfg.rotors[i].name);
            g_rotors[i].has_data   = g_cfg.rotors[i].simulate ? 1 : 0;
            g_rotors[i].current_az = 0.0;
            g_rotors[i].target_az  = 0.0;
            g_rotors[i].moving     = 0;
        }
        g_num_rotors = disk_num_rotors;
        pthread_mutex_unlock(&g_state_lock);
    }
}

/* GET /api/config */
static void api_get_config(int fd)
{
    /* Refresh scalars from disk (storm_enabled, simulate, etc.)
     * without resetting live rotor state */
    cfg_refresh_from_disk();

    static char buf[16384];
    int len = config_to_json(buf, sizeof(buf));
    dprintf(fd,
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Content-Length: %d\r\nAccess-Control-Allow-Origin: *\r\n\r\n",len);
    send(fd, buf, (size_t)len, MSG_NOSIGNAL);
}

/* POST /api/config — requires admin auth */
static void api_post_config(int fd, const char *req)
{
    const char *tok = http_bearer(req);
    if (g_cfg.web_password[0] && !token_valid(tok)) {
        dprintf(fd,"HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n"); return;
    }
    const char *body = http_body(req);
    if (!body) { dprintf(fd,"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"); return; }

    /* Parse into a fresh copy of current config, then replace */
    WebConfig nc = g_cfg;
    json_to_config(body, &nc);

    /* Safety check: reject if parsed rotor count doesn't match num_rotors claim.
     * This catches truncated POST bodies (partial JSON) before they reach disk. */
    double claimed = json_field_d(body, "num_rotors");
    if (!JISNAN(claimed) && (int)claimed != nc.num_rotors) {
        const char *e = "{\"error\":\"rotor count mismatch — request body may be truncated\"}";
        dprintf(fd,"HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n"
                   "Content-Length: %zu\r\n\r\n%s", strlen(e), e);
        return;
    }

    /* storm_enabled is managed by the server (STORM ON/OFF); preserve it */
    nc.storm_enabled = g_cfg.storm_enabled;
    g_cfg = nc;

    if (cfg_write() != 0) {
        const char *e = "{\"error\":\"config write failed\"}";
        dprintf(fd,"HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n"
                   "Content-Length: %zu\r\n\r\n%s", strlen(e), e);
        return;
    }

    /* Reseed runtime rotor list */
    pthread_mutex_lock(&g_state_lock);
    g_num_rotors = g_cfg.num_rotors;
    for (int i = 0; i < g_num_rotors; i++) {
        snprintf(g_rotors[i].name, sizeof(g_rotors[i].name), "%s", g_cfg.rotors[i].name);
        /* Simulate rotors: mark as having data immediately so compass draws */
        if (g_cfg.rotors[i].simulate && !g_rotors[i].has_data) {
            g_rotors[i].has_data   = 1;
            g_rotors[i].current_az = 0;
            g_rotors[i].target_az  = 0;
        }
    }
    pthread_mutex_unlock(&g_state_lock);

    /* Check if restart is requested */
    double restart_val = json_field_d(body, "restart_server");
    if (!JISNAN(restart_val) && (int)restart_val == 1) {
        /* Restart server asynchronously so we can send response first */
        dprintf(fd,"HTTP/1.1 200 OK\r\nContent-Length: 19\r\n\r\n{\"ok\":1,\"restarted\":1}");
        fflush(NULL);
        system("sudo systemctl restart n1mm_rotor_server 2>/dev/null &");
        return;
    }

    dprintf(fd,"HTTP/1.1 200 OK\r\nContent-Length: 8\r\n\r\n{\"ok\":1}");
}

/* GET /api/devices — list /dev/serial/by-path */
static void api_devices(int fd, const char *req)
{
    const char *tok = http_bearer(req);
    if (g_cfg.web_password[0] && !token_valid(tok)) {
        dprintf(fd,"HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n"); return;
    }
    char buf[4096];
    int len = list_serial_devices(buf, sizeof(buf));
    dprintf(fd,
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Content-Length: %d\r\nAccess-Control-Allow-Origin: *\r\n\r\n", len);
    send(fd, buf, (size_t)len, MSG_NOSIGNAL);
}

/* POST /api/restart — restart n1mm_rotor_server service */
static void api_restart(int fd, const char *req)
{
    const char *tok = http_bearer(req);
    if (g_cfg.web_password[0] && !token_valid(tok)) {
        dprintf(fd,"HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n"); return;
    }
    dprintf(fd,"HTTP/1.1 200 OK\r\nContent-Length: 8\r\n\r\n{\"ok\":1}");
    fflush(NULL);
    system("sudo systemctl restart n1mm_rotor_server 2>/dev/null &");
}

/* ─── Server log tail → SSE ──────────────────────────────────────────────── */
static void *log_tail_thread(void *arg)
{
    (void)arg;
    FILE *f      = NULL;
    long  offset = 0;

    while (g_running) {
        usleep(1000000); /* check every 1 s */

        if (!g_cfg.logfile[0]) continue;

        /* (Re)open file */
        if (!f) {
            f = fopen(g_cfg.logfile, "r");
            if (!f) continue;
            fseek(f, 0, SEEK_END);
            offset = ftell(f);
        }

        /* Detect truncation (log cleared) */
        fseek(f, 0, SEEK_END);
        long end = ftell(f);
        if (end < offset) {
            fclose(f);
            f = fopen(g_cfg.logfile, "r");
            if (!f) { offset = 0; continue; }
            fseek(f, 0, SEEK_END);
            offset = ftell(f);
            end    = offset;
        }
        if (end <= offset) continue;

        /* Read and broadcast new lines */
        fseek(f, offset, SEEK_SET);
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            size_t len = strlen(line);
            if (len > 0 && line[len-1] == '\n') line[--len] = '\0';
            if (len == 0) continue;
            char esc[2048]; json_escape(line, esc, sizeof(esc));
            char data[2060];
            snprintf(data, sizeof(data), "{\"line\":\"%s\"}", esc);
            sse_broadcast("log", data);
        }
        offset = ftell(f);
    }
    if (f) fclose(f);
    return NULL;
}

/* GET /api/log?lines=N — last N lines of the server log file */
/* Parse "[YYYY-MM-DD HH:MM:SS] ..." → Unix timestamp (0 on failure) */
static time_t parse_log_ts(const char *line)
{
    if (!line || line[0] != '[') return 0;
    int yr, mo, dy, hr, mn, sc;
    if (sscanf(line, "[%d-%d-%d %d:%d:%d]",
               &yr, &mo, &dy, &hr, &mn, &sc) != 6) return 0;
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = yr - 1900; t.tm_mon = mo - 1; t.tm_mday = dy;
    t.tm_hour = hr;        t.tm_min  = mn;    t.tm_sec   = sc;
    t.tm_isdst = -1;
    return mktime(&t);
}

/* Helper: append one JSON-escaped log line to output buffer.
   Grows *out / *cap as needed.  Returns 0 on alloc failure. */
static int log_json_append(char **out, size_t *pos, size_t *cap,
                           const char *ln, int *first)
{
    size_t need = strlen(ln) * 2 + 8;   /* worst-case escaping */
    if (*pos + need > *cap) {
        size_t ncap = *cap + need + 65536;
        char *tmp = realloc(*out, ncap);
        if (!tmp) return 0;
        *out = tmp; *cap = ncap;
    }
    if (!*first) (*out)[(*pos)++] = ',';
    *first = 0;
    (*out)[(*pos)++] = '"';
    for (const char *p = ln; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if      (c == '"')  { (*out)[(*pos)++]='\\'; (*out)[(*pos)++]='"';  }
        else if (c == '\\') { (*out)[(*pos)++]='\\'; (*out)[(*pos)++]='\\'; }
        else if (c < 0x20)  { /* skip control chars */ }
        else                  (*out)[(*pos)++] = (char)c;
    }
    (*out)[(*pos)++] = '"';
    return 1;
}

/* GET /api/log  — two modes:
   ?since=<unix>  return all lines with timestamp >= since (up to 50 000)
   ?lines=N       return last N lines (ring-buffer, default 1000, max 50 000) */
static void api_log_get(int fd, const char *req)
{
    if (!g_cfg.logfile[0]) {
        dprintf(fd, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                    "Access-Control-Allow-Origin: *\r\nContent-Length: 2\r\n\r\n[]");
        return;
    }
    FILE *f = fopen(g_cfg.logfile, "r");
    if (!f) {
        dprintf(fd, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                    "Access-Control-Allow-Origin: *\r\nContent-Length: 2\r\n\r\n[]");
        return;
    }

    char  *out   = malloc(131072); /* 128 KB start, grows as needed */
    size_t pos   = 0, cap = 131072;
    if (!out) { fclose(f);
        dprintf(fd,"HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n"); return; }

    out[pos++] = '[';
    int first = 1;

    /* Parse ?since= */
    const char *ss = strstr(req, "since=");
    if (ss) {
        /* Mode 1: return all lines with ts >= since */
        time_t since = (time_t)atoll(ss + 6);
        int nfound = 0;
        char buf[2048];
        while (fgets(buf, sizeof(buf), f) && nfound < 50000) {
            size_t len = strlen(buf);
            if (len > 0 && buf[len-1] == '\n') buf[--len] = '\0';
            if (len == 0) continue;
            time_t ts = parse_log_ts(buf);
            if (ts < since) continue;
            if (!log_json_append(&out, &pos, &cap, buf, &first)) break;
            nfound++;
        }
    } else {
        /* Mode 2: ring-buffer — last N lines */
        int nlines = 1000;
        const char *ls = strstr(req, "lines=");
        if (ls) nlines = atoi(ls + 6);
        if (nlines < 1)     nlines = 1;
        if (nlines > 50000) nlines = 50000;

        char **ring = calloc((size_t)nlines, sizeof(char *));
        if (!ring) { free(out); fclose(f);
            dprintf(fd,"HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n"); return; }

        int idx = 0, total = 0;
        char buf[2048];
        while (fgets(buf, sizeof(buf), f)) {
            size_t len = strlen(buf);
            if (len > 0 && buf[len-1] == '\n') buf[--len] = '\0';
            if (len == 0) continue;
            free(ring[idx % nlines]);
            ring[idx % nlines] = strdup(buf);
            idx++; total++;
        }

        int actual = total < nlines ? total : nlines;
        int start  = total > nlines ? (idx % nlines) : 0;
        for (int i = 0; i < actual; i++) {
            char *ln = ring[(start + i) % nlines];
            if (!ln) continue;
            if (!log_json_append(&out, &pos, &cap, ln, &first)) break;
        }
        for (int i = 0; i < nlines; i++) free(ring[i]);
        free(ring);
    }
    fclose(f);

    if (pos + 4 > cap) { /* ensure room for closing bracket */
        char *tmp = realloc(out, cap + 4);
        if (tmp) { out = tmp; cap += 4; }
    }
    out[pos++] = ']';

    dprintf(fd,
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\nContent-Length: %zu\r\n\r\n", pos);
    send(fd, out, pos, MSG_NOSIGNAL);
    free(out);
}

/* POST /api/log/clear — truncate log file (admin auth required) */
static void api_log_clear(int fd, const char *req)
{
    const char *tok = http_bearer(req);
    if (g_cfg.web_password[0] && !token_valid(tok)) {
        dprintf(fd,"HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n"); return;
    }
    if (g_cfg.logfile[0]) {
        FILE *f = fopen(g_cfg.logfile, "w");
        if (f) fclose(f);
    }
    dprintf(fd,"HTTP/1.1 200 OK\r\nContent-Length: 8\r\nAccess-Control-Allow-Origin: *\r\n\r\n{\"ok\":1}");
}

#define HTTP_BUF_SIZE 65536
typedef struct { int fd; char req[HTTP_BUF_SIZE]; } ConnArg;
static void *http_conn_thread(void *arg)
{
    ConnArg *ca = (ConnArg *)arg;
    int fd = ca->fd;
    char *req = ca->req;

    /* Parse method and path */
    char method[8]="", path[256]="";
    sscanf(req, "%7s %255s", method, path);

    /* Strip query string */
    char *qs = strchr(path,'?'); if (qs) *qs='\0';

    /* Control-password gate: GOTO and STOP require a valid token when control password is set */
    if (g_cfg.web_view_password[0] &&
        (!strcmp(path,"/api/goto") || !strcmp(path,"/api/stop")) &&
        !token_valid(http_bearer(req))) {
        dprintf(fd,"HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n");
        free(ca); close(fd); return NULL;
    }

    if (!strcmp(path,"/") || !strcmp(path,"")) {
        /* Redirect to index.html */
        dprintf(fd,"HTTP/1.1 302 Found\r\nLocation: /index.html\r\nContent-Length: 0\r\n\r\n");
    } else if (!strcmp(path,"/api/events") && !strcmp(method,"GET")) {
        SseArg *sa = malloc(sizeof(SseArg)); sa->fd = fd;
        pthread_t t; pthread_attr_t attr;
        pthread_attr_init(&attr); pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
        pthread_create(&t,&attr,sse_client_thread,sa);
        pthread_attr_destroy(&attr);
        free(ca); return NULL; /* fd ownership transferred */
    } else if (!strcmp(path,"/api/status")  && !strcmp(method,"GET"))  {
        api_status(fd);
    } else if (!strcmp(path,"/api/hist")      && !strcmp(method,"GET"))  {
        api_hist(fd);
    } else if (!strcmp(path,"/api/hist-meteo") && !strcmp(method,"GET")) {
        api_hist_meteo(fd);
    } else if (!strcmp(path,"/api/config")  && !strcmp(method,"GET"))  {
        api_get_config(fd);
    } else if (!strcmp(path,"/api/config")  && !strcmp(method,"POST")) {
        api_post_config(fd, req);
    } else if (!strcmp(path,"/api/devices") && !strcmp(method,"GET"))  {
        api_devices(fd, req);
    } else if (!strcmp(path,"/api/restart") && !strcmp(method,"POST")) {
        api_restart(fd, req);
    } else if (!strcmp(path,"/api/login")   && !strcmp(method,"POST")) {
        api_login(fd, http_body(req));
    } else if (!strcmp(path,"/api/heartbeat") && !strcmp(method,"POST")) {
        api_heartbeat(fd, req);
    } else if (!strcmp(path,"/api/goto")    && !strcmp(method,"POST")) {
        api_goto(fd, req);
    } else if (!strcmp(path,"/api/stop")    && !strcmp(method,"POST")) {
        api_stop(fd, req);
    } else if (!strcmp(path,"/api/storm")    && !strcmp(method,"POST")) {
        api_storm(fd, req);
    } else if (!strcmp(path,"/api/set-password") && !strcmp(method,"POST")) {
        api_set_password(fd, req);
    } else if (!strcmp(path,"/api/log")       && !strcmp(method,"GET"))  {
        api_log_get(fd, req);
    } else if (!strcmp(path,"/api/log/clear") && !strcmp(method,"POST")) {
        api_log_clear(fd, req);
    } else if (!strcmp(path,"/api/reboot")   && !strcmp(method,"POST")) {
        api_reboot(fd, req);
    } else if (!strcmp(path,"/api/shutdown") && !strcmp(method,"POST")) {
        api_shutdown(fd, req);
    } else if (!strncmp(path,"/",1) && strcmp(method,"GET")==0) {
        serve_file(fd, path);
    } else {
        dprintf(fd,"HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n");
    }

    if (fd >= 0) close(fd);
    free(ca);
    return NULL;
}

static void *http_thread(void *arg)
{
    (void)arg;
    int srv = socket(AF_INET,SOCK_STREAM,0);
    if (srv < 0) { perror("socket"); return NULL; }
    int opt=1; setsockopt(srv,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    struct sockaddr_in sa; memset(&sa,0,sizeof(sa));
    sa.sin_family=AF_INET; sa.sin_addr.s_addr=INADDR_ANY;
    sa.sin_port=htons((uint16_t)g_cfg.web_port);
    if (bind(srv,(struct sockaddr *)&sa,sizeof(sa))<0) {
        perror("bind"); close(srv); return NULL;
    }
    listen(srv,16);
    fprintf(stdout,"Web interface listening on port %d\n",g_cfg.web_port);
    fflush(stdout);

    while (g_running) {
        struct timeval tv={1,0};
        fd_set rfds; FD_ZERO(&rfds); FD_SET(srv,&rfds);
        if (select(srv+1,&rfds,NULL,NULL,&tv)<=0) continue;
        int cli=accept(srv,NULL,NULL);
        if (cli<0) continue;
        struct timeval rto={5,0};
        setsockopt(cli,SOL_SOCKET,SO_RCVTIMEO,&rto,sizeof(rto));
        ConnArg *ca=malloc(sizeof(ConnArg));
        ca->fd=cli;
        memset(ca->req,0,sizeof(ca->req));
        /* Read until we have headers + full body */
        int total = 0;
        int hsz   = 0;   /* offset of body start (end of headers) */
        int clen  = 0;   /* Content-Length from header */
        while (total < HTTP_BUF_SIZE - 1) {
            int n = (int)recv(cli, ca->req + total, HTTP_BUF_SIZE - 1 - total, 0);
            if (n <= 0) break;
            total += n; ca->req[total] = '\0';
            /* Locate end-of-headers if not yet found */
            if (!hsz) {
                char *hend = strstr(ca->req, "\r\n\r\n");
                if (hend) {
                    hsz = (int)(hend - ca->req) + 4;
                    /* Extract Content-Length */
                    char *cl = strcasestr(ca->req, "Content-Length:");
                    if (cl) clen = atoi(cl + 15);
                }
            }
            /* Stop once headers + full body are received */
            if (hsz && total >= hsz + clen) break;
        }
        pthread_t t; pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
        pthread_create(&t,&attr,http_conn_thread,ca);
        pthread_attr_destroy(&attr);
    }
    close(srv);
    return NULL;
}

/* ─── Signal handler ─────────────────────────────────────────────────────── */
static void sig_handler(int s) { (void)s; g_running = 0; }

/* ─── main ───────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    const char *config = DEFAULT_CONFIG;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--config") && i+1<argc) config=argv[++i];
    }

    cfg_read(config);

    /* Init wind extended fields to NaN */
    g_wind.temp_c = g_wind.feels_like_c = g_wind.humidity_pct =
    g_wind.baro_hpa = g_wind.precip_mm = g_wind.uv_index = g_wind.solar_wm2 =
        (double)__builtin_nan("");

    pthread_t t_unix, t_http, t_sim, t_log;
    pthread_create(&t_unix, NULL, unix_rx_thread, NULL);
    pthread_create(&t_http, NULL, http_thread,    NULL);
    pthread_create(&t_sim,  NULL, sim_thread,     NULL);
    pthread_create(&t_log,  NULL, log_tail_thread, NULL);

    pthread_join(t_unix, NULL);
    pthread_join(t_http, NULL);
    return 0;
}
