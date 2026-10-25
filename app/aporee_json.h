/* SPDX-License-Identifier: MPL-2.0 */
/* Bounded, incremental reader for Aporee's flat object arrays. No response-sized allocation.
 * Fields from nested objects are ignored. Long values are flagged, never used as URLs.
 * String values are folded to ASCII for the device font. */
#pragma once
#include <stdint.h>
#include <string.h>
typedef struct {
    unsigned depth, string, escape, unicode, cp, key_mode, want_key, number;
    unsigned kn, vn, truncated, failed, started, ended;
    char key[24], value[512];
    void (*field)(void *, const char *, const char *, int);
    void (*object)(void *);
    void *ctx;
} ap_json_t;
static void apj_char(ap_json_t *j, unsigned c)
{
    if (j->key_mode) {
        if (j->kn + 1 < sizeof j->key) j->key[j->kn++] = (char)c;
        else j->failed = 1;
    } else if (j->vn + 1 < sizeof j->value) {
        j->value[j->vn++] = c < 32 ? ' ' : c < 127 ? (char)c : '?';
    } else j->truncated = 1;
}
static void apj_value(ap_json_t *j)
{
    j->value[j->vn] = 0;
    if (j->depth == 2 && j->key[0]) j->field(j->ctx, j->key, j->value, j->truncated);
}
static void apj_feed(ap_json_t *j, const uint8_t *p, unsigned n)
{
    unsigned i;
    for (i = 0; i < n && !j->failed; ++i) {
        unsigned c = p[i];
        if (j->string) {
            if (j->unicode) {
                unsigned h = c >= '0' && c <= '9' ? c-'0' :
                    (c|32) >= 'a' && (c|32) <= 'f' ? (c|32)-'a'+10 : 99;
                if (h == 99) { j->failed = 1; continue; }
                j->cp = j->cp * 16 + h;
                if (!--j->unicode) apj_char(j, j->cp);
            } else if (j->escape) {
                j->escape = 0;
                if (c == 'u') j->unicode = 4, j->cp = 0;
                else if (c == '"' || c == '\\' || c == '/') apj_char(j, c);
                else if (c == 'n' || c == 'r' || c == 't' || c == 'b' || c == 'f') apj_char(j, ' ');
                else j->failed = 1;
            } else if (c == '\\') j->escape = 1;
            else if (c == '"') {
                j->string = 0;
                if (j->key_mode) j->key[j->kn] = 0;
                else apj_value(j);
            } else apj_char(j, c);
            continue;
        }
        if (j->number) {
            if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E') {
                apj_char(j, c); continue;
            }
            j->number = 0; apj_value(j);
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (j->ended) { j->failed = 1; continue; }
        if (!j->started) {
            if (c != '[') { j->failed = 1; continue; }
            j->started = 1; j->depth = 1; continue;
        }
        if (c == '"') {
            j->string = 1; j->key_mode = j->want_key; j->kn = j->vn = j->truncated = 0;
        } else if (c == '{' || c == '[') {
            if (++j->depth > 16) j->failed = 1;
            j->want_key = c == '{'; j->key[0] = 0;
        } else if (c == '}' || c == ']') {
            if (!j->depth) { j->failed = 1; continue; }
            if (j->depth == 2 && c == '}') j->object(j->ctx);
            if (!--j->depth) j->ended = c == ']';
            j->want_key = 0;
        } else if (c == ':') j->want_key = 0;
        else if (c == ',') j->want_key = j->depth == 2;
        else if ((c >= '0' && c <= '9') || c == '-') {
            j->number = 1; j->key_mode = 0; j->vn = j->truncated = 0; apj_char(j, c);
        } else if (c != 'n' && c != 'u' && c != 'l' && c != 't' && c != 'r' && c != 'e' && c != 'f' && c != 'a' && c != 's') j->failed = 1;
    }
}
static int apj_ok(const ap_json_t *j) { return j->ended && !j->failed && !j->depth && !j->string; }
