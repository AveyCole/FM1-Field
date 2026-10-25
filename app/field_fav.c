/* SPDX-License-Identifier: MPL-2.0 */
/* Packed, versioned exact-recording bookmarks. Variable strings use the small
 * flash object efficiently, without truncating URLs or reserving audio RAM. */
#define FIELD_FAV_MAGIC 0x32464641u
#define FIELD_FAV_MAX 16u
typedef struct {
    uint32_t magic;
    uint16_t count, used;
    uint8_t data[PLAT_STORE_MAX-8u];
} field_favs_t;
typedef char field_favs_fit[(sizeof(field_favs_t)==PLAT_STORE_MAX)?1:-1];
static void field_favs_clear(field_favs_t *f)
{
    memset(f,0,sizeof *f); f->magic=FIELD_FAV_MAGIC;
}
/* An entry is two coordinates followed by URL, title, place, artist, licence,
 * and sound id. Validate every string before any list traversal. */
static int field_favs_valid(const field_favs_t *f,int len)
{
    unsigned at=0,i,j,k;
    const unsigned limits[]={512,96,96,64,32,16};
    if(len<8 || f->magic!=FIELD_FAV_MAGIC || f->count>FIELD_FAV_MAX ||
       f->used>sizeof f->data || len!=8+f->used) return 0;
    for(i=0;i<f->count;i++) {
        int32_t lat,lon;
        if(at+8>f->used) return 0;
        memcpy(&lat,f->data+at,4);memcpy(&lon,f->data+at+4,4);at+=8;
        if(lat < -900000 || lat > 900000 || lon < -1800000 || lon > 1800000) return 0;
        for(j=0;j<6;j++) {
            unsigned start=at;
            for(k=0;k<limits[j] && at<f->used && f->data[at];k++) at++;
            if(k==limits[j] || at>=f->used || (!j && at==start)) return 0;
            at++;
        }
    }
    return at==f->used;
}
static unsigned field_favs_at(const field_favs_t *f,unsigned i)
{
    unsigned at=0,j;
    while(i--) {at+=8;for(j=0;j<6;j++) while(f->data[at++]);}
    return at;
}
static const char *field_favs_string(const field_favs_t *f,unsigned i,unsigned which)
{
    const char *s=(const char *)f->data+field_favs_at(f,i)+8;
    while(which--) s+=strlen(s)+1;
    return s;
}
static int field_favs_add(field_favs_t *f,const ap_track_t *t)
{
    const char *s[]={t->url,t->title,t->place,t->artist,t->licence,t->id};
    unsigned i,n=8,at=f->used;
    if(!t->url[0]) return -3;
    for(i=0;i<f->count;i++)
        if(!strcmp(field_favs_string(f,i,0),t->url) ||
           (t->id[0] && !strcmp(field_favs_string(f,i,5),t->id))) return -2;
    for(i=0;i<6;i++) n+=(unsigned)strlen(s[i])+1;
    if(f->count==FIELD_FAV_MAX || n>sizeof f->data-at) return -1;
    memcpy(f->data+at,&t->lat,4);memcpy(f->data+at+4,&t->lon,4);at+=8;
    for(i=0;i<6;i++) {unsigned len=(unsigned)strlen(s[i])+1;memcpy(f->data+at,s[i],len);at+=len;}
    f->used=(uint16_t)at;f->count++;return 0;
}
static void field_favs_read(const field_favs_t *f,unsigned i,ap_track_t *t)
{
    const char *s=field_favs_string(f,i,0);
    unsigned at=field_favs_at(f,i);
    memcpy(&t->lat,f->data+at,4);memcpy(&t->lon,f->data+at+4,4);
#define FREAD(member) w_cpy(t->member,s,sizeof t->member);s+=strlen(s)+1
    FREAD(url);FREAD(title);FREAD(place);FREAD(artist);FREAD(licence);FREAD(id);
#undef FREAD
}
static void field_favs_delete(field_favs_t *f,unsigned i)
{
    unsigned at=field_favs_at(f,i),end=field_favs_at(f,i+1);
    memmove(f->data+at,f->data+end,f->used-end);
    f->used-=(uint16_t)(end-at);f->count--;
}
