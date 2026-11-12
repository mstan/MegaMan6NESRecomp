/* Native CFCA meters, captured once for each seat and latched with DMA. */
#include "mm6_hud.h"
#include "mm6_coop.h"
#include "mm6_proto.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_savestate.h"
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
enum { BAR=8*56 };
typedef struct {char magic[4];uint8_t valid,boss_on,character[2],colors[2][8],boss_colors[4],meters[2][2][BAR],boss[BAR];} Hud;
static Hud B,F;
void mm6_hud_power(void){memset(&B,0,sizeof B);memset(&F,0,sizeof F);}
static int meter(uint8_t *out,int amount,int stop,int palette){
    memset(out,0,BAR);if(!cyc_mod_isolate_begin())return 0;
    for(int i=0;i<256;i+=4)P(0x200+i,240);
    P(0x8C,0);P(1,amount);P(2,0);P(3,stop);P(4,palette);
    CycModRegs r;cyc_mod_regs(&r);r.y=0;int ok=cyc_mod_call(0xCFCA,&r),count=R(0x8C)/4;
    if(ok && count<=7)for(int i=0;i<count;i++){
        int top=R(0x200+i*4)+1,tile=R(0x201+i*4),attr=R(0x202+i*4);
        for(int y=0;y<8;y++){
            int dy=top-25+y;if(dy<0 || dy>=56)continue;
            int a=cyc_render_line_sprite_table(top)+tile*16+y,lo=cyc_render_chr((uint16_t)a),hi=cyc_render_chr((uint16_t)(a+8));
            for(int x=0;x<8;x++){int bit=7-x,v=((lo>>bit)&1)|(((hi>>bit)&1)<<1);if(v)out[dy*8+x]=(uint8_t)((attr&3)*4+v);}
        }
    }else ok=0;
    cyc_mod_isolate_end();return ok;
}
void mm6_hud_capture(void){
    memset(&B,0,sizeof B);
    if(!mm6_coop_ready() || cyc_mod_isolated() || R(0x3A0)!=4 || (R(0x3B7)&2))return;
    Hud next={0};int ok=1;
    for(int seat=0;seat<2 && ok;seat++){
        next.character[seat]=(uint8_t)(mm6_proto_enabled() && mm6_coop_character_for(seat));
        for(int i=0;i<8;i++)next.colors[seat][i]=(uint8_t)mm6_coop_read(seat,0x370+i);
        ok=meter(next.meters[seat][0],mm6_coop_read(seat,0x3E5),16,1);
        int weapon=mm6_coop_read(seat,0x699),form=mm6_coop_read(seat,0x9B);
        if(weapon>0 && weapon<=9)ok=ok && meter(next.meters[seat][1],mm6_coop_read(seat,0x688+weapon),16,0);
        else if(form==1 || form==2)ok=ok && meter(next.meters[seat][1],mm6_coop_read(seat,form==1?0x9C:0xA5),form==1?32:40,0);
    }
    next.boss_on=R(0x642)!=0;
    if(next.boss_on){for(int i=0;i<4;i++)next.boss_colors[i]=R(0x37C+i);ok=ok && meter(next.boss,R(0x3ED),16,3);}
    if(ok){next.valid=1;B=next;}
}
void mm6_hud_latch(void){F=B;}
int mm6_hud_native_part(const uint8_t *p){
    return mm6_coop_ready() && F.valid && p && p[0]>=24 && p[0]<=72 &&
        (p[3]==16 || p[3]==24 || p[3]==40) && ((p[1]>=0x81 && p[1]<=0x84) || p[1]==0x9F);
}
void mm6_hud_draw(uint32_t *out,int width,int height,int own){
    if(!mm6_coop_ready() || !F.valid || !out)return;
    for(int seat=0;seat<2;seat++){
        if(own>=0 && own!=seat)continue;
        int top=20+(own<0?seat*76:0);
        for(int which=0;which<2;which++)for(int y=0;y<56;y++)for(int x=0;x<8;x++){
            int v=F.meters[seat][which][y*8+x],px=8+which*16+x,py=top+y;if(!v || px>=width || py>=height)continue;
            int color_index=(int)F.colors[seat][v&7]-(int)R(0x48);uint32_t color=cyc_render_color_value(color_index<0?15:color_index&63);
            if(!which && F.character[seat] && (color&0xFFFFFFu)){
                unsigned red=(color>>16)&255,green=(color>>8)&255,blue=color&255;
                color=0xFF000000u|((red*3+255)/4<<16)|((green*3+64)/4<<8)|(blue*3+64)/4;
            }
            out[py*width+px]=color;
        }
    }
    if(F.boss_on)for(int y=0;y<56;y++)for(int x=0;x<8;x++){
        int v=F.boss[y*8+x],px=width-16+x,py=20+y;if(v && px>=0 && px<width && py<height){int c=(int)F.boss_colors[v&3]-(int)R(0x48);out[py*width+px]=cyc_render_color_value(c<0?15:c&63);}
    }
}
static int get(uint8_t *out,int cap){if(cap<sizeof B+sizeof F)return -1;memcpy(B.magic,"M6H1",4);memcpy(F.magic,"M6H1",4);memcpy(out,&B,sizeof B);memcpy(out+sizeof B,&F,sizeof F);return sizeof B+sizeof F;}
static int valid(const uint8_t *in,int len){
    if(!len)return !mm6_coop_enabled();if(!in || len!=sizeof B+sizeof F)return 0;
    for(int k=0;k<2;k++){
    Hud h;memcpy(&h,in+k*sizeof h,sizeof h);if(memcmp(h.magic,"M6H1",4) || h.valid>1 || h.boss_on>1)return 0;
    for(int seat=0;seat<2;seat++){if(h.character[seat]>1)return 0;for(int i=0;i<8;i++)if(h.colors[seat][i]>63)return 0;for(int m=0;m<2;m++)for(int i=0;i<BAR;i++)if(h.meters[seat][m][i]>7)return 0;}
    for(int i=0;i<4;i++)if(h.boss_colors[i]>63)return 0;for(int i=0;i<BAR;i++)if(h.boss[i]>15)return 0;}return 1;
}
static int set(const uint8_t *in,int len){if(!valid(in,len))return 0;if(len){memcpy(&B,in,sizeof B);memcpy(&F,in+sizeof B,sizeof F);}else mm6_hud_power();return 1;}
void mm6_hud_register(void){nes_mod_register_savestate_hook("mm6.hud",get,set);nes_mod_register_savestate_validator("mm6.hud",valid);}
