/* Extend ordinary native actors; the stage director remains on its native camera. */
#include "mm6_wide.h"
#include "mm6_world.h"
#include "mm6_proto.h"
#include "mm6_coop.h"
#include "mm6_net.h"
#include "cyc_mod.h"
#include "cyc_video.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
typedef struct {char magic[4];uint8_t enabled,seen[128];uint64_t spawns,full,failed;} Wide;
static Wide S;static int s_on;
static int in_views(int x,int radius){
    int left[2];if(!mm6_coop_independent() || !mm6_coop_view_rects(left))return 0;
    return (x+radius>=left[0] && x-radius<left[0]+256) || (x+radius>=left[1] && x-radius<left[1]+256);
}
int mm6_wide_active(void){
    int independent=mm6_coop_independent(),camera,left,lo,hi,width=independent?256:mm6_net_simulation_width(cyc_video_width());
    return (independent || (s_on && width>256)) && R(0x3A0)==4 && !(R(0x3B7)&2) && mm6_world_geometry(width,&camera,&left,&lo,&hi);
}
static int spawn(uint16_t address){
    (void)address;int independent=mm6_coop_independent(),camera,left,lo,hi,width=independent?256:mm6_net_simulation_width(cyc_video_width());
    if(!mm6_wide_active() || cyc_mod_isolated() || (independent && mm6_coop_scene_locked()) || !mm6_world_geometry(width,&camera,&left,&lo,&hi))return 0;
    for(int id=0;id<128;id++){
        Mm6WorldActor a;if(!mm6_world_actor(id,&a))break;
        if(a.kind>=128 || a.y>=248)continue;
        int visible=independent?in_views(a.x,24):(a.x>=left+4 && a.x<left+width);
        if(a.x<lo || a.x>=hi || !visible){S.seen[id]=0;continue;}
        int bit=mm6_controls_prg(0x3E,0xDFB1+(id&7));if(R(0x650+(id>>3))&bit)continue;
        int exists=0;for(int slot=8;slot<=21;slot++)if(R(0x3FC+slot)==id)exists=1;
        if(exists){S.seen[id]=1;continue;}
        if(S.seen[id] || (a.x>=camera+4 && a.x<camera+256))continue;
        if(!cyc_mod_isolate_begin()){S.failed++;break;}
        CycModRegs r;cyc_mod_regs(&r);r.y=0x3A;int ok=cyc_mod_call(0xCBC0,&r);
        r.y=(uint8_t)id;if(ok)ok=cyc_mod_call(0x81A7,&r);
        int slot=r.x,created=ok && slot>=8 && slot<=21 && R(0x3A0+slot)==a.kind && R(0x3FC+slot)==id;
        uint8_t fields[30];int n=0;if(created)for(int base=0x3A0;base<=0x63B;base+=23)fields[n++]=R(base+slot);
        cyc_mod_isolate_end();
        if(!ok){S.failed++;continue;}
        if(!created){S.full++;continue;}
        n=0;for(int base=0x3A0;base<=0x63B;base+=23)P(base+slot,fields[n++]);S.seen[id]=1;S.spawns++;
    }
    return 0;
}
static int authored(uint16_t address){
    (void)address;if(!mm6_wide_active() || cyc_mod_isolated())return 0;
    CycModRegs r;cyc_mod_regs(&r);Mm6WorldActor a;
    /* Native directional cursors may subsequently reach a killed margin
     * actor. Retain that authored identity until it leaves the whole view. */
    if(r.y>=128 || !mm6_world_actor(r.y,&a) || a.kind>=128)return 0;
    if(mm6_coop_independent() && !mm6_coop_scene_locked() && !in_views(a.x,24)){S.seen[r.y]=0;return 1;}
    return S.seen[r.y]!=0;
}
static int reset(uint16_t address){
    (void)address;if(!cyc_mod_isolated())memset(S.seen,0,sizeof S.seen);return 0;
}
static int cull(uint16_t address){
    (void)address;if(!mm6_wide_active() || cyc_mod_isolated())return 0;
    int independent=mm6_coop_independent();
    CycModRegs r;cyc_mod_regs(&r);int slot=r.x;if((slot<1 && !independent) || slot>21 || R(0x4B4+slot) || R(0x4CB+slot)>=248)return 0;
    int camera,left,lo,hi,width=independent?256:mm6_net_simulation_width(cyc_video_width()),x=(R(0x46F+slot)<<8)|R(0x486+slot);
    if(!mm6_world_geometry(width,&camera,&left,&lo,&hi) || x<lo || x>=hi ||
        (independent?!in_views(x,24):(x<left+4 || x>=left+width)))return 0;
    /* E485 has already performed the original coordinate calculations.
     * Change only its outside/inside result before its SEC/RTS tail. */
    r.p&=(uint8_t)~1;cyc_mod_set_regs(&r);return 1;
}
static int emitter(uint16_t address){
    (void)address;if(!mm6_wide_active() || cyc_mod_isolated())return 0;
    CycModRegs r;cyc_mod_regs(&r);int slot=r.x;if(slot<1 || slot>21)return 0;
    int caller=R(0x100|((r.s+1)&255))|(R(0x100|((r.s+2)&255))<<8);
    if(caller!=0xE265)return 0;
    int x=((R(0x46F+slot)<<8)|R(0x486+slot))-((R(0x57)<<8)|R(0x56));
    if(x>=4 && x<=255)return 0;
    /* Native E1E1 keeps updating animation, then skips only this ordinary
     * emitter call. A nonzero X prevents its full-OAM branch when OAM is empty;
     * the next iteration reloads X and the original caller restores its own X. */
    r.x=R(0x8C)?R(0x8C):1;cyc_mod_set_regs(&r);return 1;
}
void mm6_wide_power(void){memset(&S,0,sizeof S);}
void mm6_wide_views_changed(void){
    int active=s_on || mm6_coop_independent();
    nes_mod_set_function_hook_enabled("mm6.wide.spawn",active);nes_mod_set_function_hook_enabled("mm6.wide.cull",active);
    nes_mod_set_function_hook_enabled("mm6.wide.emitter",active);nes_mod_set_function_hook_enabled("mm6.wide.authored",active);
    nes_mod_set_function_hook_enabled("mm6.wide.reset",active);
}
void mm6_wide_set(int enabled){
    if(s_on!=(enabled!=0))memset(S.seen,0,sizeof S.seen);
    s_on=enabled!=0;mm6_wide_views_changed();
}
static int get(uint8_t *out,int cap){if(cap<sizeof S)return -1;memcpy(S.magic,"M6X2",4);S.enabled=(uint8_t)s_on;memcpy(out,&S,sizeof S);return sizeof S;}
static int valid(const uint8_t *in,int len){
    if(!len)return !(s_on || mm6_coop_independent());
    if(!in || len!=sizeof S || memcmp(in,"M6X2",4) || in[4]!=s_on)return 0;
    for(int i=0;i<128;i++)if(in[5+i]>1)return 0;return 1;
}
static int set(const uint8_t *in,int len){if(!valid(in,len))return 0;if(len)memcpy(&S,in,sizeof S);else mm6_wide_power();return 1;}
int mm6_wide_json(char *out,size_t cap){return snprintf(out,cap,"\"wide\":{\"on\":%d,\"spawns\":%llu,\"full\":%llu,\"failed\":%llu}",s_on,(unsigned long long)S.spawns,(unsigned long long)S.full,(unsigned long long)S.failed);}
void mm6_wide_register(void){
    nes_mod_register_function_entry_plugin("mm6.wide.spawn",0x8120,spawn);nes_mod_register_function_entry_plugin("mm6.wide.cull",0xE4A5,cull);
    nes_mod_register_function_entry_plugin("mm6.wide.emitter",0xE2A9,emitter);
    nes_mod_register_function_entry_plugin("mm6.wide.authored",0x81A7,authored);nes_mod_register_function_entry_plugin("mm6.wide.reset",0x8032,reset);
    nes_mod_register_savestate_hook("mm6.wide",get,set);nes_mod_register_savestate_validator("mm6.wide",valid);
}
