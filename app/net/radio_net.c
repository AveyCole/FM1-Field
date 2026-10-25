/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The radio's network side, built against the SDK's lwIP headers (sockets, DNS); the app sees only
 * radio_net_* and rgn_*.
 *  - The stream: a task that fetches an HTTP or HTTPS stream (Icecast / SHOUTcast) into a ring the app's
 *    decoder drains. Redirects (3xx Location) are followed a few times. A stream that is not MP3 or AAC in
 *    ADTS frames (MP4, HLS, Ogg, a page) is refused by its Content-Type.
 *  - Radio Garden: a task that finds a place by name (its search) and reads the place's stations (its
 *    channels page), as the JSON arrives, into a list the app browses. A station plays through Radio
 *    Garden's listen link, which redirects to the real stream.
 * HTTPS is BearSSL's client (third_party/bearssl), TLS 1.2 with ECDHE and AES-GCM. The server's
 * certificate is NOT checked (no roots, no clock): its key is used as sent. For a radio, the worst a man
 * in the middle can do is change the audio or the station list. One TLS connection at a time: the app
 * stops an https stream before it asks Radio Garden. */
#include <stdint.h>
#include <string.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/netif.h"
#include "shim.h"
#include "bearssl.h"
#include "stream_crypto.h"

extern int snprintf(char *buf, unsigned long sz, const char *f, ...);
extern void *malloc(unsigned long n);
extern void free(void *p);
extern void radio_entropy(uint8_t *p, unsigned n);   /* the app's: timing and converter noise */
extern void os_time_dly(int ticks);
void br_fm1_yield(void) { os_time_dly(0); }        /* BearSSL's long computations let WiFi's tasks run (inner.h) */

enum { RN_IDLE, RN_CONNECTING, RN_STREAMING, RN_ERROR, RN_ENDED, RN_UNPLAYABLE };
volatile int radio_net_state;
volatile int radio_net_aac;                       /* the stream is AAC (ADTS), else MP3 */
volatile uint32_t radio_rd, radio_wr;            /* the ring: the task writes, the app reads (bytes, ever) */
static uint8_t *ring;
static uint32_t ring_size;
#define URL_MAX 512u                              /* tokenised redirects (zeno.fm) run to ~350 */
static char *url;                                 /* URL_MAX, from the heap once (RAM is full) */
static volatile uint8_t stop_req, running;
char radio_net_why[32];
char radio_net_name[48];                          /* icy-name, when the station sends one */

static void why_to(char *w, const char *s)
{
    uint32_t i;
    for (i = 0; i + 1u < 32u && s[i]; i++)
        w[i] = s[i];
    w[i] = 0;
    shim_log(s);
}

/* ---- the connection: a socket, and TLS over it for https */
typedef struct {
    const br_x509_class *vtable;                  /* no checks: the first certificate's key, as sent */
    br_x509_decoder_context dec;
    int depth;
} nov_t;
typedef struct {
    br_ssl_client_context cc;
    nov_t x509;
    br_sslio_context io;
} tls_t;
typedef struct {
    int sock;
    tls_t *tls;                                   /* &tls_state while this connection holds it */
    unsigned char *buf;                           /* its 16.7 KB record buffer, from the heap */
    char *why;                                    /* where its errors are told (32 bytes) */
} conn_t;
/* the TLS state (4.5 KB) in the NOINIT region the smaller log left free: one connection at a time */
static tls_t tls_state __attribute__((section(".noinit")));
static volatile uint8_t tls_taken;

static void nov_start_chain(const br_x509_class **c, const char *name) { (void)name; ((nov_t *)c)->depth = 0; }
static void nov_start_cert(const br_x509_class **c, uint32_t len)
{
    nov_t *x = (nov_t *)c;
    (void)len;
    if (x->depth == 0)
        br_x509_decoder_init(&x->dec, 0, 0);
}
static void nov_append(const br_x509_class **c, const unsigned char *b, size_t n)
{
    nov_t *x = (nov_t *)c;
    if (x->depth == 0)
        br_x509_decoder_push(&x->dec, b, n);
}
static void nov_end_cert(const br_x509_class **c) { ((nov_t *)c)->depth++; }
static unsigned nov_end_chain(const br_x509_class **c)
{
    nov_t *x = (nov_t *)c;
    return x->depth && br_x509_decoder_get_pkey(&x->dec) ? 0u : (unsigned)BR_ERR_X509_EMPTY_CHAIN;
}
static const br_x509_pkey *nov_get_pkey(const br_x509_class *const *c, unsigned *usages)
{
    if (usages)
        *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
    return br_x509_decoder_get_pkey(&((nov_t *)c)->dec);
}
static const br_x509_class NOV = {sizeof(nov_t), nov_start_chain, nov_start_cert, nov_append, nov_end_cert,
                                  nov_end_chain, nov_get_pkey};

static int sock_rd(void *ctx, unsigned char *b, size_t n)
{
    int r = recv(*(int *)ctx, b, n, 0);
    return r > 0 ? r : -1;
}
static int sock_wr(void *ctx, const unsigned char *b, size_t n)
{
    int r = send(*(int *)ctx, b, n, 0);
    return r > 0 ? r : -1;
}

/* what streaming servers offer: ECDHE with AES-GCM (P-256 for the exchange and ECDSA keys, or RSA keys) */
static const uint16_t SUITES[] = {
    BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
    BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384, BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384};

static int tls_open(conn_t *c, const char *host)
{
    br_ssl_engine_context *e;
    uint8_t seed[32];
    if (tls_taken)
        return -2;                                /* the other task holds it */
    c->buf = malloc(BR_SSL_BUFSIZE_MONO);
    if (!c->buf)
        return -1;
    tls_taken = 1;
    c->tls = &tls_state;
    e = &c->tls->cc.eng;
    br_ssl_client_zero(&c->tls->cc);
    br_ssl_engine_set_versions(e, BR_TLS12, BR_TLS12);
    br_ssl_engine_set_suites(e, SUITES, sizeof SUITES / sizeof SUITES[0]);
    br_ssl_engine_set_rsavrfy(e, &br_rsa_i31_pkcs1_vrfy);
    br_ssl_engine_set_ec(e, &br_ec_p256_m31);
    br_ssl_engine_set_ecdsa(e, &br_ecdsa_i31_vrfy_asn1);
    br_ssl_engine_set_hash(e, br_sha1_ID, &br_sha1_vtable);
    br_ssl_engine_set_hash(e, br_sha256_ID, &br_sha256_vtable);
    br_ssl_engine_set_hash(e, br_sha384_ID, &br_sha384_vtable);
    br_ssl_engine_set_prf_sha256(e, &br_tls12_sha256_prf);
    br_ssl_engine_set_prf_sha384(e, &br_tls12_sha384_prf);
    br_ssl_engine_set_gcm(e, &br_sslrec_in_gcm_vtable, &br_sslrec_out_gcm_vtable);
    br_ssl_engine_set_aes_ctr(e, &stream_aes_ctr);
    br_ssl_engine_set_ghash(e, &stream_ghash);
    c->tls->x509.vtable = &NOV;
    br_ssl_engine_set_x509(e, &c->tls->x509.vtable);
    br_ssl_engine_set_buffer(e, c->buf, BR_SSL_BUFSIZE_MONO, 0);
    radio_entropy(seed, sizeof seed);
    br_ssl_engine_inject_entropy(e, seed, sizeof seed);
    if (!br_ssl_client_reset(&c->tls->cc, host, 0)) {
        free(c->buf);
        c->buf = 0;
        c->tls = 0;
        tls_taken = 0;
        return -1;
    }
    br_sslio_init(&c->tls->io, e, sock_rd, &c->sock, sock_wr, &c->sock);
    return 0;
}

static void conn_close(conn_t *c)
{
    if (c->tls) {                                 /* no close_notify: the socket goes too */
        free(c->buf);
        c->buf = 0;
        c->tls = 0;
        tls_taken = 0;
    }
    if (c->sock >= 0)
        closesocket(c->sock);
    c->sock = -1;
}
static int conn_read(conn_t *c, void *b, unsigned n)   /* > 0 bytes, 0 the end, < 0 an error or a timeout */
{
    int r, err;
    if (!c->tls)
        return recv(c->sock, b, n, 0);
    r = br_sslio_read(&c->tls->io, b, n);
    if (r >= 0)
        return r;
    err = br_ssl_engine_last_error(&c->tls->cc.eng);
    if (err && err != BR_ERR_IO)
        snprintf(c->why, 32, "tls error %d", err);
    return err == 0 ? 0 : -1;
}
static int conn_write(conn_t *c, const void *b, unsigned n)
{
    if (!c->tls)
        return send(c->sock, b, n, 0) == (int)n ? 0 : -1;
    if (br_sslio_write_all(&c->tls->io, b, n) < 0 || br_sslio_flush(&c->tls->io) < 0) {
        snprintf(c->why, 32, "tls error %d", br_ssl_engine_last_error(&c->tls->cc.eng));
        return -1;
    }
    return 0;
}

/* "http[s]://host[:port]/path" */
static int split(const char *u, char *host, unsigned hmax, int *port, const char **path, int *secure)
{
    unsigned n = 0;
    *secure = !strncmp(u, "https://", 8);
    if (strncmp(u, "http://", 7) && !*secure)
        return -1;
    u += *secure ? 8 : 7;
    while (*u && *u != '/' && *u != ':' && n + 1u < hmax)
        host[n++] = *u++;
    host[n] = 0;
    *port = *secure ? 443 : 80;
    if (*u == ':') {
        *port = 0;
        for (u++; *u >= '0' && *u <= '9'; u++)
            *port = *port * 10 + (*u - '0');
    }
    *path = *u ? u : "/";
    return n ? 0 : -1;
}

static int ieq_prefix(const char *a, const char *p)    /* a starts with p, ignoring case */
{
    for (; *p; a++, p++) {
        char x = *a, y = *p;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y)
            return 0;
    }
    return 1;
}
static int has_word(const char *s, const char *w)      /* w somewhere in s, ignoring case */
{
    for (; *s; s++)
        if (ieq_prefix(s, w))
            return 1;
    return 0;
}

/* one request on c for u (u is replaced by a redirect's target, umax its room); json: an API call (no ICY
 * name, any Content-Type). 0 = the body follows, 1 = redirected, -1 = failed, -2 = not an MP3 stream */
static int http_open(conn_t *c, char *u, unsigned umax, int json)
{
    char host[64], req[640], line[URL_MAX];
    const char *path;
    int port, s, n = 0, status = 0, secure, rc;
    uint32_t t_hs;                                /* when the request (and so the handshake) began */
    struct hostent *he;
    struct sockaddr_in a;
    char type[40];
    type[0] = 0;
    if (split(u, host, sizeof host, &port, &path, &secure)) {
        why_to(c->why, "not an http(s):// url");
        return -1;
    }
    he = gethostbyname(host);
    if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
        why_to(c->why, "dns failed");
        return -1;
    }
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        why_to(c->why, "no socket");
        return -1;
    }
    memset(&a, 0, sizeof a);
    a.sin_len = sizeof a;
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    memcpy(&a.sin_addr, he->h_addr_list[0], 4);
    {
        const uint8_t *ip = (const uint8_t *)he->h_addr_list[0];
        snprintf(req, 48, "%s = %u.%u.%u.%u:%d", host, ip[0], ip[1], ip[2], ip[3], port);
        shim_log(req);
    }
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) {
        int err = 0;
        socklen_t el = sizeof err;
        getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &el);
        closesocket(s);
        snprintf(c->why, 32, "connect failed (%d)", err);
        shim_log(c->why);
        return -1;
    }
    {   /* a silent server ends the recv after 5 s (lwIP's NONSTANDARD option: an int, ms), so it can reconnect */
        int ms = 5000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &ms, sizeof ms);
    }
    c->sock = s;
    if (secure && (rc = tls_open(c, host)) != 0) {
        conn_close(c);
        why_to(c->why, rc == -2 ? "tls busy" : "tls: no memory");
        return -1;
    }
    n = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: FM-1 Net/0.1\r\n%s"
                 "Connection: close\r\n\r\n", path, host,
                 json ? "Accept: application/json\r\nX-Requested-With: XMLHttpRequest\r\nReferer: https://aporee.org/maps/\r\n" : "Icy-MetaData: 0\r\nAccept: */*\r\n");
    t_hs = shim_ms();
    if (conn_write(c, req, (unsigned)n)) {        /* for https, the handshake happens here */
        conn_close(c);
        if (!c->why[0] || !secure)
            why_to(c->why, "send failed");
        shim_log(c->why);
        return -1;
    }
    if (secure) {
        char b[32];
        snprintf(b, sizeof b, "tls up after %lu ms", (unsigned long)(shim_ms() - t_hs));
        shim_log(b);
    }
    /* the headers, a line at a time */
    if (!json)
        radio_net_name[0] = 0;
    for (;;) {
        unsigned k = 0;
        char ch = 0;
        while (k + 1u < sizeof line) {
            if (conn_read(c, &ch, 1) != 1) {
                conn_close(c);
                if (!c->why[0])
                    why_to(c->why, "closed in headers");
                shim_log(c->why);
                return -1;
            }
            if (ch == '\n')
                break;
            if (ch != '\r')
                line[k++] = ch;
        }
        if (ch != '\n')                           /* too long: the rest of the line goes */
            while (conn_read(c, &ch, 1) == 1 && ch != '\n')
                ;
        line[k] = 0;
        if (!status) {                            /* "HTTP/1.x 200 OK" or "ICY 200 OK" */
            const char *p = line;
            while (*p && *p != ' ')
                p++;
            while (*p == ' ')
                p++;
            while (*p >= '0' && *p <= '9')
                status = status * 10 + (*p++ - '0');
            snprintf(req, 40, "http %d", status);
            shim_log(req);
            continue;
        }
        if (!k)
            break;                                /* the blank line: the body follows */
        if (status >= 300 && status < 400 && ieq_prefix(line, "location:")) {
            const char *v = line + 9;
            while (*v == ' ')
                v++;
            strncpy(u, v, umax - 1u);
            u[umax - 1u] = 0;
        } else if (!json && ieq_prefix(line, "icy-name:")) {
            const char *v = line + 9;
            while (*v == ' ')
                v++;
            strncpy(radio_net_name, v, sizeof radio_net_name - 1);
            radio_net_name[sizeof radio_net_name - 1] = 0;
        } else if (ieq_prefix(line, "content-type:")) {
            const char *v = line + 13;
            while (*v == ' ')
                v++;
            strncpy(type, v, sizeof type - 1);
            type[sizeof type - 1] = 0;
        }
    }
    if (status >= 300 && status < 400) {
        conn_close(c);
        shim_log("redirect");
        return 1;
    }
    if (status != 200) {
        conn_close(c);
        if (!json && (status == 404 || status == 410)) {   /* gone: Radio Garden's link to an offline station */
            why_to(c->why, "station offline");
            return -2;
        }
        snprintf(c->why, 32, "http status %d", status);
        shim_log(c->why);
        return -1;
    }
    /* a stream the decoders cannot play: AAC in an MP4 container, HLS (mpegurl), Ogg, a web page. MP3 and AAC
     * in ADTS frames (aac, aacp) are played: which one, radio_net_aac */
    if (!json)
        radio_net_aac = type[0] && has_word(type, "aac") && !has_word(type, "mp4");
    if (!json && type[0] && (has_word(type, "mp4") || has_word(type, "mpegurl") ||
                             has_word(type, "ogg") || has_word(type, "html"))) {
        conn_close(c);
        snprintf(c->why, 32, "can't play %s", has_word(type, "mp4") ? "MP4" :
                 has_word(type, "mpegurl") ? "HLS" : has_word(type, "ogg") ? "Ogg" : "a page");
        shim_log(c->why);
        return -2;
    }
    return 0;
}

/* ---- the stream */
static conn_t sc = {-1, 0, 0, radio_net_why};

static void radio_task(void *arg)
{
    int tries, rc = -1;
    (void)arg;
    running = 1;
    radio_net_state = RN_CONNECTING;
    for (tries = 0; tries < 4 && !stop_req; tries++) {
        rc = http_open(&sc, url, URL_MAX, 0);
        if (rc != 1)
            break;
    }
    if (rc != 0) {
        radio_net_state = rc == -2 ? RN_UNPLAYABLE : RN_ERROR;
        running = 0;
        return;
    }
    radio_net_state = RN_STREAMING;
    while (!stop_req) {
        uint32_t used = radio_wr - radio_rd, room = ring_size - used, at = radio_wr % ring_size, n;
        int got;
        if (room < 1460u) {                       /* full: let the decoder catch up */
            os_time_dly(1);
            continue;
        }
        n = ring_size - at;                       /* the contiguous part */
        if (n > room)
            n = room;
        if (n > 2920u)
            n = 2920u;
        got = conn_read(&sc, ring + at, n);
        if (got <= 0) {
            if (got == 0 || !sc.tls)
                why_to(radio_net_why, got == 0 ? "stream ended" : "stream stalled");
            else if (!radio_net_why[0])
                why_to(radio_net_why, "stream stalled");
            radio_net_state = got == 0 ? RN_ENDED : RN_ERROR;
            break;
        }
        radio_wr += (uint32_t)got;
        /* A file server can send the entire reserve in one burst. Give decoding
         * and WiFi a turn even when recv() never needs to wait. */
        os_time_dly(0);
    }
    conn_close(&sc);
    if (stop_req)
        radio_net_state = RN_IDLE;
    running = 0;
}

/* the app's: start fetching url into buf (a power of two is not needed); 0 = started */
int radio_net_start(const char *u, uint8_t *buf, uint32_t size)
{
    if (running)
        return -1;
    if (!url && !(url = malloc(URL_MAX)))
        return -1;
    strncpy(url, u, URL_MAX - 1);
    url[URL_MAX - 1] = 0;
    ring = buf;
    ring_size = size;
    radio_rd = radio_wr = 0;
    radio_net_why[0] = 0;
    stop_req = 0;
    radio_net_state = RN_CONNECTING;              /* now, not when the task first runs: the last stream's state (an */
    running = 1;                                  /* unplayable one) was read meanwhile, with no reason: ": turn K4" */
    if (task_create(radio_task, 0, "radio")) {
        running = 0;
        radio_net_state = RN_ERROR;
        why_to(radio_net_why, "no task");
        return -1;
    }
    return 0;
}
/* ask the task to stop; it ends after its current recv */
void radio_net_stop(void) { stop_req = 1; }
int radio_net_running(void) { return running; }
int radio_net_tls(void) { return sc.tls != 0; }   /* the stream holds the TLS state */

static void js_put(char *d,unsigned max,const char *s,unsigned n) {
    if(n>=max)n=max-1;memcpy(d,s,n);d[n]=0;
}
#include "aporee_net.c"
