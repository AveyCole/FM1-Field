/* SPDX-License-Identifier: MPL-2.0 */
/* Optional, read-only pixel mirror. One small heap object owns packet scratch,
 * a low-priority USB queue and resumable rectangle hashes. No framebuffer. */
typedef struct {uint16_t x,y,w,h,row,pad;uint32_t hash;} screen_rect_t;
typedef struct {
    uint8_t ring[PLAT_SCREEN_RING],msg[740],wire[848];
    screen_rect_t rect[16];
    uint32_t heard,redraw,drops,frames;
    int link;
    uint16_t fill_x,fill_y,fill_w,fill_h,fill_color;
    uint8_t fill,next,dirty,seq;
} field_screen_t;
typedef char field_screen_fits[(sizeof(field_screen_t)<=4096u)?1:-1];
static field_screen_t *screen;
static unsigned screen_pack(uint8_t *m,unsigned cmd,unsigned n,uint8_t *wire)
{
    unsigned i,o=1,bits=0,sum=0;uint32_t acc=0;
    m[0]=0;m[1]=0x59;m[2]=(uint8_t)cmd;m[3]=(uint8_t)n;m[4]=(uint8_t)(n>>8);m[5]=0;
    for(i=0;i<n;i++) sum+=m[6+i];
    m[6+n]=(uint8_t)~sum;wire[0]=0xF0;
    for(i=0;i<n+7u;i++) {
        acc|=(uint32_t)m[i]<<bits;bits+=8;
        while(bits>=7) {wire[o++]=(uint8_t)(acc&127u);acc>>=7;bits-=7;}
    }
    if(bits) wire[o++]=(uint8_t)acc;
    wire[o++]=0xF7;return o;
}
static void screen_hello(unsigned error)
{
    uint8_t m[12],w[20];
    m[6]=1;m[7]=screen!=0;m[8]=239;m[9]=239;m[10]=(uint8_t)error;
    (void)plat_screen_reply(w,screen_pack(m,0x50,5,w));
}
static void screen_redraw(void)
{
    unsigned i;
    for(i=0;i<UI_ROWS;i++) ui_key[i][0]=0;
    ap_globe_valid=0;
}
static void screen_stop(void)
{
    plat_screen_detach();free(screen);screen=0;
}
static void screen_command(unsigned op)
{
    int link=plat_screen_link();
    if(!op) {screen_stop();screen_hello(0);return;}
    if(link<0) return;
    if(screen && link!=screen->link) screen_stop();
    if(!screen) {
        if(plat_screen_memory()<sizeof *screen+8192u || !(screen=malloc(sizeof *screen))) {screen_hello(1);return;}
        memset(screen,0,sizeof *screen);screen->link=link;plat_screen_attach(screen->ring);op=2;
    }
    screen->heard=plat_ms();
    if(op==2) {
        memset(screen->rect,0,sizeof screen->rect);
        screen->fill=1;screen->fill_x=screen->fill_y=0;screen->fill_w=screen->fill_h=240;
        screen->fill_color=K_BG;screen->dirty=1;screen->redraw=plat_ms()-100u;
    }
    screen_hello(0);
}
static int screen_send(unsigned cmd,unsigned n)
{
    unsigned bytes=screen_pack(screen->msg,cmd,n,screen->wire);
    if(plat_screen_send(screen->wire,bytes)) {screen->dirty=1;screen->drops++;return 0;}
    screen->frames++;screen->seq++;return 1;
}
static int screen_fill_pending(void)
{
    uint8_t *m;
    if(!screen->fill) return 1;
    m=screen->msg+6;m[0]=(uint8_t)screen->fill_x;m[1]=(uint8_t)screen->fill_y;
    m[2]=(uint8_t)screen->fill_w;m[3]=(uint8_t)screen->fill_h;m[4]=screen->seq;
    m[5]=(uint8_t)(screen->fill_color>>8);m[6]=(uint8_t)screen->fill_color;
    if(!screen_send(0x52,7)) return 0;
    screen->fill=0;return 1;
}
void app_screen_fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint16_t color)
{
    if(!screen || !w || !h) return;
    memset(screen->rect,0,sizeof screen->rect);
    screen->fill_x=(uint16_t)x;screen->fill_y=(uint16_t)y;screen->fill_w=(uint16_t)w;
    screen->fill_h=(uint16_t)h;screen->fill_color=color;screen->fill=screen->dirty=1;
    (void)screen_fill_pending();
}
void app_screen_blit(uint32_t x,uint32_t y,uint32_t w,uint32_t h,const uint16_t *px)
{
    unsigned i,slot=16;uint32_t hash=2166136261u;screen_rect_t *r;
    if(!screen || !w || !h || x+w>240 || y+h>240 || !screen_fill_pending()) return;
    for(i=0;i<w*h;i++) hash=(hash^px[i])*16777619u;
    for(i=0;i<16;i++) if(screen->rect[i].w==w && screen->rect[i].h==h &&
        screen->rect[i].x==x && screen->rect[i].y==y) {slot=i;break;}
    if(slot==16) {slot=screen->next++%16u;memset(&screen->rect[slot],0,sizeof screen->rect[slot]);}
    r=&screen->rect[slot];
    if(r->hash!=hash || r->w!=w || r->h!=h || r->x!=x || r->y!=y) {
        r->x=(uint16_t)x;r->y=(uint16_t)y;r->w=(uint16_t)w;r->h=(uint16_t)h;r->row=0;r->hash=hash;
    }
    while(r->row<h) {
        const uint16_t *p=px+r->row*w;uint8_t *m=screen->msg+6;unsigned at=5,col=0;
        /* Only literal-colour runs are needed: the standard mirror decoder
         * accepts them and the worst row is bounded at 720 coded bytes. */
        m[0]=(uint8_t)x;m[1]=(uint8_t)(y+r->row);m[2]=(uint8_t)w;m[3]=1;m[4]=screen->seq;
        while(col<w) {
            unsigned run=1;uint16_t c=p[col];
            while(col+run<w && run<64 && p[col+run]==c) run++;
            m[at++]=(uint8_t)(0xC0u+run-1u);m[at++]=(uint8_t)c;m[at++]=(uint8_t)(c>>8);col+=run;
        }
        if(!screen_send(0x51,at)) return;
        r->row++;
    }
}
static void screen_step(void)
{
    if(!screen) return;
    if(plat_screen_link()!=screen->link || plat_ms()-screen->heard>6000u) {screen_stop();return;}
    (void)plat_screen_room();
    if(!screen_fill_pending()) return;
    if(screen->dirty && plat_ms()-screen->redraw>=100u && plat_screen_room()>=1024u) {
        screen->dirty=0;screen->redraw=plat_ms();screen_redraw();
    }
}
