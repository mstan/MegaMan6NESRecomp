/* Keep MM6's coroutine, native charge, slide and adapter mechanisms intact. */
#include "mm6_proto.h"
#include "mm6_coop.h"
#include "mm6_proto_sheet.h"
#include "mm6_net.h"
#ifdef NESRECOMP_NET
#include "nes_netplay_identity.h"
#endif
#include "cyc_mod.h"
#include "cyc_ring.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
typedef struct {uint16_t x;uint8_t y,right;} Reflection;
typedef struct {
    char magic[4];
    uint8_t enabled,mode,selected,teleport,saved_inv,saved_phase,saved_timer,native_animation;
    uint8_t reflected[3],pending[3],muzzle[3];
    Reflection reflection[3];
    uint64_t switches,limited,blocks,reflections,damage_events;
    uint8_t last_enemy,last_damage;
} ProtoState;
static ProtoState s_players[2];
#define S (s_players[mm6_coop_seat()])
static CycProtoAtlas s_atlas;
static int s_loaded,s_on,s_mode=1,s_start=1;
int mm6_proto_source_hash(uint8_t out[32]) {
#ifdef NESRECOMP_NET
    /* Native adapter armor/wings are part of the separately hashed cartridge.
     * Include the source art and exact cropped-head policy in this identity. */
    static const char adapter[]="MM6 adapter head v1: IDLE0 rows8..19; mask dx[-8,8] dy[-13,-1]; native armor,wings,punches,exhaust";
    uint8_t sources[96];nes_sha256(MM6_PROTO_PNG,sizeof MM6_PROTO_PNG,sources);
    nes_sha256(MM6_PROTO_JUMP_PNG,sizeof MM6_PROTO_JUMP_PNG,sources+32);
    nes_sha256(adapter,sizeof adapter-1,sources+64);nes_sha256(sources,sizeof sources,out);return 1;
#else
    (void)out;return 0;
#endif
}
int mm6_proto_enabled(void) {return s_on;}
int mm6_proto_selected(void) {int character=mm6_coop_character();return s_on && (character>=0?character:S.selected);}
int mm6_proto_swapping(void) {return s_on && !mm6_coop_enabled() && S.teleport;}
const CycProtoAtlas *mm6_proto_atlas(void) {return s_loaded?&s_atlas:NULL;}
void mm6_proto_animation(int animation) {S.native_animation=(uint8_t)animation;}
void mm6_proto_clone_animation(int from,int to) {
    if(from>=0 && from<2 && to>=0 && to<2)s_players[to].native_animation=s_players[from].native_animation;
}
static int safe(void) {
    return R(0x3a0)==4 && R(0x3e5)<28 && !mm6_controls_hurt() &&
        !(R(0x3b7)&2) && R(0x94)!=3 && !R(0x47) && !R(0x60);
}
int mm6_proto_dispatch(void) {
    if(!s_on || mm6_coop_enabled() || cyc_mod_isolated())return 0;
    if(!S.teleport && (R(0x42)&32) && safe()) {
        S.teleport=1;S.switches++;S.saved_inv=R(0xa2);
        S.saved_phase=R(0x510);S.saved_timer=R(0x555);
        P(0x555,255);
        P(0x9e,0);P(0xa5,255);P(0x22,0);P(0xdc,0x24);
        mm6_controls_restore_palette();
    }
    if(S.teleport) {P(0xa2,2);return 1;}
    return 0;
}
static int actor(uint16_t address) {
    (void)address;
    if(!S.teleport || !s_on || cyc_mod_isolated())return 0;
    CycModRegs r;cyc_mod_regs(&r);
    /* Skip this invocation of actor0's saved continuation. The native loop
     * still restores its stack/banks and advances all other actors once. */
    return r.x==0 && R(0x8f)==0 && R(0x08)==R(0x42a) && R(0x09)==R(0x413);
}
static int limit(uint16_t address) {
    (void)address;CycModRegs r;cyc_mod_regs(&r);
    if(!mm6_proto_selected() || !s_mode || cyc_mod_isolated() || r.y!=9)return 0;
    int count=0;
    for(int slot=1;slot<=3;slot++)count+=R(0x3a0+slot)==9 && !S.reflected[slot-1];
    if(count<2)return 0;
    S.limited++;r.p|=1;cyc_mod_set_regs(&r);return 1;
}
static int created(uint16_t address) {
    (void)address;if(cyc_mod_isolated())return 0;
    CycModRegs r;cyc_mod_regs(&r);
    if(r.x>=1 && r.x<=3) {
        S.reflected[r.x-1]=S.pending[r.x-1]=0;
        S.muzzle[r.x-1]=(uint8_t)(r.y>=9 && r.y<=11);
    }
    return 0;
}
static int muzzle(uint16_t address) {
    (void)address;if(cyc_mod_isolated())return 0;
    CycModRegs r;cyc_mod_regs(&r);int slot=r.x;
    if(slot<1 || slot>3)return 0;
    if(S.pending[slot-1]) {
        Reflection *p=&S.reflection[slot-1];
        P(0x486+slot,p->x);P(0x46f+slot,p->x>>8);P(0x4cb+slot,p->y);P(0x4b4+slot,0);
        P(0x3ce + slot,p->right?1:2);P(0x3b7+slot,p->right?64:0);
        P(0x5c8+slot,p->right?4:252);P(0x5df+slot,0);
        S.pending[slot-1]=S.muzzle[slot-1]=0;return 0;
    }
    if(S.muzzle[slot-1] && mm6_proto_selected() && !R(0x9b)) {
        int kind=R(0x94),offset=(kind==4 || kind==5)?-4:3;
        int y=R(0x4cb+slot)+offset;
        if(y>=0 && y<240)P(0x4cb+slot,y);
    }
    S.muzzle[slot-1]=0;return 0;
}
static int damage(uint16_t address) {
    (void)address;
    if(!mm6_proto_selected() || !s_mode || S.teleport || cyc_mod_isolated())return 0;
    CycModRegs r;cyc_mod_regs(&r);int amount=R(4);
    S.last_damage=(uint8_t)amount;S.last_enemy=R(0x3a0+r.y);S.damage_events++;
    r.a=(uint8_t)(r.a>amount?r.a-amount:0);cyc_mod_set_regs(&r);return 0;
}
static int shield_up(void) {
    return mm6_proto_selected() && s_mode && !S.teleport && !mm6_controls_hurt() &&
        R(0x94)==7 && !R(0x98) && !R(0x9d) && !(R(0x3b7)&2);
}
static int shield(uint16_t address) {
    (void)address;if(!shield_up() || cyc_mod_isolated())return 0;
    CycModRegs r;cyc_mod_regs(&r);int enemy=r.y;
    /* 14/1A are the native small hostile pellets. DA/E4 are friendly
     * ricochets created in slot6 by the game and must not be shield targets. */
    if(enemy<8 || enemy>=22 || (R(0x3a0+enemy)!=0x14 && R(0x3a0+enemy)!=0x1a))return 0;
    int x=(R(0x46f)<<8)|R(0x486),ex=(R(0x46f+enemy)<<8)|R(0x486+enemy);
    int dx=ex-x,dy=(int)R(0x4cb+enemy)-R(0x4cb),right=(R(0x3b7)&64)!=0;
    if(R(0x4b4+enemy)!=R(0x4b4) || dy<-16 || dy>12 || (right?(dx<0 || dx>20):(dx>0 || dx<-20)))return 0;
    int slot;for(slot=1;slot<=3;slot++)if(!R(0x3a0+slot))break;
    if(slot<=3 && cyc_mod_isolate_begin()) {
        CycModRegs ctor=r;
        /* CBC0 is the ordinary returning two-bank setter. CB91 creates
         * a native nested bank frame and requires its matching CBE9 tail. */
        ctor.y=0x3a;int ok=cyc_mod_call(0xcbc0,&ctor);
        ctor.x=(uint8_t)slot;ctor.y=9;if(ok)ok=cyc_mod_call(0x81f0,&ctor);
        uint8_t fields[30];int n=0;
        for(int base=0x3a0;base<=0x63b;base+=23)fields[n++]=R(base+slot);
        cyc_mod_isolate_end();
        if(ok) {
            n=0;for(int base=0x3a0;base<=0x63b;base+=23)P(base+slot,fields[n++]);
            P(0x3fc+slot,255);
            S.reflected[slot-1]=S.pending[slot-1]=1;S.muzzle[slot-1]=0;
            S.reflection[slot-1].x=(uint16_t)ex;S.reflection[slot-1].y=R(0x4cb+enemy);
            S.reflection[slot-1].right=(uint8_t)right;S.reflections++;
        }
    }
    P(0x3a0+enemy,0);P(0x3fc+enemy,255);P(0x3e5+enemy,255);S.blocks++;P(0xdc,0x27);return 1;
}
void mm6_proto_body(Mm6ProtoBody *b) {
    memset(b,0,sizeof *b);
    b->active=(uint8_t)((mm6_proto_selected() || S.teleport) && R(0x3a0)==4 && R(0x3e5)<28);
    b->x=(uint8_t)(R(0x486)-R(0xf7));b->y=(uint8_t)(R(0x4cb)-R(0xf9));
    b->right=(R(0x3b7)&64)!=0;b->flipped=(R(0x3b7)&128)!=0;b->behind=(R(0x3b7)&32)!=0;
    b->weapon=R(0x699);b->charge=R(0x9e);b->form=R(0x9b)<=2?R(0x9b):0;
    for(int i=0;i<8;i++)b->palette[i]=R(0x370+i);
    int kind=R(0x94),phase=(R(0x510)/2)&3,firing=R(0x98)!=0 || R(0x9d)!=0;
    static const int run[]={0,1,2,1};
    if(S.teleport) {b->pose=CYC_PROTO_TELEPORT;b->y-=(S.teleport<=16?S.teleport-1:32-S.teleport)*16;}
    else if(mm6_controls_hurt())b->pose=CYC_PROTO_HURT;
    else if(kind==3)b->pose=CYC_PROTO_SLIDE;
    else if(kind==4 || kind==5) {
        b->pose=firing?CYC_PROTO_LADDER_FIRE0:kind==5?CYC_PROTO_LADDER_TOP0:CYC_PROTO_LADDER0;
        if(!firing)b->right=(uint8_t)(phase&1);
    }
    else if(kind==7)b->pose=firing?CYC_PROTO_JUMP_FIRE:shield_up()?CYC_PROTO_JUMP_GUARD:CYC_PROTO_JUMP_OPEN;
    else if(kind==2)b->pose=(uint8_t)((firing?CYC_PROTO_FIRE_RUN0:CYC_PROTO_RUN0)+run[phase]);
    else if(kind==1)b->pose=(uint8_t)(CYC_PROTO_STEP0+(phase&1));
    else b->pose=(uint8_t)(firing?CYC_PROTO_FIRE_IDLE0:CYC_PROTO_IDLE0+((cyc_ring_frame>>3)&3));
    /* Adapter armor and animation remain native. The compositor replaces
     * only the helmet/scarf; unknown base entry/effect poses stay native. */
    if(!S.teleport) {
        int a=S.native_animation;
        int known=a<=4 || a==7 || a==8 || a==10 || a==12 || a==14 || a==15 || a==24;
        if(b->form)b->active=(uint8_t)(b->active?2:0);
        else if(!known)b->active=0;
    }
}
void mm6_proto_power(void) {memset(s_players,0,sizeof s_players);for(int i=0;i<2;i++){s_players[i].selected=(uint8_t)s_start;s_players[i].native_animation=255;}mm6_view_power();}
void mm6_proto_end(void) {
    for(int seat=0;seat<(mm6_coop_enabled()?2:1);seat++)for(int slot=1;slot<=3;slot++)
        if(mm6_coop_read(seat,0x3A0+slot)!=9){s_players[seat].reflected[slot-1]=s_players[seat].pending[slot-1]=0;}
    if(mm6_coop_enabled())return;
    if(!s_on || !S.teleport)return;
    P(0xa2,2);P(0x555,255);
    if(S.teleport==16){S.selected^=1;P(0xdc,0x24);}
    if(++S.teleport>32){S.teleport=0;P(0xa2,S.saved_inv);P(0x510,S.saved_phase);P(0x555,S.saved_timer);}
}
bool mm6_proto_set(int enabled,int mode,int start) {
    if(enabled && !s_loaded) {
        char why[160];
        if(!cyc_protoman_atlas_load(&s_atlas,MM6_PROTO_PNG,sizeof MM6_PROTO_PNG,MM6_PROTO_JUMP_PNG,sizeof MM6_PROTO_JUMP_PNG,why,sizeof why)) {fprintf(stderr,"[Proto Man] %s\n",why);return false;}
        s_loaded=1;
    }
    s_start=start!=0;s_mode=mode!=0;
    if(enabled && (!s_on || !cyc_ring_frame))S.selected=(uint8_t)s_start;
    if(!enabled && S.teleport){P(0xa2,S.saved_inv);P(0x510,S.saved_phase);P(0x555,S.saved_timer);S.teleport=0;}
    s_on=enabled!=0;
    static const char *const ids[]={"actor","limit","created","muzzle","damage","shield"};
    for(int i=0;i<sizeof ids/sizeof *ids;i++){char id[48];snprintf(id,sizeof id,"mm6.proto.%s",ids[i]);nes_mod_set_function_hook_enabled(id,s_on);}
    nes_mod_set_local_only("MM6 Proto Man (unqualified netplay)",s_on);mm6_controls_refresh();mm6_view_enable(s_on);return true;
}
static int get(uint8_t *out,int cap) {if(cap<sizeof s_players)return -1;for(int i=0;i<2;i++){memcpy(s_players[i].magic,"M6P2",4);s_players[i].enabled=(uint8_t)s_on;s_players[i].mode=(uint8_t)s_mode;}memcpy(out,s_players,sizeof s_players);return sizeof s_players;}
static int valid(const uint8_t *in,int len) {
    if(!len)return !s_on;if(!in || len!=sizeof s_players)return 0;
    ProtoState p[2];memcpy(p,in,sizeof p);
    for(int seat=0;seat<2;seat++){
        if(memcmp(p[seat].magic,"M6P2",4) || p[seat].enabled!=s_on || p[seat].mode!=s_mode || p[seat].selected>1 || p[seat].teleport>32)return 0;
        for(int i=0;i<3;i++)if(p[seat].reflected[i]>1 || p[seat].pending[i]>1 || p[seat].muzzle[i]>1 || p[seat].reflection[i].right>1)return 0;
    }
    return 1;
}
static int set(const uint8_t *in,int len) {if(!valid(in,len))return 0;if(len)memcpy(s_players,in,sizeof s_players);else memset(s_players,0,sizeof s_players);return 1;}
int mm6_proto_json(char *out,int cap) {
    return snprintf(out,cap,"\"proto\":%d,\"swap\":%u,\"native_anim\":%u,\"switches\":%llu,\"limited\":%llu,\"blocks\":%llu,\"reflections\":%llu,\"damage_events\":%llu,\"last_enemy\":%u,\"last_damage\":%u",mm6_proto_selected(),S.teleport,S.native_animation,(unsigned long long)S.switches,(unsigned long long)S.limited,(unsigned long long)S.blocks,(unsigned long long)S.reflections,(unsigned long long)S.damage_events,S.last_enemy,S.last_damage);
}
void mm6_proto_register(void) {
    nes_mod_register_function_entry_plugin("mm6.proto.actor",0xe482,actor);
    nes_mod_register_function_entry_plugin("mm6.proto.limit",0xe510,limit);
    nes_mod_register_function_entry_plugin("mm6.proto.created",0x81f0,created);
    nes_mod_register_function_entry_plugin("mm6.proto.muzzle",0x8ca3,muzzle);
    nes_mod_register_function_entry_plugin("mm6.proto.damage",0x8f84,damage);
    nes_mod_register_function_entry_plugin("mm6.proto.shield",0x8f72,shield);
    nes_mod_register_savestate_hook("mm6.proto",get,set);nes_mod_register_savestate_validator("mm6.proto",valid);
    mm6_view_register();
}
