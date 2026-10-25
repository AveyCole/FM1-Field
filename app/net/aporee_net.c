/* SPDX-License-Identifier: MPL-2.0 */
/* Included after radio_net.c's HTTP transport. One TLS connection at a time.
 * A reservoir of 32 real locations is refreshed after it is used up. Shuffles
 * within the reservoir need only a small playlist request, not the world index. */
#include "../aporee.h"
#include "../aporee_json.h"
#include "../regions.c"
volatile int ap_state;
volatile uint32_t ap_bytes;
char ap_why[32];
ap_track_t ap_track __attribute__((section(".pool")));
typedef struct { uint32_t id; int32_t lat, lon; } ap_loc_t;
static ap_loc_t *ap_locs, *ap_world_locs; /* two 32-location caches, allocated together */
static unsigned ap_world_n;
static uint16_t ap_cache_region;
static unsigned ap_n, ap_seen;
static uint32_t ap_rng, ap_last;
static unsigned ap_rand(unsigned n)
{
    ap_rng ^= ap_rng << 13; ap_rng ^= ap_rng >> 17; ap_rng ^= ap_rng << 5;
    return n ? ap_rng % n : 0;
}
/* Parse signed coordinates without strtod, doubles or libm. */
static int ap_coord(const char *s, int32_t *out, int max)
{
    int neg = *s == '-', digits = 0;
    int32_t v = 0, f = 0, scale = 1000;
    if (neg) s++;
    while (*s >= '0' && *s <= '9') {
        v = v*10 + *s++-'0'; if (++digits > 3) return 0;
    }
    if (!digits) return 0;
    if (*s == '.') {
        s++; while (*s >= '0' && *s <= '9') {
            if (scale) f += (*s-'0')*scale, scale /= 10;
            s++;
        }
    }
    if (*s || v > max || (v == max && f)) return 0;
    *out = (v*10000+f)*(neg ? -1 : 1); return 1;
}
typedef struct {
    ap_json_t json;
    ap_loc_t loc;
    unsigned lat_ok, lon_ok, playlist, tracks;
    ap_track_t cur;
} ap_reader_t;
static uint32_t ap_id(const char *s)
{
    uint32_t v = 0;
    for (; *s; s++) { if (*s < '0' || *s > '9' || v > 100000000u) return 0; v = v*10+*s-'0'; }
    return v;
}
static void ap_field(void *ctx, const char *k, const char *v, int cut)
{
    ap_reader_t *r = ctx;
    if (!r->playlist) {
        if (!strcmp(k, "locid")) r->loc.id = ap_id(v);
        if (!strcmp(k, "lat")) r->lat_ok = ap_coord(v, &r->loc.lat, 90);
        if (!strcmp(k, "lng")) r->lon_ok = ap_coord(v, &r->loc.lon, 180);
    } else {
        if (!strcmp(k, "mp3") && !cut && !strncmp(v, "https://aporee.org/maps/files/", 29))
            js_put(r->cur.url, sizeof r->cur.url, v, strlen(v));
        if (!strcmp(k, "ctitle")) js_put(r->cur.title, sizeof r->cur.title, v, strlen(v));
        if (!strcmp(k, "ltitle")) js_put(r->cur.place, sizeof r->cur.place, v, strlen(v));
        if (!strcmp(k, "artist")) js_put(r->cur.artist, sizeof r->cur.artist, v, strlen(v));
        if (!strcmp(k, "licence")) js_put(r->cur.licence, sizeof r->cur.licence, v, strlen(v));
        if (!strcmp(k, "sndid")) js_put(r->cur.id, sizeof r->cur.id, v, strlen(v));
    }
}
static void ap_object(void *ctx)
{
    ap_reader_t *r = ctx;
    if (!r->playlist) {
        if (r->loc.id && r->lat_ok && r->lon_ok && ap_region_matches(r->loc.id,ap_cache_region)) {
            unsigned pick = ap_rand(++ap_seen);
            if (ap_n < 32) ap_locs[ap_n++] = r->loc;
            else if (pick < 32) ap_locs[pick] = r->loc;
        }
        memset(&r->loc, 0, sizeof r->loc); r->lat_ok = r->lon_ok = 0;
    } else {
        if (r->cur.url[0] && !ap_rand(++r->tracks)) ap_track = r->cur;
        memset(&r->cur, 0, sizeof r->cur);
    }
}
static int ap_get(ap_reader_t *r, char *u, unsigned cap)
{
    conn_t c = {-1, 0, 0, ap_why};
    uint8_t b[256];
    int got = 0, rc, tries;
    memset(&r->json, 0, sizeof r->json);
    r->json.field = ap_field; r->json.object = ap_object; r->json.ctx = r;
    for (tries = 0; tries < 3; ++tries) { rc = http_open(&c, u, cap, 1); if (rc != 1) break; }
    if (rc) { conn_close(&c); return -1; }
    {
        uint32_t start = shim_ms(), bytes = 0;
        while ((got = conn_read(&c, b, sizeof b)) > 0) {
            ap_bytes += (uint32_t)got; bytes += (uint32_t)got;
            apj_feed(&r->json, b, (unsigned)got);
            if (r->json.failed) break;
            while (bytes > (shim_ms()-start)*48u) os_time_dly(1);
        }
    }
    conn_close(&c);
    if (got < 0 || !apj_ok(&r->json)) { why_to(ap_why, "Aporee response incomplete"); return -1; }
    return 0;
}
static void ap_task(void *arg)
{
    ap_reader_t *r = malloc(sizeof *r);
    ap_loc_t loc;
    char u[128];
    int err = 1;
    (void)arg;
    if (!r) { why_to(ap_why, "no memory for Aporee"); ap_state = AP_ERROR; return; }
    memset(r, 0, sizeof *r);
    if (!ap_n) {
        ap_seen = 0;
        strcpy(u, "https://aporee.org/api/maps/getAllLocations");
        if (ap_get(r, u, sizeof u)) { ap_n = 0; goto done; }
    }
    if (!ap_n) { why_to(ap_why, "no locations in region"); goto done; }
    {
        unsigned k = ap_rand(ap_n);
        if(ap_n>1 && ap_locs[k].id==ap_last) k=(k+1u)%ap_n;
        loc = ap_locs[k]; ap_locs[k] = ap_locs[--ap_n];
    }
    memset(r, 0, sizeof *r); r->playlist = 1;
    snprintf(u, sizeof u, "https://aporee.org/api/maps/getPlaylist?locids%%5B%%5D=%u", (unsigned)loc.id);
    if (ap_get(r, u, sizeof u)) goto done;
    if (!r->tracks) { why_to(ap_why, "no playable MP3 here"); goto done; }
    ap_track.lat = loc.lat; ap_track.lon = loc.lon; ap_last = loc.id; err = 0;
done:
    free(r);
    __asm__ volatile("" ::: "memory");
    ap_state = err ? AP_ERROR : AP_DONE;
}
int ap_start(uint16_t region)
{
    if (ap_state == AP_BUSY || radio_net_running() || tls_taken) return -1;
    if(!ap_locs) {
        ap_locs=malloc(64u*sizeof *ap_locs);
        if(!ap_locs) {why_to(ap_why,"no location memory");return -1;}
        ap_world_locs=ap_locs+32;
    }
    if(region>=ap_region_total()) return -1;
    if(region && !ap_regions_ready()) {why_to(ap_why,"no region memory");return -1;}
    if(region!=ap_cache_region) {
        if(!ap_cache_region) {memcpy(ap_world_locs,ap_locs,32u*sizeof *ap_locs);ap_world_n=ap_n;}
        ap_n=0;
        if(!region) {memcpy(ap_locs,ap_world_locs,32u*sizeof *ap_locs);ap_n=ap_world_n;ap_world_n=0;}
        ap_cache_region=region;
    }
    if (!ap_rng) { radio_entropy((uint8_t *)&ap_rng, sizeof ap_rng); if (!ap_rng) ap_rng = 0xA90832u; }
    ap_why[0] = 0; ap_bytes = 0; ap_state = AP_BUSY;
    if (task_create(ap_task, 0, "aporee")) { why_to(ap_why, "no Aporee task"); ap_state = AP_ERROR; return -1; }
    return 0;
}
