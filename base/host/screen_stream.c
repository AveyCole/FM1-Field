/* SPDX-License-Identifier: MPL-2.0 */
/* Optional screen queue at full-speed USB-MIDI's 48 payload bytes/ms. */
static FILE *host_screen_out;
static uint8_t *host_screen_ring;
static uint32_t host_screen_w,host_screen_r,host_screen_t;
static uint32_t host_screen_available=1024u*1024u;
static void host_screen_file(const char *path)
{
    if(host_screen_out) fclose(host_screen_out);
    host_screen_out=path?fopen(path,"wb"):0;
}
static void host_screen_drain(uint32_t budget)
{
    while(budget-- && host_screen_r!=host_screen_w) {
        uint8_t b=host_screen_ring[host_screen_r++%PLAT_SCREEN_RING];
        if(host_screen_out) fwrite(&b,1,1,host_screen_out);
    }
}
uint32_t plat_screen_memory(void) {return host_screen_available;}
int plat_screen_link(void) {return host_screen_out?1:-1;}
void plat_screen_attach(uint8_t *ring)
{
    host_screen_ring=ring;host_screen_w=host_screen_r=0;host_screen_t=now_ms;
}
void plat_screen_detach(void) {host_screen_ring=0;host_screen_w=host_screen_r=0;}
uint32_t plat_screen_room(void)
{
    uint32_t dt=now_ms-host_screen_t;host_screen_t=now_ms;
    if(host_screen_ring) host_screen_drain(dt>1000?48000u:dt*48u);
    return host_screen_ring?PLAT_SCREEN_RING-(host_screen_w-host_screen_r):0;
}
int plat_screen_send(const uint8_t *p,uint32_t n)
{
    unsigned i;
    if(n>plat_screen_room()) return -1;
    for(i=0;i<n;i++) host_screen_ring[host_screen_w++%PLAT_SCREEN_RING]=p[i];
    return 0;
}
int plat_screen_reply(const uint8_t *p,uint32_t n)
{
    if(!host_screen_out) return -1;
    if(host_screen_ring) host_screen_drain(PLAT_SCREEN_RING);
    fwrite(p,1,n,host_screen_out);return 0;
}
