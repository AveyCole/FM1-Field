"""One-time migration from 0.1.0 to the MP3-only buffered player."""
from pathlib import Path
import re
r=Path(__file__).parent
p=r/'app/radio.c';s=p.read_text()
s=re.sub(r'#include "third_party/pvaac/[^\n]+\n','',s)
a=s.index('#define NET_RING ');b=s.index('#define LIN_MAX ',a)
s=s[:a]+'''#define NET_RING (80u * 1024u) /* four times the original compressed-audio reserve */
#define PCM_FRAMES 6912u       /* six MP3 frames: ~157 ms at 44.1 kHz */
'''+s[b:]
a=s.index('/* the decoders, one at a time');b=s.index('static int radio_alloc',a)
s=s[:a]+'''/* Aporee supplies MP3s. No AAC decoder or permanently reserved AAC arena. */
static uint8_t dec_kind; /* 0 none, 1 MP3 */
static void dec_drop(void)
{
    free(mp3); free(pcm_tmp); mp3 = 0; pcm_tmp = 0; dec_kind = 0;
}
static int dec_use(void)
{
    if (mp3 && pcm_tmp) return 0;
    mp3 = malloc(sizeof *mp3);
    pcm_tmp = malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof *pcm_tmp);
    if (!mp3 || !pcm_tmp) { dec_drop(); return -1; }
    mp3dec_init(mp3); dec_kind = 1; return 0;
}
/* Buffer about 2 seconds initially, 3 after a real network underrun, capped
 * below the ring's capacity so startup cannot deadlock on a full producer. */
static uint32_t radio_buffer_target(void)
{
    uint32_t kbps = radio_kbps ? radio_kbps : 192u;
    uint32_t target = kbps * (radio_rebuffers ? 375u : 250u);
    if (target < 12u*1024u) target = 12u*1024u;
    if (target > NET_RING - 4096u) target = NET_RING - 4096u;
    return target;
}

'''+s[b:]
s=s.replace('dec_drop();                                   /* its RAM back (AAC\'s is 88 KB): kept, a stopped AAC station left\n                                                   * WiFi too little to join again ("heap: out of memory") */','dec_drop(); /* return decoder memory for WiFi reconnects */')
s=s.replace('(dec_kind == 2 ? 2048u : 1152u)','1152u')
s=s.replace('dec_use(radio_mode != 2 && radio_net_aac ? 2 : 1)','dec_use()')
a=s.index('        if (dec_kind == 2) {');b=s.index('        samples = mp3dec_decode_frame',a)
s=s[:a]+s[b:]
s=s.replace('        src = pcm_tmp;\n        }\n#ifdef RADIO_LOCAL_TEST','        src = pcm_tmp;\n#ifdef RADIO_LOCAL_TEST')
s=s.replace('radio_wr - radio_rd >= NET_RING / 2u','radio_wr - radio_rd >= radio_buffer_target()')
mark='    /* the stream dropped (stalled, ended, refused):'
i=s.index(mark)
s=s[:i]+'''    if (radio_mode != 2 && radio_net_aac) {
        w_cpy(radio_why, "Aporee needs an MP3", sizeof radio_why);
        radio_unplayable = 1; radio_st = RS_ERROR; radio_net_stop(); return;
    }
'''+s[i:]
s=s.replace('/* play once half the stream buffer and the decoded audio are full; dry: pause and refill */','/* Decode reserve plus a bitrate-aware compressed reserve; short files drain without waiting. */')
p.write_text(s)
p=r/'app/app.c';s=p.read_text()
s=s.replace('    arena_init();                                 /* first, while the heap is whole (radio.c) */\n','')
s=s.replace('"  under %u  rebuf %u  reconn %u  aac bad %u  %s%s"','"  under %u  rebuf %u  reconn %u  %s%s"')
s=s.replace('(unsigned)radio_rebuffers, (unsigned)radio_reconnects, (unsigned)radio_aac_errs,','(unsigned)radio_rebuffers, (unsigned)radio_reconnects,')
s=s.replace('dec_kind == 2 ? "AAC " : dec_kind == 1 ? "MP3 " : ""','dec_kind == 1 ? "MP3 " : ""')
s=s.replace('spf = dec_kind != 2 ? 1152u : aac_ext->aacPlusUpsamplingFactor == 2 ? 2048u : 1024u;', 'spf = 1152u;')
s=s.replace('        ui_dirty = 0;\n        lcd_fill','        ui_dirty = 0;\n        ap_globe_valid = 0;\n        lcd_fill')
p.write_text(s)
p=r/'app/fm1.toml';s=p.read_text();a=s.index('# PacketVideo');b=s.index('vendored =',a)
s=s[:a]+'# MP3 only: reclaimed the AAC arena for streaming reserve.\no2 = []\n'+s[b:];p.write_text(s)
