/* Immutable character pixels replace only actor0's actual emitted OAM parts. */
#include "mm6_proto.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>
typedef struct {char magic[4];uint8_t valid,owner,owners[64];Mm6ProtoBody body;} ViewFrame;
static ViewFrame s_build,s_frame;
static int s_enabled;
static uint32_t s_bg[256*240];
static uint8_t s_opaque[864*240],s_claimed[864*240],s_allowed[64][240];
static int begin(uint16_t address) {
    (void)address;if(cyc_mod_isolated())return 0;
    memset(&s_build,0,sizeof s_build);memset(s_build.owners,255,sizeof s_build.owners);s_build.owner=255;
    if(mm6_proto_swapping()){mm6_proto_body(&s_build.body);s_build.valid=1;}
    return 0;
}
static int actor(uint16_t address) {
    (void)address;if(cyc_mod_isolated())return 0;
    CycModRegs r;cyc_mod_regs(&r);s_build.owner=r.x;
    if(!r.x){mm6_proto_body(&s_build.body);s_build.valid=1;}
    return 0;
}
static int part(uint16_t address) {
    (void)address;if(cyc_mod_isolated())return 0;
    CycModRegs r;cyc_mod_regs(&r);s_build.owners[((r.x+252)&255)/4]=s_build.owner;
    return 0;
}
static int dma(uint16_t address) {(void)address;if(!cyc_mod_isolated())s_frame=s_build;return 0;}
static int animation(uint16_t address) {
    (void)address;if(cyc_mod_isolated())return 0;
    CycModRegs r;cyc_mod_regs(&r);if(!r.x)mm6_proto_animation(r.y);return 0;
}
static uint32_t color(int index,const Mm6ProtoBody *b) {
    const CycProtoAtlas *atlas=mm6_proto_atlas();uint32_t c=atlas->colors[index-1];
    unsigned red=(c>>16)&255,green=(c>>8)&255,blue=c&255;
    if(b->weapon || b->charge>=0x24) {
        if(!red && !green && !blue)c=cyc_render_color_value(b->palette[1]);
        else if(red==green && green==blue && red>32 && red<240)c=cyc_render_color_value(b->palette[2]);
        else if(red>64 && red>green+blue)c=cyc_render_color_value(b->palette[3]);
    }
    if(b->palette[1]==15 && b->palette[2]==15 && b->palette[3]==15)c=0xff000000u;
    return c;
}
static void body(uint32_t *out,int W,int origin,const Mm6ProtoBody *b) {
    const CycProtoAtlas *atlas=mm6_proto_atlas();if(!atlas || b->pose>=CYC_PROTO_POSE_COUNT)return;
    const CycProtoPose *p=&atlas->poses[b->pose];
    for(int y=0;y<p->height;y++)for(int x=0;x<p->width;x++) {
        int index=p->pixels[y*32+x];if(!index)continue;
        int dx=x-p->origin_x,dy=y+p->top_y;
        int px=b->x+(b->right?-dx-1:dx),py=b->y+(b->flipped?-dy:dy+1);
        if(origin+px<0 || origin+px>=W || py<0 || py>=240 || !(cyc_render_line_mask(py)&16) ||
            (px<8 && !(cyc_render_line_mask(py)&4)))continue;
        int at=py*W+origin+px;if(s_claimed[at])continue;s_claimed[at]=1;
        if(!b->behind || !s_opaque[at])out[at]=color(index,b);
    }
}
static void sprite(uint32_t *out,int W,int origin,int slot,const uint8_t *oam) {
    const uint8_t *o=oam+slot*4;
    int top=o[0]+1,height=cyc_render_line_sprite16(top<240?top:239)?16:8;
    for(int y=0;y<height;y++) {
        int py=top+y;if(py>=240 || !s_allowed[slot][py] || !(cyc_render_line_mask(py)&16))continue;
        int row=(o[2]&128)?height-y-1:y;
        uint16_t a=height==16?(uint16_t)((o[1]&1)*4096+((o[1]&254)+row/8)*16+row%8):
            (uint16_t)(cyc_render_line_sprite_table(py)+o[1]*16+row);
        int lo=cyc_render_chr(a),hi=cyc_render_chr(a+8);
        for(int x=0;x<8;x++) {
            int px=o[3]+x,bit=(o[2]&64)?x:7-x,v=((lo>>bit)&1)|(((hi>>bit)&1)<<1);
            if(!v || origin+px>=W || (px<8 && !(cyc_render_line_mask(py)&4)))continue;
            int at=py*W+origin+px;if(s_claimed[at])continue;s_claimed[at]=1;
            if(!(o[2]&32) || !s_opaque[at])out[at]=cyc_render_color(16+(o[2]&3)*4+v);
        }
    }
}
static int render(uint32_t *out,int W,int H,int native_x0,const uint32_t *native,void *ctx) {
    (void)ctx;(void)native_x0;(void)native;
    if(cyc_mod_peek(0x3a0)!=4 || (cyc_mod_peek(0x3b7)&2))return 0;
    if(!s_enabled || !s_frame.valid || !s_frame.body.active || W<256 || W>864 || H!=240 || !cyc_render_background(s_bg))return 0;
    int origin=(W-256)/2;
    memset(s_opaque,0,sizeof s_opaque);memset(s_claimed,0,(size_t)W*H);
    for(int i=0;i<W*H;i++)out[i]=0xff000000u;
    const uint8_t *opaque=cyc_frame_bg_opaque();
    for(int y=0;y<H;y++) {
        memcpy(out+y*W+origin,s_bg+y*256,256*sizeof *out);
        memcpy(s_opaque+y*W+origin,opaque+y*256,256);
    }
    const uint8_t *oam=cyc_render_oam();uint8_t counts[240]={0};memset(s_allowed,0,sizeof s_allowed);
    int first=64;
    for(int slot=0;slot<64;slot++) {
        int top=oam[slot*4]+1,height=cyc_render_line_sprite16(top<240?top:239)?16:8;
        for(int y=top;y<top+height && y<240;y++)s_allowed[slot][y]=counts[y]++<8;
        if(s_frame.owners[slot]==0 && slot<first)first=slot;
    }
    for(int slot=0;slot<64;slot++) {
        if(s_frame.owners[slot]==0){if(slot==first)body(out,W,origin,&s_frame.body);}
        else sprite(out,W,origin,slot,oam);
    }
    if(first==64 && mm6_proto_swapping())body(out,W,origin,&s_frame.body);
    cyc_render_set_native_origin(origin);return 1;
}
void mm6_view_power(void) {
    memset(&s_build,0,sizeof s_build);memset(&s_frame,0,sizeof s_frame);
    memset(s_build.owners,255,sizeof s_build.owners);memset(s_frame.owners,255,sizeof s_frame.owners);
    s_build.owner=s_frame.owner=255;
}
void mm6_view_enable(int enabled) {
    s_enabled=enabled!=0;
    static const char *const ids[]={"begin","actor","part0","part1","part2","part3","dma","animation","animation-update"};
    for(int i=0;i<sizeof ids/sizeof *ids;i++){char id[48];snprintf(id,sizeof id,"mm6.view.%s",ids[i]);nes_mod_set_function_hook_enabled(id,s_enabled);}
    cyc_render_set_compositor(s_enabled?render:NULL,NULL);cyc_render_set_stock_compositing(s_enabled!=0);cyc_render_capture_background(s_enabled!=0);
}
static int get(uint8_t *out,int cap) {
    if(cap<sizeof s_build+sizeof s_frame)return -1;
    memcpy(s_build.magic,"M6V1",4);memcpy(s_frame.magic,"M6V1",4);
    memcpy(out,&s_build,sizeof s_build);memcpy(out+sizeof s_build,&s_frame,sizeof s_frame);return sizeof s_build+sizeof s_frame;
}
static int valid(const uint8_t *in,int len) {
    if(!len)return !s_enabled;if(!in || len!=sizeof s_build+sizeof s_frame)return 0;
    ViewFrame p[2];memcpy(p,in,sizeof p);
    for(int i=0;i<2;i++) {
        if(memcmp(p[i].magic,"M6V1",4) || p[i].valid>1 || (p[i].owner>22 && p[i].owner!=255))return 0;
        const Mm6ProtoBody *b=&p[i].body;
        if(b->pose>=CYC_PROTO_POSE_COUNT || b->active>1 || b->right>1 || b->flipped>1 || b->behind>1 || b->form>2)return 0;
        for(int j=0;j<64;j++)if(p[i].owners[j]>22 && p[i].owners[j]!=255)return 0;
    }
    return 1;
}
static int set(const uint8_t *in,int len) {if(!valid(in,len))return 0;if(len){memcpy(&s_build,in,sizeof s_build);memcpy(&s_frame,in+sizeof s_build,sizeof s_frame);}else mm6_view_power();return 1;}
void mm6_view_register(void) {
    nes_mod_register_function_entry_plugin("mm6.view.begin",0xe1dd,begin);
    nes_mod_register_function_entry_plugin("mm6.view.actor",0xe263,actor);
    nes_mod_register_function_entry_plugin("mm6.view.part0",0xe349,part);
    nes_mod_register_function_entry_plugin("mm6.view.part1",0xe387,part);
    nes_mod_register_function_entry_plugin("mm6.view.part2",0xe3c6,part);
    nes_mod_register_function_entry_plugin("mm6.view.part3",0xe40e,part);
    nes_mod_register_function_entry_plugin("mm6.view.dma",0xc02e,dma);
    nes_mod_register_function_entry_plugin("mm6.view.animation",0xe522,animation);
    nes_mod_register_function_entry_plugin("mm6.view.animation-update",0xe55d,animation);
    nes_mod_register_savestate_hook("mm6.view",get,set);nes_mod_register_savestate_validator("mm6.view",valid);
}
