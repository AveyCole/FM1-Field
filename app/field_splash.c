/* SPDX-License-Identifier: MPL-2.0 */
/* Original artwork drawn in the existing canvas: no image allocation,
 * animation, extra font or artificial startup delay. */
void app_splash(void)
{
    static const uint8_t letters[5][7]={
        {31,16,16,30,16,16,16}, {31,4,4,4,4,4,31},
        {31,16,16,30,16,16,31}, {16,16,16,16,16,16,31},
        {30,17,17,17,17,17,30}
    };
    static const int16_t terrain[][2]={
        {18,96},{37,94},{57,87},{75,91},{95,99},{114,94},
        {134,85},{153,91},{172,82},{193,90},{222,86}
    };
    static const uint8_t wave[]={2,4,3,7,12,5,16,9,22,13,7,18,25,11,5,14,20,8,16,10,4,8,3,5,2,1};
    unsigned band,k,row,col;
    for(band=0;band<6;band++) {
        int y=(int)band*40,px=0,py=0;
        cv_begin(240,40,K_BG);
        cv_text(20,15-y,&FONT_S,"FM1",K_TEXT);
        cv_text(220-text_w(&FONT_XS,"FIELD RECORDINGS"),17-y,&FONT_XS,"FIELD RECORDINGS",K_DIM);
        cv_line(20,36-y,220,36-y,K_LINE);
        for(k=0;k<=96;k++) {
            float a=k*(FM_TWO_PI/96);
            int x=120+(int)(34*fm_cosf(a)),yy=77+(int)(34*fm_sinf(a));
            if(k) cv_line(px,py-y,x,yy-y,K_LINE);
            px=x;py=yy;
        }
        for(row=0;row<3;row++)
            for(k=1;k<sizeof terrain/sizeof terrain[0];k++)
                cv_line(terrain[k-1][0],terrain[k-1][1]+(int)row*6-y,
                        terrain[k][0],terrain[k][1]+(int)row*6-y,K_LINE);
        for(k=0;k<sizeof wave;k++) cv_rect(69+(int)k*4,76-wave[k]/2-y,2,wave[k],K_CORAL);
        for(k=0;k<5;k++) for(row=0;row<7;row++) for(col=0;col<5;col++)
            if(letters[k][row]&(16u>>col)) cv_rect(33+(int)k*36+(int)col*6,130+(int)row*6-y,6,6,K_TEXT);
        cv_text((240-text_w(&FONT_S,"THE WORLD, RECORDED."))/2,184-y,&FONT_S,"THE WORLD, RECORDED.",K_DIM);
        cv_line(20,211-y,220,211-y,K_LINE);
        cv_rect(69,225-y,4,4,K_CORAL);
        cv_text(80,219-y,&FONT_XS,"LISTEN CLOSELY",K_DIM);
        cv_blit(0,(unsigned)y);
    }
}
