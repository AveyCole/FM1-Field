/* SPDX-License-Identifier: MPL-2.0 */
#include "aporee.h"
#include "fastmath.h"
#include "regions.h"
static void ui_page_set(int pg);
static uint16_t ap_active_region, ap_browse_country, ap_browse_pick;
static uint8_t ap_browse_level;
static int ap_browse_cursor, ap_browse_top;
static int ap_player_page(void) {return ap_active_region?PG_REGION:PG_HOME;}
static void ui_cell(int slot,uint32_t x,uint32_t y,const char *knob,const char *label,const char *value,int hi)
{
    (void)slot;
    cv_begin(112,44,hi?K_PANEL:K_BG);
    cv_text(6,2,&FONT_XS,label,K_DIM);
    cv_text(90,2,&FONT_XS,knob,K_LINE);
    cv_text(6,22,&FONT_S,value,K_TEXT); cv_blit(x,y);
}
static char play_url[512];
static uint8_t radio_want = 1, ap_pending = 1, ap_fetching, ap_info;
static uint32_t ap_retry, ap_play_at;
static ap_track_t ap_now __attribute__((section(".pool")));
static float globe_lat, globe_lon, globe_zoom;
static uint8_t ap_globe_valid;
static int32_t ap_globe_lat, ap_globe_lon;
static uint8_t ap_globe_has_sound;
#ifdef PLAT_HOST
static unsigned ap_test_globe_draws;
#endif
static int sel_valid(void) { return 1; }
static void ap_remember(void) { rset.playing = radio_want; rset_save(); }
static void ap_shuffle(void)
{
    ap_pending = 1; ap_retry = plat_ms(); radio_want = 1;
    if (wifi_st == WS_OFF) wifi_power(1);
    radio_stop(); play_url[0] = 0;
}
static void sel_turn(int level, int32_t d) { if (level == 3 && d) ap_shuffle(); }
static void radio_toggle(void)
{
    radio_want = !radio_want;
    if (!radio_want) radio_stop();
    else { if (wifi_st == WS_OFF) wifi_power(1); if (!play_url[0]) ap_pending = 1; }
    ap_remember();
}
static void radio_ui_restore(void) { radio_want = rset.playing; ap_pending = 1; }
static void radio_ui_service(void)
{
    uint32_t now = plat_ms();
    if (ap_audio_ended) { ap_audio_ended = 0; if (radio_want) ap_shuffle(); }
    if (ap_fetching && ap_state != AP_BUSY) {
        ap_fetching = 0;
        if (ap_state == AP_DONE && !ap_pending) {
            ap_now = ap_track;
            w_cpy(play_url, ap_now.url, sizeof play_url);
            radio_url = play_url; radio_unplayable = 0;
            ap_play_at = now; /* static globe: audio can start fetching immediately */
        } else if (ap_state == AP_ERROR) { ap_pending = 1; ap_retry = now + 15000; }
    }
    if (radio_want && ap_pending && !ap_fetching && wifi_st == WS_CONNECTED && (int32_t)(now-ap_retry) >= 0) {
        if (radio_st != RS_OFF) radio_stop();
        if (!radio_net_running() && ap_start(ap_active_region) == 0) { ap_fetching = 1; ap_pending = 0; }
    }
    if (radio_want && play_url[0] && !ap_fetching && !ap_pending && wifi_st == WS_CONNECTED &&
        !radio_net_running() && (int32_t)(now-ap_play_at) >= 0 &&
        (radio_st == RS_OFF || radio_st == RS_ERROR)) {
        radio_url = play_url; radio_play(0); ap_play_at = now + 15000;
    }
    if (!radio_want && radio_st != RS_OFF) radio_stop();
}
/* Coarse original outlines, latitude/longitude degrees. No map tiles or texture memory. */
static const int16_t ap_coasts[][2] = {
 {70,-165},{60,-140},{55,-130},{48,-124},{32,-117},{22,-105},{15,-92},{9,-79},{20,-87},{25,-81},{30,-81},{45,-65},{52,-55},{60,-65},{70,-85},{72,-120},{70,-165},{999,0},
 {10,-79},{8,-61},{-5,-35},{-23,-43},{-40,-64},{-55,-68},{-40,-74},{-15,-80},{10,-79},{999,0},
 {60,-45},{65,-52},{80,-65},{83,-35},{70,-20},{60,-45},{999,0},
 {36,-10},{44,-9},{50,-5},{58,5},{70,25},{70,60},{75,100},{65,180},{50,160},{40,140},{35,130},{22,120},{10,105},{2,103},{20,95},{8,77},{25,65},{12,44},{30,35},{36,20},{36,-10},{999,0},
 {36,-6},{32,-17},{15,-17},{5,-5},{5,10},{-5,12},{-25,15},{-35,20},{-25,33},{-10,40},{10,50},{12,44},{30,35},{32,20},{36,-6},{999,0},
 {-12,130},{-14,142},{-25,153},{-38,146},{-35,116},{-22,113},{-12,130},{999,0},
 {-34,173},{-41,176},{-47,167},{-40,172},{-34,173},{999,0},
 {50,-5},{59,-3},{58,-7},{50,-5},{999,0},{31,131},{36,138},{45,142},{40,140},{31,131}
};
static int ap_project(float lat, float lon, int *x, int *y)
{
    float a=lat*(FM_PI/180), d=(lon-globe_lon)*(FM_PI/180), b=globe_lat*(FM_PI/180);
    float z=fm_sinf(b)*fm_sinf(a)+fm_cosf(b)*fm_cosf(a)*fm_cosf(d);
    float r=44+10*globe_zoom;
    *x=56+(int)(r*fm_cosf(a)*fm_sinf(d));
    *y=80-(int)(r*(fm_cosf(b)*fm_sinf(a)-fm_sinf(b)*fm_cosf(a)*fm_cosf(d)));
    return z >= 0;
}
static void ap_globe(void)
{
    unsigned band, k; int x,y,px=0,py=0,visible=0;
    float target_lat=ap_now.lat*.0001f, target_lon=ap_now.lon*.0001f;
    uint8_t has_sound = play_url[0] != 0;
    if (ap_globe_valid && ap_globe_lat == ap_now.lat && ap_globe_lon == ap_now.lon &&
        ap_globe_has_sound == has_sound) return;
    ap_globe_valid = 1; ap_globe_lat = ap_now.lat; ap_globe_lon = ap_now.lon;
    ap_globe_has_sound = has_sound;
    globe_lat=target_lat; globe_lon=target_lon; globe_zoom=has_sound?1.f:0.f;
#ifdef PLAT_HOST
    ap_test_globe_draws++;
#endif
    for (band=0;band<4;band++) {
        cv_begin(112,40,K_BG);
        /* Local y is translated so clipping uses the existing 40-row buffer. */
        for (k=0;k<=120;k++) {
            float a=k*(FM_TWO_PI/120), r=44+10*globe_zoom;
            cv_pset(56+(int)(r*fm_cosf(a)),80+(int)(r*fm_sinf(a))-(int)band*40,K_LINE);
        }
        for (int lat=-60;lat<=60;lat+=30) {
            visible=0;
            for(int lon=-180;lon<=180;lon+=6) {
                int v=ap_project((float)lat,(float)lon,&x,&y);
                if(v&&visible) cv_line(px,py-(int)band*40,x,y-(int)band*40,K_LINE);
                px=x;py=y;visible=v;
            }
        }
        for (int lon=-180;lon<180;lon+=30) {
            visible=0;
            for(int lat=-90;lat<=90;lat+=6) {
                int v=ap_project((float)lat,(float)lon,&x,&y);
                if(v&&visible) cv_line(px,py-(int)band*40,x,y-(int)band*40,K_LINE);
                px=x;py=y;visible=v;
            }
        }
        visible=0;
        for(k=0;k<sizeof ap_coasts/sizeof ap_coasts[0];k++) {
            int v;
            if(ap_coasts[k][0]==999) { visible=0; continue; }
            v=ap_project((float)ap_coasts[k][0],(float)ap_coasts[k][1],&x,&y);
            if(v&&visible) cv_line(px,py-(int)band*40,x,y-(int)band*40,K_TEAL);
            px=x;py=y;visible=v;
        }
        if(play_url[0] && ap_project(target_lat,target_lon,&x,&y)) {
            cv_rect(x-2,y-2-(int)band*40,5,5,K_CORAL);
        }
        cv_blit(128,40+band*40);
    }
}
static void ap_left(unsigned row, const char *s, uint16_t color)
{
    char key[UI_KEY];
    int width=text_w(&FONT_XS,s), offset=0;
    if(width>112) {
        unsigned phase=(plat_ms()/60u)%(unsigned)(width+70);
        offset=phase<35?0:phase>=(unsigned)(width+35)?width-112:(int)phase-35;
        if(offset>width-112) offset=width-112;
    }
    snprintf(key,sizeof key,"%d/%x/%s",offset,color,s);
    if(w_eq(ui_key[row+1],key)) return;
    memcpy(ui_key[row+1],key,sizeof key);
    cv_begin(120,22,K_BG); cv_text(6-offset,3,&FONT_XS,s,color); cv_blit(0,40+row*22);
}
static void ap_coordinate(char *s, unsigned n, int32_t v, char plus, char minus)
{
    unsigned a=(unsigned)(v<0?-v:v);
    snprintf(s,n,"%u.%02u %c",a/10000,(a%10000)/100,v<0?minus:plus);
}
static void ui_home(void)
{
    char s[40];
    const char *status=!radio_want?"Stopped":wifi_st!=WS_CONNECTED?"Waiting for WiFi":
        ap_fetching?"Finding a sound...":ap_pending?ap_why[0]?ap_why:"Next sound...":radio_status_str();
    char buffering[32];
    if (radio_want && !ap_fetching && !ap_pending && radio_st == RS_BUFFERING) {
        uint32_t percent=(radio_wr-radio_rd)*100u/radio_buffer_target();
        snprintf(buffering,sizeof buffering,"Buffering %u%%",(unsigned)(percent>100?100:percent));
        status=buffering;
    }
    ui_title(0,"APOREE",wifi_st==WS_CONNECTED?"WiFi":"GLO WiFi");
    ap_left(0,ap_info?"RECORDING":ap_active_region?ap_region_name(ap_active_region):"WORLD / RANDOM",K_TEAL);
    ap_left(1,ap_info?ap_now.artist:ap_now.place,K_TEXT);
    ap_left(2,ap_now.title[0]?ap_now.title:"Field recordings",K_TEXT);
    if(ap_info) ap_left(3,ap_now.licence,K_DIM);
    else { ap_coordinate(s,sizeof s,ap_now.lat,'N','S'); ap_left(3,play_url[0]?s:"",K_DIM); }
    ap_coordinate(s,sizeof s,ap_now.lon,'E','W'); ap_left(4,ap_info?ap_now.id:play_url[0]?s:"",K_DIM);
    ap_left(5,status,K_CORAL);
    snprintf(s,sizeof s,"Index %u KB",(unsigned)(ap_bytes/1024));
    ap_left(6,ap_fetching?s:"SEL details",K_DIM);
    ap_globe();
    ui_row(13,214,26,"SAVE regions","OCT-/+ random",K_DIM,K_DIM,K_PANEL);
}
static void ap_regions_open(void)
{
    ap_browse_level=0;ap_browse_cursor=ap_browse_top=0;
}
static unsigned ap_browse_count(void)
{
    return !ap_browse_level?ap_country_total()+1u:ap_browse_level==1?ap_region_children(ap_browse_country)+1u:1u;
}
static uint16_t ap_browse_region(unsigned row)
{
    return !ap_browse_level?(row?ap_country_at(row-1u):0):
        ap_browse_level==1?(uint16_t)(ap_browse_country+row):ap_browse_pick;
}
static void ap_region_line(unsigned slot,unsigned y,const char *s,const char *right,int selected)
{
    char name[80],key[UI_KEY];unsigned n;
    snprintf(key,sizeof key,"%u/%d/%s/%s",y,selected,s,right);
    if(w_eq(ui_key[slot],key)) return;
    memcpy(ui_key[slot],key,sizeof key);
    w_cpy(name,s,sizeof name);n=(unsigned)strlen(name);
    while(n && text_w(&FONT_XS,name)>170) name[--n]=0;
    cv_begin(240,24,selected?K_PANEL:K_BG);
    cv_text(12,4,&FONT_XS,name,selected?K_CORAL:K_TEXT);
    cv_text(228-text_w(&FONT_XS,right),4,&FONT_XS,right,K_DIM);
    cv_blit(0,y);
}
static void ui_regions(void)
{
    unsigned n=ap_browse_count(),i;
    ui_title(0,"Regions",!ap_browse_level?"country":ap_browse_level==1?"state / area":"listen");
    ap_region_line(1,38,!ap_browse_level?"Worldwide":ap_region_name(ap_browse_level==1?ap_browse_country:ap_browse_pick),"",0);
    if(ap_browse_cursor<ap_browse_top) ap_browse_top=ap_browse_cursor;
    if(ap_browse_cursor>=ap_browse_top+6) ap_browse_top=ap_browse_cursor-5;
    for(i=0;i<6;i++) {
        unsigned row=(unsigned)ap_browse_top+i;
        const char *name="";char count[16]="";
        if(row<n) {
            uint16_t region=ap_browse_region(row);
            name=ap_browse_level==2?"Play random here":ap_browse_level==1&&!row?"Play whole country":ap_region_name(region);
            if(region) snprintf(count,sizeof count,"%u",ap_region_locations(region));
        }
        ap_region_line(2+i,64+i*24,name,count,row<n&&row==(unsigned)ap_browse_cursor);
    }
    ui_row(13,214,26,ap_browse_level==2?"SEL / PLAY listen":"SELECT move  SEL open","HOME up",K_DIM,K_DIM,K_PANEL);
}
static void ap_regions_play(void)
{
    ap_active_region=ap_browse_region((unsigned)ap_browse_cursor);
    ap_info=0;ap_shuffle();ui_page_set(ap_player_page());
}
static void ap_regions_input(uint32_t down,int32_t turn)
{
    int n=(int)ap_browse_count();
    if(turn) {
        ap_browse_cursor+=turn;
        if(ap_browse_cursor<0) ap_browse_cursor=0;
        if(ap_browse_cursor>=n) ap_browse_cursor=n-1;
    }
    if(down&(1u<<B_HOME)) {
        if(!ap_browse_level) ui_page_set(ap_player_page());
        else {ap_browse_level--;ap_browse_cursor=ap_browse_top=0;ui_dirty=1;}
        return;
    }
    if(down&(1u<<B_PLAY)) {ap_regions_play();return;}
    if(down&(1u<<B_SEL)) {
        if(ap_browse_level==2 || (!ap_browse_level&&!ap_browse_cursor)) {ap_regions_play();return;}
        if(!ap_browse_level) ap_browse_country=ap_browse_region((unsigned)ap_browse_cursor);
        else ap_browse_pick=ap_browse_region((unsigned)ap_browse_cursor);
        ap_browse_level++;ap_browse_cursor=ap_browse_top=0;ui_dirty=1;
    }
}
static void ui_help(void)
{
    ui_title(0,"Controls","");
    ui_row(1,42,24,"OCT- or OCT+","random sound",K_TEXT,K_TEAL,K_BG);
    ui_row(2,66,24,"PLAY","play / stop",K_TEXT,K_DIM,K_BG);
    ui_row(3,90,24,"GLO","WiFi setup",K_TEXT,K_DIM,K_BG);
    ui_row(4,114,24,"SEL","credits / details",K_TEXT,K_DIM,K_BG);
    ui_row(5,138,24,"MASTER","volume",K_TEXT,K_DIM,K_BG);
    ui_row(6,162,24,"Keys","hold for effects",K_TEXT,K_DIM,K_BG);
    ui_row(7,186,24,"SAVE","browse regions",K_TEXT,K_DIM,K_BG);
    ui_row(13,214,26,"Release HOME","",K_DIM,K_DIM,K_PANEL);
}
static void radio_ui_dump(void)
{
    wire_line(ap_now.place); wire_line(ap_now.title); wire_line(ap_now.artist); wire_line(ap_now.licence);
    wire_line(ap_why);
}
