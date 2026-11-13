/* Immutable character pixels replace only actor0's actual emitted OAM parts. */
#include "mm6_proto.h"
#include "mm6_coop.h"
#include "mm6_sprites.h"
#include "mm6_world.h"
#include "mm6_wide.h"
#include "mm6_hud.h"
#include "mm6_menu.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "cyc_video.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>
typedef struct {
    char magic[4];uint8_t valid,owner,owners[64];Mm6ProtoBody body;
    uint8_t panels,native_scene;uint16_t camera;int32_t left[2];
    uint8_t tiles[2][30][33],palettes[2][30][33],present[2][30][33];
} ViewFrame;
static ViewFrame s_build,s_frame;
static int s_enabled,s_coop,s_no_flicker,s_wide,s_adapter_drawn,s_panel_drawing,s_capture_policy=-1;
static uint32_t s_bg[256*240];
static uint8_t s_opaque[864*240],s_claimed[864*240],s_allowed[64][240];
static uint32_t s_panel[2][256*240];
int mm6_view_panel_painting(void){return s_panel_drawing;}
static void panels_build(void){
    int left[2],lo,hi;
    if(!mm6_coop_view_rects(left) || cyc_mod_peek(0x51)>=16)return;
    s_build.panels=1;s_build.camera=(uint16_t)((cyc_mod_peek(0x57)<<8)|cyc_mod_peek(0x56));
    s_build.native_scene=(uint8_t)(mm6_coop_scene_locked() || !mm6_world_bounds(&lo,&hi));
    for(int seat=0;seat<2;seat++){
        s_build.left[seat]=left[seat];if(s_build.native_scene)continue;
        for(int y=0;y<30;y++)for(int x=0;x<33;x++){
            int wx=(left[seat]&~7)+x*8;Mm6WorldTile tile;
            if(wx<lo || wx>=hi || !mm6_world_tile(wx,y*8,&tile))continue;
            s_build.tiles[seat][y][x]=tile.tile;s_build.palettes[seat][y][x]=tile.palette;s_build.present[seat][y][x]=1;
        }
    }
}
static int begin(uint16_t address) {
    (void)address;if(cyc_mod_isolated())return 0;
    memset(&s_build,0,sizeof s_build);memset(s_build.owners,255,sizeof s_build.owners);s_build.owner=255;
    mm6_coop_build();
    panels_build();
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
    (void)address;if(cyc_mod_isolated()){mm6_sprites_part();return 0;}
    CycModRegs r;cyc_mod_regs(&r);s_build.owners[((r.x+252)&255)/4]=s_build.owner;
    return 0;
}
static int dma(uint16_t address) {(void)address;if(!cyc_mod_isolated()){s_frame=s_build;mm6_coop_latch();mm6_sprites_latch();mm6_hud_latch();}return 0;}
static int hud(uint16_t address){(void)address;if(!cyc_mod_isolated())mm6_hud_capture();return 0;}
static int complete(uint16_t address){
    (void)address;if(!cyc_mod_isolated() && mm6_sprites_capture()){mm6_proto_body(&s_build.body);s_build.valid=1;}return 0;
}
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
            (!s_panel_drawing && origin+px<8 && !(cyc_render_line_mask(py)&4)))continue;
        int at=py*W+origin+px;if(s_claimed[at])continue;s_claimed[at]=1;
        if(!b->behind || !s_opaque[at])out[at]=color(index,b);
    }
}
void mm6_view_draw_character(uint32_t *out,int width,int origin,const Mm6ProtoBody *b){body(out,width,origin,b);}
int mm6_view_adapter_head_mask(const Mm6ProtoBody *b,int x,int y) {
    if(b->active!=2)return 0;
    int dx=x-b->x,dy=b->flipped?b->y-y:y-b->y-1;
    if(b->right)dx=-dx-1;
    return dx>=-8 && dx<=8 && dy>=-13 && dy<=-1;
}
void mm6_view_draw_adapter_head(uint32_t *out,int W,int origin,const Mm6ProtoBody *b) {
    const CycProtoAtlas *atlas=mm6_proto_atlas();if(!atlas || b->active!=2)return;
    const CycProtoPose *p=&atlas->poses[CYC_PROTO_IDLE0];
    /* The credited idle's helmet, visor and scarf share the native adapter's
     * body anchor. Its lower body is deliberately excluded. Wings, shoulder
     * armor, punches, exhaust and their animation remain native packets. */
    for(int y=8;y<20;y++)for(int x=0;x<p->width;x++) {
        int index=p->pixels[y*32+x];if(!index)continue;
        int dx=x-p->origin_x,dy=y+p->top_y;
        int px=origin+b->x+(b->right?-dx-1:dx),py=b->y+(b->flipped?-dy:dy+1);
        if(px<0 || px>=W || py<0 || py>=240 || !(cyc_render_line_mask(py)&16) ||
            (!s_panel_drawing && px<8 && !(cyc_render_line_mask(py)&4)))continue;
        int at=py*W+px;if(s_claimed[at])continue;s_claimed[at]=1;
        if(!b->behind || !s_opaque[at])out[at]=color(index,b);
    }
}
static void sprite(uint32_t *out,int W,int origin,int slot,const uint8_t *oam) {
    const uint8_t *o=oam+slot*4;
    int adapter=s_frame.owners[slot]==0 && s_frame.body.active==2;
    if(adapter && !s_adapter_drawn){mm6_view_draw_adapter_head(out,W,origin,&s_frame.body);s_adapter_drawn=1;}
    int top=o[0]+1,height=cyc_render_line_sprite16(top<240?top:239)?16:8;
    for(int y=0;y<height;y++) {
            int py=top+y;if(py>=240 || (!s_no_flicker && !s_allowed[slot][py]) || !(cyc_render_line_mask(py)&16))continue;
        int row=(o[2]&128)?height-y-1:y;
        uint16_t a=height==16?(uint16_t)((o[1]&1)*4096+((o[1]&254)+row/8)*16+row%8):
            (uint16_t)(cyc_render_line_sprite_table(py)+o[1]*16+row);
        int lo=cyc_render_chr(a),hi=cyc_render_chr(a+8);
        for(int x=0;x<8;x++) {
            int px=o[3]+x,bit=(o[2]&64)?x:7-x,v=((lo>>bit)&1)|(((hi>>bit)&1)<<1);
            if(!v || origin+px>=W || (!s_panel_drawing && origin+px<8 && !(cyc_render_line_mask(py)&4)))continue;
            if(adapter && mm6_view_adapter_head_mask(&s_frame.body,px,py))continue;
            int at=py*W+origin+px;if(s_claimed[at])continue;s_claimed[at]=1;
            if(!(o[2]&32) || !s_opaque[at]){
                int palette=o[2]&3;
                if(s_coop && s_frame.valid && s_frame.owners[slot]<8 && palette<2){
                    int c=(int)s_frame.body.palette[palette*4+v]-(int)cyc_mod_peek(0x48);
                    out[at]=cyc_render_color_value(c<0?15:c&63);
                }else out[at]=cyc_render_color(16+palette*4+v);
            }
        }
    }
}
static void panel_scene(uint32_t *out,int seat){
    int left=s_frame.left[seat],phase=left&7;
    memset(s_opaque,0,256*240);memset(s_claimed,0,256*240);s_panel_drawing=1;
    const uint8_t *opaque=cyc_frame_bg_opaque();
    for(int y=0;y<240;y++)for(int x=0;x<256;x++){
        int at=y*256+x,value=0,sy=cyc_render_line_scroll_y(y)%240,cell=(phase+x)/8,bit=7-((phase+x)&7);
        if(s_frame.native_scene){out[at]=s_bg[at];s_opaque[at]=opaque[at];continue;}
        if(s_frame.present[seat][sy/8][cell] && (cyc_render_line_mask(y)&8)){
            int a=cyc_render_line_bg_table(y)+s_frame.tiles[seat][sy/8][cell]*16+(sy&7);
            value=((cyc_render_chr((uint16_t)a)>>bit)&1)|(((cyc_render_chr((uint16_t)(a+8))>>bit)&1)<<1);
        }
        out[at]=cyc_render_color(value?s_frame.palettes[seat][sy/8][cell]*4+value:0);s_opaque[at]=(uint8_t)(value!=0);
    }
    int origin=s_frame.camera-left,replace=s_enabled && s_frame.valid && s_frame.body.active==1;
    mm6_sprites_draw(out,256,origin,&s_frame.body,replace,s_opaque,s_claimed,!s_no_flicker,NULL,0);
    mm6_coop_draw(out,256,origin,s_opaque,s_claimed);mm6_hud_draw(out,256,240,seat);s_panel_drawing=0;
}
static int panels(uint32_t *out,int width,int height){
    int mode=mm6_coop_view_mode();if(!mode || !s_frame.panels || width<256 || height!=240)return 0;
    if(mode==1){
        panel_scene(s_panel[0],0);panel_scene(s_panel[1],1);int half=width/2;
        for(int y=0;y<240;y++)for(int x=0;x<width;x++){
            int seat=x>=half,local=x-(seat?half:0),span=seat?width-half:half;
            out[y*width+x]=s_panel[seat][y*256+local*256/span];
        }
    }else{
        int seat=mode==3;panel_scene(s_panel[seat],seat);
        for(int y=0;y<240;y++)for(int x=0;x<width;x++)out[y*width+x]=s_panel[seat][y*256+x*256/width];
    }
    cyc_render_set_native_origin(0);return 1;
}
static int render(uint32_t *out,int W,int H,int native_x0,const uint32_t *native,void *ctx) {
    (void)ctx;(void)native_x0;(void)native;
    if(mm6_menu_draw(out,W,H,native))return 1;
    if(cyc_mod_peek(0x3a0)!=4 || (cyc_mod_peek(0x3b7)&2))return 0;
    if(mm6_coop_independent() && cyc_render_background(s_bg) && panels(out,W,H))return 1;
    int replace=s_enabled && s_frame.valid && s_frame.body.active==1;
    int adapter=s_enabled && s_frame.valid && s_frame.body.active==2;
    if((!replace && !adapter && !mm6_coop_has_frame() && !(s_no_flicker && mm6_sprites_ready()) && !s_wide) || W<256 || W>864 || H!=240 || !cyc_render_background(s_bg))return 0;
    mm6_world_audit(s_bg);
    int origin=(W-256)/2;
    memset(s_opaque,0,sizeof s_opaque);memset(s_claimed,0,(size_t)W*H);
    s_adapter_drawn=0;
    for(int i=0;i<W*H;i++)out[i]=0xff000000u;
    const uint8_t *opaque=cyc_frame_bg_opaque();
    for(int y=0;y<H;y++) {
        memcpy(out+y*W+origin,s_bg+y*256,256*sizeof *out);
        memcpy(s_opaque+y*W+origin,opaque+y*256,256);
    }
    if(s_wide)mm6_world_background(out,W,s_bg,s_opaque,&origin);
    const uint8_t *oam=cyc_render_oam();uint8_t counts[240]={0};memset(s_allowed,0,sizeof s_allowed);
    int first=64;
    for(int slot=0;slot<64;slot++) {
        int top=oam[slot*4]+1,height=cyc_render_line_sprite16(top<240?top:239)?16:8;
        for(int y=top;y<top+height && y<240;y++)s_allowed[slot][y]=counts[y]++<8;
        if(s_frame.owners[slot]==0 && slot<first)first=slot;
    }
    if((s_no_flicker || s_wide) && mm6_sprites_ready()){
        uint8_t hud_lines[240]={0};int hud_parts=0;
        for(int slot=0;slot<64;slot++)if(s_frame.owners[slot]==255){
            if(!mm6_hud_native_part(oam+slot*4))sprite(out,W,origin,slot,oam);
            int top=oam[slot*4]+1,height=cyc_render_line_sprite16(top<240?top:239)?16:8;
            if(top<240){hud_parts++;for(int y=top;y<top+height && y<240;y++)hud_lines[y]++;}
        }
        mm6_sprites_draw(out,W,origin,&s_frame.body,replace,s_opaque,s_claimed,!s_no_flicker,hud_lines,hud_parts);
    }else for(int slot=0;slot<64;slot++) {
        if(replace && s_frame.owners[slot]==0){if(slot==first)body(out,W,origin,&s_frame.body);}
        else if(!mm6_hud_native_part(oam+slot*4))sprite(out,W,origin,slot,oam);
    }
    if(first==64 && mm6_proto_swapping())body(out,W,origin,&s_frame.body);
    mm6_coop_draw(out,W,origin,s_opaque,s_claimed);
    mm6_hud_draw(out,W,H,-1);
    cyc_render_set_native_origin(origin);return 1;
}
void mm6_view_power(void) {
    mm6_sprites_power();
    mm6_hud_power();
    memset(&s_build,0,sizeof s_build);memset(&s_frame,0,sizeof s_frame);
    memset(s_build.owners,255,sizeof s_build.owners);memset(s_frame.owners,255,sizeof s_frame.owners);
    s_build.owner=s_frame.owner=255;
    s_capture_policy=s_enabled || s_coop || s_no_flicker || s_wide;cyc_render_capture_background(s_capture_policy!=0);
}
static void refresh(void) {
    int active=s_enabled || s_coop || s_no_flicker || s_wide;mm6_sprites_active(active);
    static const char *const ids[]={"begin","frame","actor","part0","part1","part2","part3","dma","animation","animation-update","complete","complete-full","hud"};
    for(int i=0;i<sizeof ids/sizeof *ids;i++){char id[48];snprintf(id,sizeof id,"mm6.view.%s",ids[i]);nes_mod_set_function_hook_enabled(id,active);}
    cyc_render_set_compositor(active?render:NULL,NULL);cyc_render_set_stock_compositing(active);
    if(s_capture_policy!=active){cyc_render_capture_background(active);s_capture_policy=active;}
}
void mm6_view_enable(int enabled){s_enabled=enabled!=0;refresh();}
void mm6_view_coop(int enabled){s_coop=enabled!=0;refresh();}
void mm6_view_no_flicker(int enabled){s_no_flicker=enabled!=0;refresh();}
void mm6_view_wide(int enabled,const char *aspect){
    s_wide=enabled!=0;int mode=NES_VIDEO_FIT;
    mm6_wide_set(s_wide);
    if(aspect && !strcmp(aspect,"stock"))mode=NES_VIDEO_STOCK;
    else if(aspect && !strcmp(aspect,"16-9"))mode=NES_VIDEO_16_9;
    else if(aspect && !strcmp(aspect,"21-9"))mode=NES_VIDEO_21_9;
    else if(aspect && !strcmp(aspect,"32-9"))mode=NES_VIDEO_32_9;
    cyc_video_set_mode(s_wide?mode:NES_VIDEO_STOCK);
    nes_mod_set_local_only("MM6 horizontal terrain prototype (unqualified netplay)",s_wide);refresh();
}
static int get(uint8_t *out,int cap) {
    if(cap<sizeof s_build+sizeof s_frame)return -1;
    memcpy(s_build.magic,"M6V2",4);memcpy(s_frame.magic,"M6V2",4);
    memcpy(out,&s_build,sizeof s_build);memcpy(out+sizeof s_build,&s_frame,sizeof s_frame);return sizeof s_build+sizeof s_frame;
}
static int valid(const uint8_t *in,int len) {
    if(!len)return !(s_enabled || s_coop || s_no_flicker || s_wide);if(!in || len!=sizeof s_build+sizeof s_frame)return 0;
    ViewFrame p[2];memcpy(p,in,sizeof p);
    for(int i=0;i<2;i++) {
        if(memcmp(p[i].magic,"M6V2",4) || p[i].valid>1 || p[i].panels>1 || p[i].native_scene>1 || (p[i].owner>22 && p[i].owner!=255))return 0;
        const Mm6ProtoBody *b=&p[i].body;
        if(b->pose>=CYC_PROTO_POSE_COUNT || b->active>2 || b->right>1 || b->flipped>1 || b->behind>1 || b->form>2 || (b->active==2 && !b->form))return 0;
        for(int j=0;j<64;j++)if(p[i].owners[j]>22 && p[i].owners[j]!=255)return 0;
        for(int s=0;s<2;s++)for(int y=0;y<30;y++)for(int x=0;x<33;x++)if(p[i].palettes[s][y][x]>3 || p[i].present[s][y][x]>1)return 0;
    }
    return 1;
}
static int set(const uint8_t *in,int len) {if(!valid(in,len))return 0;if(len){memcpy(&s_build,in,sizeof s_build);memcpy(&s_frame,in+sizeof s_build,sizeof s_frame);}else mm6_view_power();return 1;}
void mm6_view_register(void) {
    mm6_sprites_register();
    mm6_hud_register();
    nes_mod_register_function_entry_plugin("mm6.view.begin",0xe1dd,begin);
    nes_mod_register_function_entry_plugin("mm6.view.frame",0xd1d4,begin);
    nes_mod_register_function_entry_plugin("mm6.view.actor",0xe263,actor);
    nes_mod_register_function_entry_plugin("mm6.view.part0",0xe349,part);
    nes_mod_register_function_entry_plugin("mm6.view.part1",0xe387,part);
    nes_mod_register_function_entry_plugin("mm6.view.part2",0xe3c6,part);
    nes_mod_register_function_entry_plugin("mm6.view.part3",0xe40e,part);
    nes_mod_register_function_entry_plugin("mm6.view.dma",0xc02e,dma);
    nes_mod_register_function_entry_plugin("mm6.view.animation",0xe522,animation);
    nes_mod_register_function_entry_plugin("mm6.view.animation-update",0xe55d,animation);
    nes_mod_register_function_entry_plugin("mm6.view.complete",0xe27a,complete);
    nes_mod_register_function_entry_plugin("mm6.view.complete-full",0xe283,complete);
    nes_mod_register_function_entry_plugin("mm6.view.hud",0xd1f2,hud);
    nes_mod_register_savestate_hook("mm6.view",get,set);nes_mod_register_savestate_validator("mm6.view",valid);
}
