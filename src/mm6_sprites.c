/* Immutable full native sprite packets, independent of physical OAM capacity. */
#include "mm6_sprites.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_savestate.h"
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
enum { PART_CAP=4096 };
typedef struct {uint8_t x,y,owner,attr,colors[4],pixels[64];} Part;
typedef struct {char magic[4];uint16_t count;uint8_t valid;Part parts[PART_CAP];} Frame;
static Frame B,F;
static int s_active,s_capture,s_owner,s_failed,s_body;
void mm6_sprites_power(void){memset(&B,0,sizeof B);memset(&F,0,sizeof F);s_capture=0;}
void mm6_sprites_active(int active){s_active=active!=0;}
void mm6_sprites_part(void){
    if(!s_capture || !cyc_mod_isolated())return;
    CycModRegs r;cyc_mod_regs(&r);int offset=(r.x+252)&255,top=R(0x200+offset)+1;
    if(top<240){
        if(B.count==PART_CAP)s_failed=1;
        else{
            Part *p=&B.parts[B.count++];p->x=R(0x203+offset);p->y=(uint8_t)top;p->owner=(uint8_t)s_owner;p->attr=R(0x202+offset);
            for(int i=0;i<4;i++)p->colors[i]=R(0x370+(p->attr&3)*4+i)&63;
            int tile=R(0x201+offset),table=cyc_render_line_sprite_table(top);
            for(int y=0;y<8;y++){
                int row=(p->attr&128)?7-y:y,address=table+tile*16+row;
                int low=cyc_render_chr((uint16_t)address),high=cyc_render_chr((uint16_t)(address+8));
                for(int x=0;x<8;x++){int bit=(p->attr&64)?x:7-x;p->pixels[y*8+x]=(uint8_t)(((low>>bit)&1)|(((high>>bit)&1)<<1));}
            }
            if(!s_owner)s_body=1;
        }
    }
    /* Reuse the scratch OAM page as a ring. Only the pure capture call skips
     * the emitter's full-page return; original animation and hardware stay intact. */
    if(!r.x && !s_failed){r.p&=(uint8_t)~2;cyc_mod_set_regs(&r);}
}
int mm6_sprites_capture(void){
    memset(&B,0,sizeof B);
    if(!s_active || cyc_mod_isolated() || R(0x3A0)!=4 || (R(0x3B7)&2))return 0;
    if(!cyc_mod_isolate_begin())return 0;
    s_capture=1;s_failed=s_body=0;cyc_mod_allow_isolated_hooks(true);int ok=1;
    for(int order=22;order>=0 && ok && !s_failed;order--){
        int slot=R(0x7D9+order);if(slot>22){s_failed=1;break;}
        if(!R(0x3A0+slot) || (R(0x3B7+slot)&3) || R(0x4B4+slot))continue;
        int relative=(R(0x486+slot)-R(0xF7))&255;if(relative<4)continue;
        CycModRegs r;cyc_mod_regs(&r);r.y=R(0x458+slot);ok=cyc_mod_call(0xCBC0,&r);
        for(int i=0;i<256;i+=4)P(0x200+i,240);
        P(0x8C,0);P(0x00,relative);r.x=(uint8_t)slot;s_owner=slot;
        if(ok)ok=cyc_mod_call(0xE2A9,&r);
    }
    s_capture=0;cyc_mod_allow_isolated_hooks(false);cyc_mod_isolate_end();
    B.valid=(uint8_t)(ok && !s_failed);return B.valid && s_body;
}
void mm6_sprites_latch(void){F=B;}
int mm6_sprites_ready(void){return s_active && F.valid;}
void mm6_sprites_draw(uint32_t *out,int width,int origin,const Mm6ProtoBody *body,int replace,const uint8_t *opaque,uint8_t *claimed){
    if(!mm6_sprites_ready())return;int drawn=0;
    for(int i=0;i<F.count;i++){
        const Part *p=&F.parts[i];
        if(replace && !p->owner){if(!drawn){mm6_view_draw_character(out,width,origin,body);drawn=1;}continue;}
        for(int y=0;y<8;y++)for(int x=0;x<8;x++){
            int v=p->pixels[y*8+x],px=origin+p->x+x,py=p->y+y;
            if(!v || px<0 || px>=width || py>=240 || !(cyc_render_line_mask(py)&16) || (px<8 && !(cyc_render_line_mask(py)&4)))continue;
            int at=py*width+px;if(claimed[at])continue;claimed[at]=1;
            if(!(p->attr&32) || !opaque[at]){int c=(int)p->colors[v]-(int)R(0x48);out[at]=cyc_render_color_value(c<0?15:c&63);}
        }
    }
}
static int get(uint8_t *out,int cap){
    if(cap<sizeof B+sizeof F)return -1;memcpy(B.magic,"M6S1",4);memcpy(F.magic,"M6S1",4);
    memcpy(out,&B,sizeof B);memcpy(out+sizeof B,&F,sizeof F);return sizeof B+sizeof F;
}
static int valid(const uint8_t *in,int len){
    if(!len)return !s_active;if(!in || len!=sizeof B+sizeof F)return 0;
    static Frame frames[2];memcpy(frames,in,sizeof frames);
    for(int i=0;i<2;i++){
        const Frame *f=&frames[i];if(memcmp(f->magic,"M6S1",4) || f->count>PART_CAP || f->valid>1)return 0;
        for(int j=0;j<f->count;j++){const Part *p=&f->parts[j];if(p->owner>22 || p->y>=240)return 0;for(int k=0;k<4;k++)if(p->colors[k]>63)return 0;for(int k=0;k<64;k++)if(p->pixels[k]>3)return 0;}
    }
    return 1;
}
static int set(const uint8_t *in,int len){if(!valid(in,len))return 0;if(len){memcpy(&B,in,sizeof B);memcpy(&F,in+sizeof B,sizeof F);}else mm6_sprites_power();return 1;}
void mm6_sprites_register(void){nes_mod_register_savestate_hook("mm6.sprites",get,set);nes_mod_register_savestate_validator("mm6.sprites",valid);}
