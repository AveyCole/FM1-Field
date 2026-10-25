/* SPDX-License-Identifier: MPL-2.0 */
/* Still landscape, drawn in the existing canvas strips. No sample decoding or
 * animation in the UI; held keyboard notes do not change this artwork. */
static void ui_landscape(int river)
{
    unsigned band;int x,y;
    const char *key=river?"river":"landscape";
    if(w_eq(ui_key[2],key)) return;
    w_cpy(ui_key[2],key,UI_KEY);
    for(band=0;band<3;band++) {
        int top=(int)band*26;
        cv_begin(240,26,K_BG);
        for(y=0;y<26;y++) for(x=12;x<228;x++) {
            int yy=(y+top)*96/78,dx=x-181,dy=yy-21;
            uint16_t color=K_BG;
            int far=43+(x<95?(95-x)/4:(x-95)/7);
            int near=62+(x<156?(156-x)/10:(x-156)/5);
            if(dx*dx+dy*dy<100) color=K_CORAL;
            if(yy>=far) color=K_LINE;
            if(yy>=near) color=K_PANEL;
            /* A quiet cream river widens toward the foreground. */
            if(!river && yy>=59) {
                int center=127+(yy-59)*(yy-59)/75,half=1+(yy-59)/7;
                if(x>=center-half && x<=center+half) color=K_DIM;
            }
            if(river && yy>=48) {
                int t=yy-48,center=115+(t<20?-t/2:(t-20)*(t-20)/24-10),half=2+t/3;
                if(x>=center-half-1 && x<=center+half+1) color=K_CORAL;
                if(x>=center-half && x<=center+half) color=yy%12==0?K_TEXT:K_DIM;
            }
            cv_rect(x,y,1,1,color);
        }
        for(x=0;x<7;x++) {
            int tx=29+x*15,base=86-(x%3)*4,height=16+(x%3)*3;
            for(y=0;y<height;y++) {
                int half=y/3;
                cv_rect(tx-half,(base-height+y)*78/96-top,half*2+1,1,K_TEAL);
            }
            cv_rect(tx,(base-2)*78/96-top,1,5,K_TEAL);
        }
        cv_blit(0,90+(unsigned)top);
    }
}
static void ui_knobs(const char *const *labels,const char *a,const char *b,const char *c,const char *d)
{
    const char *values[4]={a,b,c,d};
    char key[UI_KEY];unsigned i;
    snprintf(key,sizeof key,"%.12s|%.12s|%.12s|%.12s",a,b,c,d);
    if(w_eq(ui_key[3],key)) return;
    w_cpy(ui_key[3],key,UI_KEY);
    cv_begin(240,40,K_BG);
    for(i=0;i<4;i++) {
        int left=8+(int)i*59;
        cv_text(left,3,&FONT_XS,labels[i],K_DIM);
        cv_text(left,19,&FONT_S,values[i],K_TEXT);
    }
    cv_blit(0,172);
}
