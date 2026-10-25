/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Radio on fm1-base: the app's unity build (compiled into fm1-base's, after the platform: gfx.c in scope). The
 * pages (Home, the browser, WiFi, the networks around, a password typed on the keys, held HOME's help), the main
 * loop's work (input, the scan, the lights, the stream), Radio's own commands over USB, and the glue to the player
 * (radio.c), the places (rg.c) and the network side (net/radio_net.c, built apart against lwIP). */

/* ---- the platform as the radio's code knows it (fm1-radio's own platform before fm1-base) */
/* the WiFi states: fm1-base's, in the same order (WIFI_OFF .. WIFI_FAILED) */
enum { WS_OFF, WS_STARTING, WS_IDLE, WS_JOINING, WS_DHCP, WS_CONNECTED, WS_FAILED };
#define wifi_st ((uint8_t)plat_wifi_state())
#define wifi_rssi plat_wifi_rssi()
#define wifi_power(on) plat_wifi_on(on)
#define shim_log(s) plat_log(s)
#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#endif
extern void *malloc(unsigned long n);
extern void free(void *p);
#ifndef PLAT_HOST
extern int snprintf(char *buf, unsigned long sz, const char *f, ...);   /* (fm1-base's WiFi glue: ble/wifi) */
#endif

static int w_eq(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}
static void w_cpy(char *d, const char *s, unsigned max)
{
    unsigned i;
    for (i = 0; i + 1u < max && s && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}
/* what the screen shows of an SSID: the font is ASCII, so U+2019 (the iPhone's apostrophe) becomes ' */
static void ssid_show(char *d, const char *s, unsigned max)
{
    unsigned n = 0;
    while (*s && n + 1u < max) {
        if ((uint8_t)s[0] == 0xE2 && (uint8_t)s[1] == 0x80 && ((uint8_t)s[2] == 0x98 || (uint8_t)s[2] == 0x99)) {
            d[n++] = '\'';
            s += 3;
        } else {
            d[n++] = (uint8_t)*s >= 32 && (uint8_t)*s < 127 ? *s : '?';
            s++;
        }
    }
    d[n] = 0;
}
static const char *wifi_status_str(void)
{
    static const char *const S[] = {"OFF", "STARTING", "ON", "JOINING", "GETTING IP", "CONNECTED", "FAILED"};
    return wifi_st < 7u ? S[wifi_st] : "?";
}

#include "synth.c"
#include "sampler.c"
#include "sample_store.c"

/* ---- the station of the last session (OBJ_RADIO), and the synth's settings (saved before them: older saves
 * end at st_title, and load with the synth at its defaults) */
#define RSET_MAGIC 0x41505231u                    /* "RAD1" */
static struct {
    uint32_t magic;
    uint8_t playing, rsv;
    uint16_t country;
    char place[32], st_id[12], st_title[32];
    synth_set_t syn;
    int32_t near_lat,near_lon;
} rset;
static void rset_load(void)
{
    int n = plat_store_load(OBJ_RADIO, &rset, sizeof rset);
    int old = (int)((char *)&rset.syn - (char *)&rset);
    if (n < old || rset.magic != RSET_MAGIC) {
        memset(&rset, 0, sizeof rset);
        rset.magic = RSET_MAGIC;
        rset.playing = 1;
        n = 0;
    }
    if (n < (int)((char *)&rset.syn-(char *)&rset)+(int)sizeof rset.syn)
        rset.syn = SYN_DEFAULT;
    if(n < (int)sizeof rset || rset.near_lat < -900000 || rset.near_lat > 900000 ||
       rset.near_lon < -1800000 || rset.near_lon > 1800000) rset.rsv=0;
    rset.rsv &= 1u;
    syn = rset.syn;

}
static void rset_save(void) { plat_store_save(OBJ_RADIO, &rset, sizeof rset); }

/* ---- the networks around: fm1-base's scan, copied when a scan's results arrive */
#define SCAN_N 12
typedef struct { char ssid[33]; int8_t rssi; uint8_t secured, saved, channel; } scan_t;
static scan_t scan_list[SCAN_N];
static int scan_n = -1;                           /* -1: none yet */
static uint32_t scans_seen;
static void scan_refresh(void)
{
    if (plat_wifi_scans() == scans_seen)
        return;
    scans_seen = plat_wifi_scans();
    for (scan_n = 0; scan_n < SCAN_N; scan_n++) {
        scan_t *s = &scan_list[scan_n];
        int rssi, sec;
        if (!plat_wifi_found(scan_n, s->ssid, &rssi, &sec))
            break;
        s->rssi = (int8_t)rssi;
        s->secured = (uint8_t)sec;
        s->channel = (uint8_t)plat_wifi_found_channel(scan_n);
        s->saved = (uint8_t)plat_wifi_kept(s->ssid);
    }
}
/* a join asked while WiFi was still starting: made once it is on */
static char join_ssid[33], join_pass[64];
static uint8_t join_due, join_pass_given;
static void wifi_join_to(const char *ssid, const char *pass)
{
    w_cpy(join_ssid, ssid, sizeof join_ssid);
    w_cpy(join_pass, pass ? pass : "", sizeof join_pass);
    join_pass_given = pass != 0;
    join_due = 1;
    if (wifi_st == WS_OFF)
        wifi_power(1);
}
/* a network around: joined when its password is kept or it is open; -1 = it needs a password */
static int wifi_join_ssid(const char *ssid, int secured)
{
    if (secured && !plat_wifi_kept(ssid))
        return -1;
    wifi_join_to(ssid, 0);
    return 0;
}

/* ---- Radio's own commands over USB: F0 7D 57 <cmd> [data] F7 (tools/radio_ctl.py); the networks are fm1-base's
 * (F0 7D 4C 57: fm1_remote.py wifi ...). Answers: lines F0 7D 57 10 <text> F7, then F0 7D 57 11 F7.
 *   0x10 dump   0x40 n play (0 the stream, 1 fetched and dropped, 2 the built-in MP3)   0x41 stop   0x45 play/stop
 *   0x43 <url as nibbles> the stream's url (none: read it back)   0x46 level step: as a knob (0..3 K1..K4)
 *   0x47 n hold FX n (1..16; 0 let go)   0x48 bpm-40: the tempo   0x53 the places table */
static void wire_send(uint8_t code, const char *s)
{
    uint8_t m[100];
    uint32_t n = 0;
    m[n++] = 0xF0; m[n++] = 0x7D; m[n++] = 0x57; m[n++] = code;
    while (s && *s && n < sizeof m - 2u)
        m[n++] = (uint8_t)*s++ & 0x7Fu;
    m[n++] = 0xF7;
    plat_sysex_send(m, n);
}
static void wire_line(const char *s) { wire_send(0x10, s); }
static void wire_end(void) { wire_send(0x11, 0); }
static volatile uint8_t wire_cmd, wire_arg, wire_arg2;

static const uint8_t WHITE_KEY[16] = PLAT_WHITE_KEYS;   /* F3 .. G5 */
static uint32_t fx_wire;
static volatile uint8_t url_restart;
static uint32_t main_loops;

#include "radio.c"

/* tap tempo (FX on Home): the last taps, the tempo they make, when the screen stops showing it */
static uint32_t tap_t[4], tap_n, tempo_bpm = 120, tempo_t0, tempo_shown_until;
static void tempo_set(uint32_t beat_ms, uint32_t t0)
{
    if (beat_ms < 250u)
        beat_ms = 250u;                           /* 240 BPM */
    if (beat_ms > 1500u)
        beat_ms = 1500u;                          /* 40 BPM */
    fx_beat = beat_ms * 441u / 10u;
    tempo_bpm = (60000u + beat_ms / 2u) / beat_ms;
    tempo_t0 = t0;
    tempo_shown_until = plat_ms() + 2000u;
}
static void tempo_tap(uint32_t now)
{
    uint32_t n;
    if (tap_n && now - tap_t[(tap_n - 1u) % 4u] > 2000u)
        tap_n = 0;                                /* a pause: a new count */
    tap_t[tap_n % 4u] = now;
    tap_n++;
    n = tap_n < 4u ? tap_n : 4u;
    if (n >= 2u) {                                /* the mean of the last intervals */
        uint32_t first = tap_t[(tap_n - n) % 4u];
        tempo_set((now - first) / (n - 1u), now);
    } else {
        tempo_t0 = now;
        tempo_shown_until = now + 2000u;
    }
}

/* ---- the screen: FoMni's paper look; a row is drawn only when what it shows changes */
#define K_BG RGB(20, 42, 32)
#define K_PANEL RGB(36, 63, 48)
#define K_LINE RGB(74, 99, 75)
#define K_DIM RGB(181, 191, 161)
#define K_TEXT RGB(246, 239, 216)
#define K_CORAL RGB(205, 139, 91)
#define K_TEAL RGB(160, 187, 125)
#include "field_splash.c"

enum { PG_HOME, PG_BROWSE, PG_WIFI, PG_NETS, PG_PASS, PG_HELP, PG_SYNTH, PG_FAV, PG_REGION, PG_EFFECTS, PG_FILTER, PG_SPACE };
static uint8_t ui_page, ui_dirty = 1;
static int ui_sel, ui_top;
static uint32_t ui_scan_t;                        /* the scan asked for (ms), 0 = none waiting */
static char ui_msg[40];
#define UI_ROWS 14
#define UI_KEY 72                         /* a row's summary, to skip drawing it unchanged (72: the pool is full) */
static char ui_key[UI_ROWS][UI_KEY] __attribute__((section(".pool")));

static void ui_row(int slot, uint32_t y, uint32_t h, const char *l, const char *r, uint16_t lc, uint16_t rc,
                   uint16_t bg)
{
    char key[UI_KEY];
    int32_t rw;
    snprintf(key, sizeof key, "%u%s|%s|%x%x%x", (unsigned)y, l, r, lc, rc, bg);
    if (slot < UI_ROWS) {
        if (w_eq(ui_key[slot], key))
            return;
        memcpy(ui_key[slot], key, sizeof key);
    }
    cv_begin(240, h, bg);
    cv_text(16, (int32_t)(h - FONT_S.h) / 2, &FONT_S, l, lc);
    rw = text_w(&FONT_S, r);
    cv_text(224 - rw, (int32_t)(h - FONT_S.h) / 2, &FONT_S, r, rc);
    cv_blit(0, y);
}
static void ui_title(int slot, const char *t, const char *hint)
{
    char key[UI_KEY], shown[40];
    int battery = plat_battery();
    int room = 190 - 16 - text_w(&FONT_B, t) - 12;
    unsigned n;
    w_cpy(shown, hint, sizeof shown);
    n = (unsigned)strlen(shown);
    while (n && text_w(&FONT_S, shown) > room)
        shown[--n] = 0;
    snprintf(key, sizeof key, "T%d%s|%s", battery, t, shown);
    if (w_eq(ui_key[slot], key))
        return;
    memcpy(ui_key[slot], key, sizeof key);
    cv_begin(240, 36, K_BG);
    cv_text(16, 10, &FONT_B, t, K_TEXT);
    cv_text(190 - text_w(&FONT_S, shown), 10, &FONT_S, shown, K_DIM);
    if (battery == 4) {
        cv_text(202, 10, &FONT_XS, "USB", K_DIM);
    } else {
        unsigned bar;
        uint16_t color = battery == 0 ? K_CORAL : K_TEAL;
        cv_rect(203, 12, 22, 12, color);
        cv_rect(225, 15, 3, 6, color);
        cv_rect(204, 13, 20, 10, K_BG);
        if (battery < 0)
            cv_text(210, 10, &FONT_XS, "?", K_DIM);
        else for (bar = 0; bar < (unsigned)battery && bar < 3u; bar++)
            cv_rect(206 + bar * 6u, 15, 4, 6, color);
    }
    cv_rect(16, 34, 208, 1, K_LINE);
    cv_blit(0, 0);
}

static void ui_status(void)
{
    char ssid[33], v[24];
    uint16_t wc = wifi_st == WS_CONNECTED ? K_TEAL : wifi_st == WS_FAILED ? K_CORAL : K_TEXT;
    uint32_t ip = plat_wifi_ip();
    ui_title(0, "WiFi", "");
    ui_row(1, 48, 26, "WiFi", wifi_status_str(), K_DIM, wc, K_BG);
    ssid_show(ssid, plat_wifi_ssid()[0] ? plat_wifi_ssid() : "-", sizeof ssid);
    if (wifi_st == WS_OFF)
        ssid[0] = 0;
    ui_row(2, 74, 26, "Network", ssid, K_DIM, K_TEXT, K_BG);
    v[0] = 0;
    if (ip)
        snprintf(v, sizeof v, "%u.%u.%u.%u", (unsigned)(ip & 255u), (unsigned)(ip >> 8 & 255u),
                 (unsigned)(ip >> 16 & 255u), (unsigned)(ip >> 24));
    ui_row(3, 100, 26, "Address", wifi_st == WS_FAILED ? plat_wifi_why() : v, K_DIM,
           wifi_st == WS_FAILED ? K_CORAL : K_TEXT, K_BG);
    v[0] = 0;
    if (wifi_st == WS_CONNECTED)
        snprintf(v, sizeof v, "%d dBm", wifi_rssi);
    ui_row(4, 126, 26, "Signal", v, K_DIM, K_TEXT, K_BG);
    {
        char nm[40];
        uint16_t rc = radio_st == RS_PLAYING ? K_TEAL : radio_st == RS_ERROR ? K_CORAL : K_TEXT;
        if (radio_st == RS_PLAYING && radio_net_name[0])
            ssid_show(nm, radio_net_name, 22);
        else if (radio_st == RS_ERROR)
            w_cpy(nm, radio_why, sizeof nm);
        else
            w_cpy(nm, radio_status_str(), sizeof nm);
        ui_row(5, 152, 26, "Stream", nm, K_DIM, rc, K_BG);
    }
    ui_row(13, 214, 26, wifi_st == WS_OFF ? "PLAY wifi on  SEL networks" : "PLAY wifi off  SEL networks",
           "HOME", K_DIM, K_DIM, K_PANEL);
}

static void ui_nets(void)
{
    int i;
    ui_title(0, "Networks", "SEL  scan again");
    if (scan_n < 0) {
        ui_row(1, 40, 20, "Other...", "", K_DIM, K_CORAL, K_PANEL);   /* (a hidden network: no need to wait) */
        ui_row(2, 60, 20, ui_scan_t ? "Scanning..." : "", "", K_DIM, K_DIM, K_BG);
        for (i = 2; i < 7; i++)
            ui_row(1 + i, 40 + 20u * (uint32_t)i, 20, "", "", K_DIM, K_DIM, K_BG);
    } else {
        if (ui_sel > scan_n)
            ui_sel = scan_n;
        if (ui_sel < ui_top)
            ui_top = ui_sel;
        if (ui_sel >= ui_top + 7)
            ui_top = ui_sel - 6;
        for (i = 0; i < 7; i++) {
            int k = ui_top + i;
            char l[40], r[24], ss[33];
            uint16_t bg = K_BG, fg = K_TEXT;
            l[0] = r[0] = 0;
            if (k < scan_n) {
                scan_t *s = &scan_list[k];
                ssid_show(ss, s->ssid, 17);
                snprintf(l, sizeof l, "%s%s", s->saved ? "* " : "", ss);
                snprintf(r, sizeof r, "%d ch %u %s", s->rssi, s->channel, s->secured ? "lock" : "open");
                if (w_eq(s->ssid, plat_wifi_ssid()) && wifi_st == WS_CONNECTED)
                    fg = K_TEAL;
                if (k == ui_sel)
                    bg = K_PANEL;
            } else if (k == scan_n) {             /* a hidden network: its name typed */
                snprintf(l, sizeof l, "Other...");
                fg = K_DIM;
                if (k == ui_sel)
                    bg = K_PANEL;
            }
            ui_row(1 + i, 40 + 20u * (uint32_t)i, 20, l, r, fg, k == ui_sel ? K_CORAL : K_DIM, bg);
        }
    }
    ui_row(12, 184, 28, ui_msg, "", K_CORAL, K_DIM, K_BG);
    ui_row(13, 214, 26, "PLAY join  SAVE password", "HOME", K_DIM, K_DIM, K_PANEL);
}

/* ---- the password page: the 27 keys type (F3 = a ... E5 = z, G5 = delete); hold OCT- for capitals, OCT+ for
 * digits (F3 .. D4) and space / symbols (D#4 .. F#5) */
static char pw_ssid[33], pw_buf[64];
static int pw_len;
static uint8_t pw_other;                          /* Other...: 1 its name typed first, 2 then its password */
static uint32_t pw_keys;                          /* held, for the drawing */
static const char PW_SYM[] = " !@#$%&*-_.,?+/:";

static int pw_layer(uint32_t b)
{
    return (b & (1u << B_OCTUP)) ? 2 : (b & (1u << B_OCTDN)) ? 1 : 0;
}
static char pw_char(int k, int layer)
{
    if (k >= 26)
        return 0;                                 /* delete */
    if (layer == 2)
        return k < 10 ? (char)('0' + k) : PW_SYM[k - 10];
    return (char)((layer == 1 ? 'A' : 'a') + k);
}
static void pw_open(const char *ssid)             /* (a kept password is not shown: it is typed again) */
{
    w_cpy(pw_ssid, ssid, sizeof pw_ssid);
    pw_buf[0] = 0;
    pw_len = 0;
    pw_other = !ssid[0];
    ui_page = PG_PASS;
    ui_dirty = 1;
    ui_msg[0] = 0;
}
static void pw_key(int k, int layer)
{
    char c = pw_char(k, layer);
    if (!c) {
        if (pw_len)
            pw_buf[--pw_len] = 0;
    } else if (pw_len < (int)sizeof pw_buf - 1) {
        pw_buf[pw_len++] = c;
        pw_buf[pw_len] = 0;
    }
}

/* F3 .. G5 as a little piano: 16 white keys, 11 black, each with what it types now */
static void ui_piano(int slot, uint32_t y, int layer, uint32_t held)
{
    static const uint8_t BLACK[12] = {0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0};   /* from F */
    char key[UI_KEY];
    int k, wi = 0;
    const int kw = 14, x0 = 8, wh = 40, bh = 26;
    snprintf(key, sizeof key, "P%d%x", layer, (unsigned)held);
    if (w_eq(ui_key[slot], key))
        return;
    memcpy(ui_key[slot], key, sizeof key);
    cv_begin(240, (uint32_t)wh + (layer<0?0u:2u), K_BG);
    for (k = 0; k < 27; k++)                      /* white keys first, then the black ones over them */
        if (!BLACK[k % 12]) {
            char t[3] = {layer < 0 ? 0 : pw_char(k, layer), 0, 0};
            int x = x0 + wi * kw;
            if (!t[0] && layer >= 0)
                t[0] = '<';
            cv_rect(x, 0, kw - 1, wh, (held >> k & 1u) ? K_CORAL : RGB(255, 255, 255));
            cv_rect(x + kw - 1, 0, 1, wh, K_LINE);
            cv_text(x + (kw - 1 - text_w(&FONT_XS, t)) / 2, wh - 16, &FONT_XS, t,
                    (held >> k & 1u) ? RGB(255, 255, 255) : K_TEXT);
            wi++;
        }
    wi = 0;
    for (k = 0; k < 27; k++) {
        if (!BLACK[k % 12]) {
            wi++;
            continue;
        }
        {
            char t[2] = {layer < 0 ? 0 : pw_char(k, layer), 0};
            int x = x0 + wi * kw - 6;
            cv_rect(x, 0, 12, bh, (held >> k & 1u) ? K_CORAL : K_TEXT);
            cv_text(x + (12 - text_w(&FONT_XS, t)) / 2, 6, &FONT_XS, t, RGB(255, 255, 255));
        }
    }
    cv_blit(0, y);
}

static void ui_pass(void)
{
    char ss[33], shown[24];
    int layer = pw_layer(plat_buttons());
    ssid_show(ss, pw_other == 1 ? "a hidden network" : pw_ssid, 22);
    ui_title(0, pw_other == 1 ? "Network name" : "Password", "");
    ui_row(1, 40, 22, ss, "", K_DIM, K_DIM, K_BG);
    {   /* the end of what is typed, with a cursor */
        int from = pw_len > 21 ? pw_len - 21 : 0;
        snprintf(shown, sizeof shown, "%s_", pw_buf + from);
    }
    ui_row(2, 66, 30, shown, "", K_TEXT, K_DIM, K_PANEL);
    ui_row(3, 100, 20, layer == 1 ? "CAPITALS" : layer == 2 ? "DIGITS + SYMBOLS" : "letters",
           "OCT- caps  OCT+ 123", layer ? K_CORAL : K_DIM, K_DIM, K_BG);
    ui_piano(4, 128, layer, pw_keys);
    ui_row(12, 184, 28, ui_msg, "", K_CORAL, K_DIM, K_BG);
    ui_row(13, 214, 26, pw_other == 1 ? "PLAY  next" : "PLAY  save + join", "HOME  cancel", K_DIM, K_DIM, K_PANEL);
}

#include "aporee_ui.c"
#include "field_screen.c"

static void ui_page_set(int pg);

/* ARP edits the same temporary sample played by the main keyboard. */
static uint32_t sam_keys;
static int8_t sam_key=-1;
static void sampler_open(void)
{
    space_active=0;
    if(sam_state==SAM_EMPTY) {fx_mode=0;fx_held=fx_wire=0;(void)sam_restore();}
    sam_enter();
}
static void effects_open(void)
{
    int had_sample=sam_state!=SAM_EMPTY;
    space_active=0;
    sam_voice=sam_gate=0;sam_state=SAM_EMPTY;sam_len=0;
    sam_active=0;sam_keys=0;
    if(fx_hist) memset(fx_hist,0,FX_HIST*sizeof *fx_hist);
    fx_reset();fx_mode=1;ui_page_set(PG_EFFECTS);
    if(had_sample) w_cpy(ui_msg,"Sample cleared for FX",sizeof ui_msg);
}
static void ui_effects(void)
{
    char bpm[16];unsigned k;const char *name=ui_msg[0]?ui_msg:"White keys / hold for FX";
    for(k=0;k<FX_N;k++) if(fx_held>>k&1u) {name=FX_NAME[k];break;}
    snprintf(bpm,sizeof bpm,"%u BPM",(unsigned)tempo_bpm);
    ui_title(0,"Effects",bpm);
    ui_row(1,42,24,name,"",K_CORAL,K_DIM,K_BG);
    ui_row(2,70,24,"Keys 1-4","filter / crush",K_TEXT,K_DIM,K_BG);
    ui_row(3,94,24,"Keys 5-8","tempo stutters",K_TEXT,K_DIM,K_BG);
    ui_row(4,118,24,"Keys 9-12","tape / reverse / chop",K_TEXT,K_DIM,K_BG);
    ui_row(5,142,24,"Keys 13-16","ring / half / mute / lo-fi",K_TEXT,K_DIM,K_BG);
    ui_piano(6,174,-1,plat_keys());
    ui_row(13,214,26,"FX tap  PRESETS BPM","ENV back",K_DIM,K_DIM,K_PANEL);
}
static int sam_knob(volatile uint8_t *v,int32_t d)
{
    int x=(int)*v+2*d;
    if(!d) return 0;
    x=x<0?0:x>127?127:x;*v=(uint8_t)x;return 1;
}
#include "field_landscape.c"
static void ui_sampler(void)
{
    static const char *const labels[4]={"K1 START","K2 END","K3 PITCH","K4 LEVEL"};
    char a[20],b[20],c[20],d[20],status[40],mode[16];
    snprintf(mode,sizeof mode,"Slice / %u",(unsigned)sam_slices);
    ui_title(0,"Sampler",sam_loop==3?mode:sam_loop==2?"Loop latch":sam_loop?"Loop hold":"One shot");
    if(sam_state==SAM_CAPTURE) snprintf(status,sizeof status,"Capturing %u%%",(unsigned)(sam_len*100u/SAM_FRAMES));
    else if(ui_msg[0]) w_cpy(status,ui_msg,sizeof status);
    else if(sam_state==SAM_READY) snprintf(status,sizeof status,"Ready / %u ms",(unsigned)(sam_len*1000u/SAM_RATE));
    else w_cpy(status,ui_msg[0]?ui_msg:"REC captures / 2.7s mono",sizeof status);
    ui_row(1,40,28,status,"",sam_state==SAM_CAPTURE?K_CORAL:K_TEAL,K_DIM,K_BG);
    ui_row(4,68,20,"ALGO",sam_muted?"LIVE MUTED":"LIVE ON",K_DIM,sam_muted?K_CORAL:K_TEAL,K_BG);
    snprintf(a,sizeof a,"%u%%",(unsigned)(sam_start*100u/127u));
    snprintf(b,sizeof b,"%u%%",(unsigned)(sam_end*100u/127u));
    snprintf(c,sizeof c,"%+d st",sam_pitch+12*sam_oct);
    snprintf(d,sizeof d,"%u%%",(unsigned)(sam_level*100u/127u));
    ui_landscape(0);
    ui_knobs(labels,a,b,c,d);
    ui_row(13,214,26,sam_loop==3?"PRESETS slices":"REC capture  SEL mode","SAVE clip",K_DIM,K_DIM,K_PANEL);
}

static void ui_filter(void)
{
    static const char *const types[]={"Low-pass","High-pass","Band-pass"};
    static const char *const divisions[]={"8 beats","4 beats","2 beats","1 beat","1/2beat","1/4beat"};
    static const char *const labels[4]={"K1 CUT","K2 RES","K3 DEPTH","K4 RATE"};
    char v[24],a[16],b[16],c[16],d[16];unsigned rate=(unsigned)(fil_speed()*100.0f+0.5f);
    snprintf(v,sizeof v,"%u BPM",(unsigned)tempo_bpm);ui_title(0,"Filter",v);
    ui_row(1,42,24,fil_on?"ALGO / ON":"ALGO / BYPASS",types[fil_type],K_TEAL,K_CORAL,K_BG);
    ui_row(4,68,20,"Modulation",fil_sync?"SYNC":"FREE",K_DIM,K_TEAL,K_BG);
    snprintf(a,sizeof a,"%uHz",(unsigned)(fil_hz()+0.5f));
    snprintf(b,sizeof b,"%u%%",(unsigned)fil_res*100u/127u);
    snprintf(c,sizeof c,"%u%%",(unsigned)fil_depth*100u/127u);
    snprintf(d,sizeof d,"%u.%02uHz",rate/100u,rate%100u);
    ui_landscape(1);ui_knobs(labels,a,b,c,fil_sync?divisions[fil_division()]:d);
    ui_row(13,214,26,"SEL type  EDIT sync","LFO back",K_DIM,K_DIM,K_PANEL);
}

static void space_open(void)
{
    if(!fx_hist) {ap_fav_notice(7);return;}
    if(!space_active) {
        fx_mode=0;fx_held=fx_wire=0;sam_voice=sam_gate=sam_active=0;sam_state=SAM_EMPTY;sam_len=0;
        memset(fx_hist,0,FX_HIST*sizeof *fx_hist);space_reset();space_on=space_active=1;
    }
    ui_page_set(PG_SPACE);
}
static void ui_space(void)
{
    static const char *const labels[]={"K1 DELAY","K2 FEED","K3 ROOM","K4 MIX"};
    char a[16],b[16],c[16],d[16],bpm[16];
    snprintf(bpm,sizeof bpm,"%u BPM",(unsigned)tempo_bpm);ui_title(0,"Echo + Room",bpm);
    ui_row(1,42,24,"ALGO",space_on?"ON":"BYPASS",K_DIM,K_CORAL,K_BG);
    ui_row(4,68,20,"PRESETS BPM",space_sync?space_wanted()>3969?"SYNC / LIMIT":"SYNC":"FREE",K_DIM,K_TEAL,K_BG);
    snprintf(a,sizeof a,"%ums",space_frames()*1000u/11025u);
    snprintf(b,sizeof b,"%u%%",(unsigned)space_feedback*100u/127u);
    snprintf(c,sizeof c,"%u%%",(unsigned)space_room*100u/127u);
    snprintf(d,sizeof d,"%u%%",(unsigned)space_mix*100u/127u);
    ui_landscape(1);ui_knobs(labels,a,b,c,d);
    ui_row(13,214,26,"EDIT sync  FX tap","HOME back",K_DIM,K_DIM,K_PANEL);
}

static void ui_draw(void)
{
    if (ui_dirty) {
        int i;
        ui_dirty = 0;
        ap_globe_valid = 0;
        lcd_fill(0, 0, 240, 240, K_BG);
        for (i = 0; i < UI_ROWS; i++)
            ui_key[i][0] = 0;
    }
    switch (ui_page) {
    case PG_HOME: ui_home(); break;
    case PG_REGION: ui_home(); break;
    case PG_BROWSE: ui_regions(); break;
    case PG_FAV: ui_favorites(); break;
    case PG_WIFI: ui_status(); break;
    case PG_NETS: ui_nets(); break;
    case PG_PASS: ui_pass(); break;
    case PG_SYNTH: ui_sampler(); break;
    case PG_EFFECTS: ui_effects(); break;
    case PG_FILTER: ui_filter(); break;
    case PG_SPACE: ui_space(); break;
    default: ui_help(); break;
    }
}

static void ui_page_set(int pg)
{
    if(ui_page==PG_EFFECTS && pg!=PG_EFFECTS) {fx_mode=0;fx_held=0;fx_wire=0;}
    if(ui_page==PG_SYNTH && pg!=PG_SYNTH) {
        sam_exit();

    }
    if(ui_page==PG_FAV && pg!=PG_FAV) ap_fav_close();
    ui_page = (uint8_t)pg;
    ui_dirty = 1;
    ui_msg[0] = 0;
}

static void ui_scan(void)
{
    scan_n = -1;
    scans_seen = plat_wifi_scans();               /* the next scan's results */
    ui_msg[0] = 0;
    if (wifi_st == WS_OFF)
        wifi_power(1);                            /* the scan asks for the radio */
    ui_scan_t = plat_ms() ? plat_ms() : 1;
}

/* ---- Radio's commands over USB */
static void wire_dump(void)
{
    char b[96];
    snprintf(b, sizeof b, "up %u ms  wifi %s  loops %u", (unsigned)plat_ms(), wifi_status_str(), (unsigned)main_loops);
    wire_line(b);
    radio_ui_dump();
#ifndef PLAT_HOST
    {
        extern void shim_os_dump(void);
        shim_os_dump();                           /* each task, what it waits for, its deepest stack: into the log */
    }
    {
        extern uint32_t shim_heap_used, shim_heap_peak;
        extern const uint32_t shim_heap_size;
        snprintf(b, sizeof b, "heap %u used, %u peak, of %u", (unsigned)shim_heap_used, (unsigned)shim_heap_peak,
                 (unsigned)shim_heap_size);
        wire_line(b);
    }
#endif
    snprintf(b, sizeof b, "radio %s  net %d  buf %u/%u  pcm %u  fr %u  %uHz %ukbps", radio_status_str(),
             radio_net_state, (unsigned)(radio_wr - radio_rd), (unsigned)NET_RING, (unsigned)(pcm_w - pcm_r),
             (unsigned)radio_frames, (unsigned)radio_hz, (unsigned)radio_kbps);
    wire_line(b);
    snprintf(b, sizeof b, "  under %u  rebuf %u  reconn %u  %s%s", (unsigned)radio_underruns,
             (unsigned)radio_rebuffers, (unsigned)radio_reconnects,
             dec_kind == 1 ? "MP3 " : "", radio_why);
    wire_line(b);
    {   /* rates since the last dump: what came in, what was decoded, how often the main loop went round */
        static uint32_t t_last, wr_last, fr_last, loops_last;
        uint32_t dt = plat_ms() - t_last ? plat_ms() - t_last : 1u, spf;
        spf = 1152u;   /* samples a frame */
        snprintf(b, sizeof b, "  mode %u  in %u kbit/s  decoded %u fr/s (need %u, %u us, %u max)  loop %u/s", radio_mode,
                 (unsigned)((radio_wr - wr_last) * 8u / dt), (unsigned)((radio_frames - fr_last) * 1000u / dt),
                 (unsigned)(radio_hz / spf), radio_dec_n ? (unsigned)(radio_dec_us / radio_dec_n) : 0u,
                 (unsigned)radio_dec_max, (unsigned)((main_loops - loops_last) * 1000u / dt));
        wire_line(b);
        radio_dec_us = radio_dec_n = radio_dec_max = 0;
        t_last = plat_ms();
        wr_last = radio_wr;
        fr_last = radio_frames;
        loops_last = main_loops;
    }
}

int app_sysex(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    if(n>=4u && p[0]==0x7Du && p[1]==0x57u && p[2]==0x60u) {screen_command(p[3]);return 1;}
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x57u)
        return 0;
    switch (p[2]) {
    case 0x10:
        wire_dump();
        break;
    case 0x43:
        if (n <= 3u) {                            /* none: read it back */
            wire_line(radio_url);
        } else {                                  /* the stream's url, as nibbles */
            uint32_t k = 0;
            for (i = 3; i + 1u < n && k + 1u < sizeof radio_url_buf; i += 2)
                radio_url_buf[k++] = (char)((p[i] & 15u) << 4 | (p[i + 1] & 15u));
            radio_url_buf[k] = 0;
            /* as the station's own link (the tuning plays play_url): it plays until the next tune */
            w_cpy(play_url, k ? radio_url_buf : RADIO_URL, sizeof play_url);
            radio_url = play_url;
            radio_want = 1;
            url_restart = 1;                      /* the main loop restarts the stream on it */
        }
        break;
    case 0x54:                                    /* the places fetched again */
        wire_cmd = 0x54;
        break;
    case 0x53: radio_ui_dump(); break;
    default:                                      /* the main loop's */
        wire_arg = n > 3u ? p[3] : 0;
        wire_arg2 = n > 4u ? p[4] : 0;
        wire_cmd = p[2];
        break;
    }
    wire_end();
    return 1;
}

static void wire_actions(void)
{
    uint8_t cmd = wire_cmd;
    wire_cmd = 0;
    switch (cmd) {
    case 0x40: radio_play(wire_arg); break;
    case 0x41: radio_stop(); break;
    case 0x45: radio_toggle(); break;
    case 0x46:                                    /* as a knob: level 0..3 (K1..K4), a signed step */
        sel_turn(wire_arg & 3u, (int8_t)(wire_arg2 & 0x40u ? wire_arg2 | 0x80u : wire_arg2));
        break;
    case 0x47: fx_wire = wire_arg && wire_arg <= FX_N ? 1u << (wire_arg - 1u) : 0u; break;
    case 0x48: tempo_set(60000u / (wire_arg ? wire_arg + 40u : 120u), plat_ms()); break;
    case 0x54: ap_shuffle(); break;
    default: break;
    }
}

/* ---- the contract */
void app_init(void)
{
    /* Reserve the large stream buffers before WiFi's task allocations fragment the heap. */
    (void)radio_alloc();
    screen_stop();
    sam_init(fx_hist);
    (void)sam_restore();
    fil_init();
    space_active=0;space_reset();
    fx_mode=0;fx_held=0;
    rset_load();
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    (void)master_q12;                             /* (radio_render reads MASTER itself) */
    radio_render(lr, n);
}
void app_panic(void) { radio_st = RS_OFF;screen_stop(); }
void app_say(const char *a, const char *b)
{
    snprintf(ui_msg, sizeof ui_msg, "%s %s", a ? a : "", b ? b : "");
    ui_dirty = 1;
}

void app_ui_init(void)
{
    ap_fav_close(); /* also reload from simulated flash after a host reboot */
    if(ui_page==PG_FAV) ui_page=(uint8_t)ap_player_page();
    radio_ui_restore();                           /* the place and station of the last session, playing if it was */
    if (plat_wifi_saved(0, 0)) {                  /* a radio: WiFi on by itself when it knows a network */
        wifi_power(1);
    } else {                                      /* the first start: straight to the networks around */
        ui_page = PG_NETS;
        ui_scan();
        snprintf(ui_msg, sizeof ui_msg, "Pick your WiFi: SELECT, then PLAY");
    }
    ui_dirty = 1;
}

static uint32_t btn_prev;
static uint32_t fx_press_ms;
static uint8_t fx_pending;
static void input(void)
{
    uint32_t now = plat_buttons(), down = now & ~btn_prev, up=btn_prev&~now;
    int32_t turn = plat_enc(EN_SELECT);
    int32_t algo = plat_enc(EN_ALGO);
    int32_t presets = plat_enc(EN_PRESET);
    btn_prev = now;
    if(ui_page!=PG_HOME && ui_page!=PG_REGION) fx_pending=0;
    if(down&(1u<<B_FX)) {
        if(ui_page==PG_HOME || ui_page==PG_REGION) {fx_pending=1;fx_press_ms=plat_ms();}
        else {tempo_tap(plat_ms());fil_restart=1;if(ui_page==PG_FILTER) fil_sync=1;}
    }
    if(fx_pending) {
        if((now&(1u<<B_FX)) && plat_ms()-fx_press_ms>=450u) {fx_pending=0;space_open();return;}
        if(up&(1u<<B_FX)) {fx_pending=0;tempo_tap(fx_press_ms);fil_restart=1;}
    }
    if(presets && (ui_page==PG_EFFECTS || ui_page==PG_SPACE)) {
        int bpm=(int)tempo_bpm+presets;bpm=bpm<40?40:bpm>240?240:bpm;
        tempo_set((60000u+(unsigned)bpm/2u)/(unsigned)bpm,plat_ms());tap_n=0;fil_restart=1;
    }
    if(down&(1u<<B_LFO)) {fil_seen=1;ui_page_set(ui_page==PG_FILTER?ap_player_page():PG_FILTER);return;}
    /* Local octave/layer controls take precedence; elsewhere these always
     * request another sound without changing the screen being used. */
    if(ui_page!=PG_SYNTH && ui_page!=PG_PASS &&
       (down&((1u<<B_OCTDN)|(1u<<B_OCTUP)))) ap_shuffle();
    {   /* GLO: WiFi, from anywhere; HOME held: the controls */
        static uint32_t home_t;
        if (down & (1u << B_GLO))
            ui_page_set(ui_page == PG_WIFI ? ap_player_page() : PG_WIFI);
        if (now & (1u << B_HOME)) {
            if (down & (1u << B_HOME))
                home_t = plat_ms();
            else if (plat_ms() - home_t > 450u && ui_page != PG_HELP && ui_page != PG_PASS)
                ui_page_set(PG_HELP);
        } else if (ui_page == PG_HELP) {
            ui_page_set(ap_player_page());
            down &= ~(1u << B_HOME);
        }
    }
    {   /* One shared keyboard on the player and sample editor. */
        uint32_t keys=(ui_page==PG_HOME || ui_page==PG_REGION || ui_page==PG_SYNTH || ui_page==PG_FILTER)?plat_keys():0u;
        uint32_t on=keys&~sam_keys,off=sam_keys&~keys;int k;
        if(sam_key>=0 && (off>>(unsigned)sam_key&1u) && sam_loop==1) sam_gate=0;
        if(ui_page!=PG_HOME && ui_page!=PG_REGION && ui_page!=PG_SYNTH && ui_page!=PG_FILTER && sam_loop!=2) sam_gate=0;
        sam_keys=keys;
        for(k=0;k<NKEYS;k++) if(on>>k&1u) {
            if(sam_loop==2 && sam_voice && sam_key==k) sam_gate=0;
            else {sam_note(53+k);sam_key=(int8_t)k;}
        }
    }
    {
        uint32_t keys=ui_page==PG_EFFECTS?plat_keys():0u,h=0;int w;
        for(w=0;w<16;w++) if(keys>>WHITE_KEY[w]&1u) h|=1u<<w;
        fx_held=ui_page==PG_EFFECTS?h|fx_wire:0u;
    }
    if (ui_page == PG_HOME || ui_page == PG_REGION) {
        if(down&(1u<<B_ENV)) {effects_open();return;}
        if (down & (1u << B_PLAY))
            radio_toggle();
        if (down & (1u << B_ARP)) {
            if(!fx_hist) {ap_fav_notice(7);return;}
            sampler_open();sam_keys=0;sam_key=-1;
            ui_page_set(PG_SYNTH);
            return;
        }
        if (down & (1u << B_SEL)) ap_info = !ap_info;
        if (down & (1u << B_EDIT)) ap_near_toggle();
        if (down & (1u << B_SAVE)) {ap_regions_open();ui_page_set(PG_BROWSE);}
        if (down & (1u << B_REC)) ap_fav_save();
        if ((down & (1u << B_SEQ)) && ap_fav_open()) {ui_page_set(PG_FAV);return;}
        if (ui_page == PG_REGION && (down & (1u << B_HOME))) {
            ap_near_leave();
            ap_active_region=0;ap_shuffle();ui_page_set(PG_HOME);
        }
        sel_turn(3, plat_enc(EN_K4) + turn);
        (void)plat_enc(EN_K1); (void)plat_enc(EN_K2); (void)plat_enc(EN_K3);
    } else if (ui_page == PG_BROWSE) {
        ap_regions_input(down,turn);
    } else if (ui_page == PG_FAV) {
        ap_fav_input(down,turn);
    } else if (ui_page == PG_WIFI) {
        if (down & (1u << B_PLAY))
            wifi_power(wifi_st == WS_OFF);
        if (down & (1u << B_SEL)) {
            ui_page_set(PG_NETS);
            ui_scan();
        }
        if (down & (1u << B_HOME))
            ui_page_set(ap_player_page());
    } else if (ui_page == PG_NETS) {
        if (turn && scan_n >= 0) {
            ui_sel += turn;
            if (ui_sel < 0)
                ui_sel = 0;
            if (ui_sel > scan_n)
                ui_sel = scan_n;              /* (the last row: Other...) */
        }
        if (down & (1u << B_SEL))
            ui_scan();
        if (down & (1u << B_HOME))
            ui_page_set(PG_WIFI);
        if ((down & (1u << B_PLAY | 1u << B_SAVE)) && (scan_n < 0 || ui_sel >= scan_n)) {
            pw_open("");                          /* Other...: a hidden network's name, then its password */
        } else if ((down & (1u << B_PLAY)) && scan_n > 0) {
            scan_t *s = &scan_list[ui_sel];
            if (wifi_join_ssid(s->ssid, s->secured) == 0)
                ui_page_set(PG_WIFI);
            else
                pw_open(s->ssid);
        } else if ((down & (1u << B_SAVE)) && scan_n > 0) {
            pw_open(scan_list[ui_sel].ssid);
        }
    } else if (ui_page == PG_PASS) {
        uint32_t keys = plat_keys(), kdown = keys & ~pw_keys;
        int k;
        pw_keys = keys;
        for (k = 0; k < 27; k++)
            if (kdown >> k & 1u)
                pw_key(k, pw_layer(now));
        if (down & (1u << B_HOME))
            ui_page_set(PG_NETS);
        if ((down & (1u << B_PLAY)) && pw_other == 1) {
            if (pw_len) {                         /* the name: now its password */
                w_cpy(pw_ssid, pw_buf, sizeof pw_ssid);
                pw_buf[0] = 0;
                pw_len = 0;
                pw_other = 2;
                ui_msg[0] = 0;
                ui_dirty = 1;
            }
        } else if (down & (1u << B_PLAY)) {
            if (pw_len < 8 && !(pw_other && !pw_len)) {   /* (a hidden network may be open: no password) */
                snprintf(ui_msg, sizeof ui_msg, "WPA passwords are 8+ characters");
            } else {
                wifi_join_to(pw_ssid, pw_buf);    /* kept once it joins */
                ui_page_set(PG_WIFI);
            }
        }
    } else if (ui_page == PG_SYNTH) {
        int x;
        if(presets && sam_loop==3) {
            static const uint8_t counts[]={4,8,16,27};int i=0;
            while(i<3 && counts[i]!=sam_slices) i++;
            i+=presets;i=i<0?0:i>3?3:i;sam_slices=counts[i];sam_slice%=sam_slices;sam_voice=sam_gate=0;
        }
        if(algo) sam_muted=algo<0;
        if(down&(1u<<B_ENV)) {effects_open();return;}
        sam_knob(&sam_start,plat_enc(EN_K1));
        if(sam_start>125) sam_start=125;
        sam_knob(&sam_end,plat_enc(EN_K2));
        if(sam_end<=sam_start) sam_end=sam_start+1u;
        x=sam_pitch+plat_enc(EN_K3);sam_pitch=(int8_t)(x < -24?-24:x>24?24:x);
        sam_knob(&sam_level,plat_enc(EN_K4)+turn);
        if((down&(1u<<B_OCTDN)) && sam_oct>-2) sam_oct--;
        if((down&(1u<<B_OCTUP)) && sam_oct<2) sam_oct++;
        if(down&(1u<<B_SEL)) {
            sam_loop=(sam_loop+1u)%4u;
            if(sam_loop==0 || sam_loop==3) sam_voice=sam_gate=0;
        }
        if(down&(1u<<B_REC)) {
            if(sam_state==SAM_CAPTURE || radio_st==RS_PLAYING) {ui_msg[0]=0;sam_record();}
            else w_cpy(ui_msg,"Wait for PLAYING",sizeof ui_msg);
        }
        if(down&(1u<<B_SAVE)) {
            int rc;sam_finish();rc=sam_save();
            w_cpy(ui_msg,rc==-2?"Capture a sample first":rc?"Sample save failed":"Sample saved",sizeof ui_msg);
        }
        if(down&(1u<<B_PLAY)) {
            if(sam_voice) sam_gate=0;
            else {sam_note(sam_loop==3?53+sam_slice:60);sam_key=-1;}
        }
        if(down&((1u<<B_HOME)|(1u<<B_ARP))) ui_page_set(ap_player_page());
    } else if(ui_page==PG_FILTER) {
        if(algo) fil_on=algo>0;
        sam_knob(&fil_cut,plat_enc(EN_K1));sam_knob(&fil_res,plat_enc(EN_K2));
        sam_knob(&fil_depth,plat_enc(EN_K3));sam_knob(&fil_rate,plat_enc(EN_K4)+turn);
        if(down&(1u<<B_SEL)) fil_type=(fil_type+1u)%3u;
        if(down&(1u<<B_EDIT)) {fil_sync=!fil_sync;fil_restart=1;}
        if(down&(1u<<B_HOME)) ui_page_set(ap_player_page());
        if(down&(1u<<B_PLAY)) radio_toggle();
        if(down&(1u<<B_REC)) ap_fav_save();
        if((down&(1u<<B_SEQ)) && ap_fav_open()) ui_page_set(PG_FAV);
        if(down&(1u<<B_ARP)) {sampler_open();ui_page_set(PG_SYNTH);}
        if(down&(1u<<B_ENV)) effects_open();
    } else if(ui_page==PG_SPACE) {
        if(algo) space_on=algo>0;
        sam_knob(&space_time,plat_enc(EN_K1));sam_knob(&space_feedback,plat_enc(EN_K2));
        sam_knob(&space_room,plat_enc(EN_K3));sam_knob(&space_mix,plat_enc(EN_K4)+turn);
        if(down&(1u<<B_EDIT)) space_sync=!space_sync;
        if(down&(1u<<B_PLAY)) radio_toggle();
        if(down&(1u<<B_REC)) ap_fav_save();
        if((down&(1u<<B_SEQ)) && ap_fav_open()) ui_page_set(PG_FAV);
        if(down&(1u<<B_HOME)) ui_page_set(ap_player_page());
        if(down&(1u<<B_ENV)) effects_open();
        if(down&(1u<<B_ARP)) {sampler_open();ui_page_set(PG_SYNTH);}
    } else if(ui_page==PG_EFFECTS) {
        if(down&(1u<<B_REC)) ap_fav_save();
        if((down&(1u<<B_SEQ)) && ap_fav_open()) {ui_page_set(PG_FAV);return;}
        if(down&(1u<<B_PLAY)) radio_toggle();
        if(down&((1u<<B_ENV)|(1u<<B_HOME))) ui_page_set(ap_player_page());
        if(down&(1u<<B_ARP)) {sampler_open();ui_page_set(PG_SYNTH);}
    }
}
void app_input_only(void) { input(); }

#ifdef PLAT_HOST
int app_host_cmd(const char *cmd,const char *a,const char *b,const char *c,const char *d)
{
    (void)a;(void)b;(void)c;(void)d;
    if(!strcmp(cmd,"splash")) {app_splash();return 1;}
    if(!strcmp(cmd,"screen_mem")) {host_screen_available=(uint32_t)atoi(a);return 1;}
    if(!strcmp(cmd,"screen")) {
        if(!strcmp(a,"on")) {char path[512];snprintf(path,sizeof path,"%s/%s",plat_host_dir,b);host_screen_file(path);screen_command(2);}
        else if(!strcmp(a,"off")) {screen_command(0);host_screen_file(0);}
        else if(!strcmp(a,"refresh")) screen_command(2);
        else if(!strcmp(a,"ping")) screen_command(1);
        return 1;
    }
    return 0;
}
int app_expect(const char *what, int *got)       /* the host simulator's checks (the .host scripts in tests) */
{
    int i, n = 0;
    if (!strcmp(what,"tempo_bpm")) *got=(int)tempo_bpm;
    else if (!strcmp(what,"fx_len")) *got=(int)fx.len;
    else if (!strcmp(what,"screen_on")) *got=screen!=0;
    else if (!strcmp(what,"sam_muted")) *got=sam_muted;
    else if (!strcmp(what,"sam_slices")) *got=sam_slices;
    else if (!strcmp(what,"fil_on")) *got=fil_seen && fil_on;
    else if (!strcmp(what,"fil_type")) *got=fil_type;
    else if (!strcmp(what,"fil_depth")) *got=fil_depth;
    else if (!strcmp(what,"fil_rate")) *got=fil_rate;
    else if (!strcmp(what,"fil_sync")) *got=fil_sync;
    else if (!strcmp(what,"space_active")) *got=space_active;
    else if (!strcmp(what,"space_on")) *got=space_on;
    else if (!strcmp(what,"space_sync")) *got=space_sync;
    else if (!strcmp(what,"space_mix")) *got=space_mix;
    else if (!strcmp(what,"screen_bytes")) *got=sizeof(field_screen_t);
    else if (!strcmp(what,"screen_drops")) *got=screen?(int)screen->drops:0;
    else if (!strcmp(what, "page"))
        *got = ui_page;
    else if (!strcmp(what, "ap_lat"))
        *got = ap_now.lat;
    else if (!strcmp(what, "ap_favorites"))
        *got = ap_favs ? ap_favs->count : -1;
    else if (!strcmp(what, "ap_fav_status"))
        *got = ap_fav_status;
    else if (!strcmp(what, "ap_fav_cursor"))
        *got = ap_fav_cursor;
    else if (!strcmp(what, "ap_sound"))
        *got = atoi(ap_now.id);
    else if (!strcmp(what, "ap_url_matches"))
        *got = !strcmp(play_url,ap_now.url);
    else if (!strcmp(what, "ap_near"))
        *got = rset.rsv;
    else if (!strcmp(what, "ap_near_lat"))
        *got = rset.near_lat;
    else if (!strcmp(what, "ap_want"))
        *got = radio_want;
    else if (!strcmp(what, "ap_completed"))
        *got = ap_test_completed;
    else if (!strcmp(what, "ap_frames"))
        *got = ap_test_frames;
    else if (!strcmp(what, "ap_globe_draws"))
        *got = ap_test_globe_draws;
    else if (!strcmp(what, "ap_refills"))
        *got = radio_rebuffers;
    else if (!strcmp(what, "ap_playing"))
        *got = radio_st == RS_PLAYING;
    else if (!strcmp(what, "ap_ring"))
        *got = NET_RING;
    else if (!strcmp(what, "ap_pcm"))
        *got = PCM_FRAMES;
    else if (!strcmp(what, "ap_region"))
        *got = ap_active_region;
    else if (!strcmp(what, "ap_browse_level"))
        *got = ap_browse_level;
    else if (!strcmp(what,"sam_active")) *got=sam_active;
    else if (!strcmp(what,"sam_state")) *got=sam_state;
    else if (!strcmp(what,"sam_len")) *got=(int)sam_len;
    else if (!strcmp(what,"sam_voice")) *got=sam_voice;
    else if (!strcmp(what,"sam_loop")) *got=sam_loop;
    else if (!strcmp(what,"sam_pitch")) *got=sam_pitch;
    else if (!strcmp(what,"sam_oct")) *got=sam_oct;
    else if (!strcmp(what,"sam_slice")) *got=sam_slice;
    else if (!strcmp(what,"fx_mode")) *got=fx_mode;
    else if (!strcmp(what, "syn_wave"))
        *got = syn.wave;
    else if (!strcmp(what, "syn_cutoff"))
        *got = syn.cutoff;
    else if (!strcmp(what, "syn_attack"))
        *got = syn.attack;
    else if (!strcmp(what, "syn_release"))
        *got = syn.release;
    else if (!strcmp(what, "syn_level"))
        *got = syn.level;
    else if (!strcmp(what, "syn_octave"))
        *got = syn.octave;
    else if (!strcmp(what, "syn_voices")) {
        for (i = 0; i < SYN_VOICES; i++)
            n += syv[i].on;
        *got = n;
    } else
        return 0;
    return 1;
}
#endif

/* light what does something here: the page's buttons, and the keys while they type */
static void lights(void)
{
    uint32_t lb = 0, lk = 0;
    if (ui_page == PG_HOME || ui_page == PG_REGION) {
        lb = 1u << B_PLAY | 1u << B_GLO | 1u << B_SEL | 1u << B_SAVE | 1u << B_REC | 1u << B_SEQ | 1u << B_EDIT | 1u << B_OCTDN | 1u << B_OCTUP;
        if(ui_page==PG_REGION) lb|=1u<<B_HOME;
        lb|=1u<<B_ARP|1u<<B_ENV;
        if(sam_state==SAM_READY) lk=((1u<<NKEYS)-1u)&~sam_keys;
    } else if (ui_page == PG_BROWSE) {
        lb = 1u << B_SEL | 1u << B_HOME | 1u << B_PLAY;
    } else if (ui_page == PG_FAV) {
        lb = 1u << B_HOME | 1u << B_SEQ | 1u << B_GLO;
        if(ap_favs && ap_favs->count) lb |= 1u << B_SEL | 1u << B_PLAY | 1u << B_REC;
    } else if (ui_page == PG_WIFI) {
        lb = 1u << B_PLAY | 1u << B_SEL | 1u << B_HOME | 1u << B_GLO;
    } else if (ui_page == PG_NETS) {
        lb = 1u << B_SEL | 1u << B_HOME | 1u << B_PLAY | 1u << B_SAVE;
    } else if (ui_page == PG_PASS) {
        lb = 1u << B_PLAY | 1u << B_HOME | 1u << B_OCTDN | 1u << B_OCTUP;
        lk = (1u << NKEYS) - 1u;
    }
    if (ui_page == PG_SYNTH) {
        lb = 1u << B_HOME | 1u << B_ARP | 1u << B_OCTDN | 1u << B_OCTUP | 1u << B_SEL | 1u << B_PLAY | 1u << B_REC;
        lb|=1u<<B_ENV|1u<<B_SAVE;
        lk = ((1u << NKEYS) - 1u) & ~sam_keys;    /* the keys lit, the held ones off */
    }
    if(ui_page==PG_EFFECTS) {
        lb=1u<<B_ENV|1u<<B_HOME|1u<<B_ARP|1u<<B_PLAY|1u<<B_FX|1u<<B_GLO|1u<<B_REC|1u<<B_SEQ;
        lk=((1u<<NKEYS)-1u)&~plat_keys();
    }
    if(ui_page==PG_FILTER || ui_page==PG_SPACE) {
        lb=1u<<B_HOME|1u<<B_SEL|1u<<B_EDIT|1u<<B_PLAY|1u<<B_REC|1u<<B_SEQ|1u<<B_ARP|1u<<B_ENV;
        if(sam_state==SAM_READY) lk=((1u<<NKEYS)-1u)&~sam_keys;
    }
    lb|=1u<<B_OCTDN|1u<<B_OCTUP|1u<<B_LFO;
    lb&=~(1u<<B_FX);
    if((plat_ms()-tempo_t0)%(60000u/tempo_bpm)<90u) lb|=1u<<B_FX;
    plat_leds(lb, lk);
}

void app_frame(void)
{
    static uint32_t last_draw;
    input();
    wire_actions();
    scan_refresh();
    if (join_due && wifi_st >= WS_IDLE && wifi_st != WS_STARTING) {   /* a join asked while WiFi started */
        join_due = 0;
        plat_wifi_join(join_ssid, join_pass_given ? join_pass : 0);
    }
    /* the scan: asked once the radio is up; done when a scan's results arrive */
    if (ui_scan_t) {
        static uint8_t asked;
        if (wifi_st >= WS_IDLE && !asked) {
            asked = 1;
            plat_wifi_scan();
        }
        if (scan_n >= 0) {
            ui_scan_t = 0;
            asked = 0;
        } else if (plat_ms() - ui_scan_t > 20000u) {
            ui_scan_t = 0;
            asked = 0;
            scan_n = 0;
            snprintf(ui_msg, sizeof ui_msg, "No answer: SEL to scan again");
        }
    }
    lights();
    if (plat_ms() - last_draw >= 100u) {
        last_draw = plat_ms();
        ui_draw();
    }
}

/* every pass of the main loop: the stream decoded, the tuning */
#ifdef PLAT_HOST
static void host_net_poll(void);
#endif
void app_loop(void)
{
    main_loops++;
    screen_step();
#ifdef PLAT_HOST
    host_net_poll();
#endif
    if (url_restart) {                            /* a new url over USB: the stream again, from it */
        url_restart = 0;
        if (radio_st != RS_OFF)
            radio_stop();                         /* radio_ui_service starts it again (the station is unchanged) */
    }
    radio_ui_service();
    if (wifi_st != WS_CONNECTED && radio_mode != 2 && radio_st != RS_OFF && radio_st != RS_ERROR)
        radio_stop();                             /* the network went: so does the stream */
    radio_poll();
}

#ifdef PLAT_HOST
/* Host UI fixture, explicitly simulated. Device uses net/aporee_net.c. */
#include "regions.c"
volatile int radio_net_state, radio_net_aac;
volatile uint32_t radio_rd, radio_wr;
char radio_net_why[32], radio_net_name[48];
volatile int ap_state;
volatile uint32_t ap_bytes;
char ap_why[32];
ap_track_t ap_track;
static unsigned host_pick;
static uint32_t host_metadata_at;
static FILE *host_stream;
static uint8_t *host_ring;
static uint32_t host_ring_size, host_stream_t0, host_stream_tick;
static uint32_t host_window_tick, host_window_used;
static void host_net_poll(void)
{
    uint32_t now=plat_ms(),room,n,at,elapsed;
    const char *stall;
    if(ap_state==AP_BUSY && (int32_t)(now-host_metadata_at)>=0) ap_state=AP_DONE;
    if(!host_stream || now==host_stream_tick) return;
    host_stream_tick=now;elapsed=now-host_stream_t0;
    stall=getenv("APOREE_TEST_STALL");
    if(stall && elapsed>=5000u && elapsed<5000u+(uint32_t)atoi(stall)) return;
    room=host_ring_size-(radio_wr-radio_rd);at=radio_wr%host_ring_size;
    n=host_ring_size-at;if(n>room)n=room;if(n>48u)n=48u;
    { /* Optional window/RTT model: regression for a bandwidth-delay cap. This
       * does not simulate lwIP, retransmits, or actual WiFi/TLS performance. */
        const char *window=getenv("APOREE_TEST_WINDOW"), *rtt=getenv("APOREE_TEST_RTT");
        if(window && rtt) {
            uint32_t bytes=(uint32_t)atoi(window), ms=(uint32_t)atoi(rtt);
            if(ms && now-host_window_tick>=ms) {host_window_tick=now;host_window_used=0;}
            if(host_window_used>=bytes) return;
            if(n>bytes-host_window_used)n=bytes-host_window_used;
        }
    }
    if(n) {
        unsigned got=(unsigned)fread(host_ring+at,1,n,host_stream);
        radio_wr+=got;
        host_window_used+=got;
        if(!got) {fclose(host_stream);host_stream=0;radio_net_state=RN_ENDED;}
    }
}
int ap_start(uint16_t region) {
    (void)region;
    memset(&ap_track,0,sizeof ap_track);
    snprintf(ap_track.url,sizeof ap_track.url,"https://aporee.org/maps/files/host-fixture-%u.mp3",host_pick+1);
    snprintf(ap_track.id,sizeof ap_track.id,"%u",host_pick+1);
    snprintf(ap_track.title,sizeof ap_track.title,"SIMULATED recording %u",host_pick+1);
    w_cpy(ap_track.place,host_pick++%2?"Tokyo, Japan":"Berlin, Germany",sizeof ap_track.place);
    ap_track.lat=host_pick%2?525200:356800; ap_track.lon=host_pick%2?134050:1396900;
    w_cpy(ap_track.artist,"Host test fixture",sizeof ap_track.artist);
    w_cpy(ap_track.licence,"SIMULATED",sizeof ap_track.licence);
    {
        const char *delay=getenv("APOREE_TEST_METADATA_DELAY");
        host_metadata_at=plat_ms()+(delay?(unsigned)atoi(delay):0u);
        ap_state=delay?AP_BUSY:AP_DONE;
    }
    return 0;
}
int ap_start_near(int32_t lat,int32_t lon)
{
    int rc=ap_start(0);
    ap_track.lat=lat+100;ap_track.lon=lon+100;
    return rc;
}
int radio_net_start(const char *u,uint8_t *b,uint32_t n) {
    const char *fixture=getenv("APOREE_TEST_MP3");
    const char *stream=getenv("APOREE_TEST_STREAM");
    (void)u;
    if(stream) {
        host_stream=fopen(stream,"rb");
        if(host_stream) {
            host_ring=b;host_ring_size=n;radio_rd=radio_wr=0;
            host_stream_t0=host_stream_tick=host_window_tick=plat_ms();host_window_used=0;radio_net_state=RN_STREAMING;return 0;
        }
    }
    if(fixture) {
        FILE *f=fopen(fixture,"rb");
        if(f) {
            radio_rd=0;radio_wr=(uint32_t)fread(b,1,n,f);fclose(f);
            radio_net_state=RN_ENDED;return 0;
        }
    }
    radio_net_state=RN_ERROR;
    w_cpy(radio_net_why,"host: no audio network",sizeof radio_net_why);return 0;
}
void radio_net_stop(void) {if(host_stream)fclose(host_stream);host_stream=0;}
int radio_net_running(void) {return host_stream != 0;}
int radio_net_tls(void) {return 0;}
#endif
