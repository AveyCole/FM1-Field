"""One-time source conversion from the pinned upstream checkout; retained for provenance."""
from pathlib import Path
import re
root = Path(__file__).parent
p = root/'app/app.c'
s = p.read_text()
s = s.replace('#include "inflate.c"\n#include "rg.c"\n', '')
s = s.replace('#define RSET_MAGIC 0x52414431u', '#define RSET_MAGIC 0x41505231u')
s = s.replace('rset.magic = RSET_MAGIC;\n        n = 0;', 'rset.magic = RSET_MAGIC;\n        rset.playing = 1;\n        n = 0;')
s = s.replace('static const char *places_status(char *b, unsigned max);   /* places.c */\n#include "fav.c"\n#include "radio_ui.c"\n#include "places.c"', '#include "aporee_ui.c"')
a=s.index('/* ---- the Favorites page'); b=s.index('static void ui_draw',a); s=s[:a]+s[b:]
s=s.replace('    case PG_BROWSE: ui_browse(); break;\n','').replace('    case PG_FAV: ui_fav(); break;\n','')
a=s.index('    case 0x53:\n'); b=s.index('    default:',a); s=s[:a]+'    case 0x53: radio_ui_dump(); break;\n'+s[b:]
s=s.replace('case 0x54: places_refresh(); break;', 'case 0x54: ap_shuffle(); break;')
s=s.replace('    rg_open();\n','')
a=s.index('        if (down & (1u << B_SAVE)) {\n',s.index('static void input'))
b=s.index('    } else if (ui_page == PG_WIFI)',a)
s=s[:a]+'''        if (down & ((1u << B_OCTDN) | (1u << B_OCTUP))) ap_shuffle();
        if (down & (1u << B_SEL)) ap_info = !ap_info;
        sel_turn(3, plat_enc(EN_K4) + turn);
        (void)plat_enc(EN_K1); (void)plat_enc(EN_K2); (void)plat_enc(EN_K3);
'''+s[b:]
a=s.index('    } else if (ui_page == PG_FAV && fav)'); b=s.index('\n}\nvoid app_input_only',a)
s=s[:a]+'    }'+s[b:]
a=s.index('    } else if (!strcmp(what, "fav_open"))'); b=s.index('    } else\n        return 0;',a)
s=s[:a]+s[b:]
s=s.replace('lb = 1u << B_PLAY | 1u << B_GLO | (sel_valid() ? 1u << B_SEL : 0u);','lb = 1u << B_PLAY | 1u << B_GLO | 1u << B_SEL | 1u << B_OCTDN | 1u << B_OCTUP;')
s=re.sub(r'    if \(ui_page == PG_FAV\)\n        lb = .*?;\n','',s)
s=s.replace('    places_poll();                                /* no places yet: fetched from Radio Garden (places.c) */\n','')
a=s.index('#ifdef PLAT_WEB\n/* the browser emulator:'); s=s[:a]+'''#ifdef PLAT_HOST
/* Host UI fixture, explicitly simulated. Device uses net/aporee_net.c. */
volatile int radio_net_state, radio_net_aac;
volatile uint32_t radio_rd, radio_wr;
char radio_net_why[32], radio_net_name[48];
volatile int ap_state;
volatile uint32_t ap_bytes;
char ap_why[32];
ap_track_t ap_track;
static unsigned host_pick;
int ap_start(void) {
    memset(&ap_track,0,sizeof ap_track);
    w_cpy(ap_track.url,"https://aporee.org/maps/files/host-fixture.mp3",sizeof ap_track.url);
    w_cpy(ap_track.title,"SIMULATED recording",sizeof ap_track.title);
    w_cpy(ap_track.place,host_pick++%2?"Tokyo, Japan":"Berlin, Germany",sizeof ap_track.place);
    ap_track.lat=host_pick%2?525200:356800; ap_track.lon=host_pick%2?134050:1396900;
    w_cpy(ap_track.artist,"Host test fixture",sizeof ap_track.artist);
    w_cpy(ap_track.licence,"SIMULATED",sizeof ap_track.licence);
    ap_state=AP_DONE; return 0;
}
int radio_net_start(const char *u,uint8_t *b,uint32_t n) {
    (void)u;(void)b;(void)n;radio_net_state=RN_ERROR;
    w_cpy(radio_net_why,"host: no audio network",sizeof radio_net_why);return 0;
}
void radio_net_stop(void) {}
int radio_net_running(void) {return 0;}
int radio_net_tls(void) {return 0;}
#endif
'''
p.write_text(s)
p=root/'app/net/radio_net.c'; s=p.read_text(); s=s[:s.index('/* ---- Radio Garden:')]
s=s.replace('json ? "Accept: application/json\\r\\n"', 'json ? "Accept: application/json\\r\\nX-Requested-With: XMLHttpRequest\\r\\nReferer: https://aporee.org/maps/\\r\\n"')
s+='''static void js_put(char *d,unsigned max,const char *s,unsigned n) {
    if(n>=max)n=max-1;memcpy(d,s,n);d[n]=0;
}
#include "aporee_net.c"
''';p.write_text(s)
p=root/'app/net/aporee_net.c';s=p.read_text().replace('tls_busy','tls_taken');p.write_text(s)
p=root/'app/app.h';s=p.read_text().replace('#define APP_NAME "RADIO"','#define APP_NAME "APOREE"');p.write_text(s)
p=root/'app/fm1.toml';s=p.read_text().replace('name = "RADIO"','name = "APOREE"').replace('id_prefix = "FM-1_13"','id_prefix = "FM-1_84"').replace('usb_manufacturer = "fm1-radio"','usb_manufacturer = "aporee-fm1"').replace('usb_product = "RADIO FM-1"','usb_product = "APOREE FM-1"');p.write_text(s)
# Finite recordings must drain fully; reconnect logic is for live radio only.
p=root/'app/radio.c';s=p.read_text().replace('(radio_net_state == RN_ERROR || radio_net_state == RN_ENDED)', '(radio_net_state == RN_ERROR)')
mark='    if (radio_st == RS_CONNECTING && radio_net_state == RN_STREAMING)\n        radio_st = RS_BUFFERING;'
s=s.replace(mark,'''    if (radio_mode != 2 && radio_net_state == RN_ENDED && !radio_net_running() &&
        radio_wr == radio_rd && pcm_w == pcm_r) {
        radio_stop(); ap_audio_ended = 1; return;
    }
'''+mark)
s=s.replace('static const char *radio_url = RADIO_URL;', 'static const char *radio_url = RADIO_URL;\nstatic uint8_t ap_audio_ended;')
p.write_text(s)
p=root/'app/aporee_ui.c';s=p.read_text().replace('    uint32_t now = plat_ms();','    uint32_t now = plat_ms();\n    if (ap_audio_ended) { ap_audio_ended = 0; if (radio_want) ap_shuffle(); }',1);p.write_text(s)
