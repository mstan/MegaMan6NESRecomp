/* Native MM6 equipment, palette and CHR-RAM descriptors remain authoritative. */
#include "mm6_proto.h"
#include "cyc_host_extras.h"
#include "cyc_input.h"
#include "cyc_mod.h"
#include "cyc_ring.h"
#include "logical_input.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
typedef struct {char magic[4];uint8_t on,previous,pending,hurt;uint64_t changes;} Controls;
static Controls S;
static int s_on;
static int s_proto_on,s_proto_mode=1,s_proto_start=1;
static uint8_t *s_cart;
static size_t s_size;
static FILE *s_log;
static unsigned prg(unsigned bank,unsigned address) {
    size_t at=(size_t)bank*8192+(address&8191);
    return at<s_size?s_cart[at]:255;
}
static int graphics(unsigned id,int commit) {
    /* C95C: count in 16-byte units, destination in 16-byte units, a 9-bit
     * source offset in 16-byte units, and the physical source PRG bank. */
    unsigned descriptor=0x8000+4*id;
    unsigned count=prg(0x37,descriptor)*16,dest=prg(0x37,descriptor+1)*16;
    unsigned low=prg(0x37,descriptor+2),flags=prg(0x37,descriptor+3);
    dest|=(flags&128)<<5;
    size_t source=(size_t)((flags&126)>>1)*8192+(low|((flags&1)<<8))*16;
    if(!count || source+count>s_size || dest+count>8192)return 0;
    if(commit)for(unsigned i=0;i<count;i++)if(!cyc_mod_chr_poke((uint16_t)(dest+i),s_cart[source+i]))return 0;
    return 1;
}
static int palette(unsigned id,uint8_t out[32]) {
    /* C9EF: bit6 inserts the universal color, bit7 ends the packet. */
    unsigned pointer=prg(0x37,0x9000+id)|(prg(0x37,0x9100+id)<<8);
    if(pointer<0x8000 || pointer>=0xa000)return 0;
    unsigned at=prg(0x37,pointer);
    for(unsigned i=1;i<=65 && pointer+i<0xa000;i++) {
        unsigned value=prg(0x37,pointer+i);
        if(value&64){if(at>=32)return 0;out[at++]=15;}
        if(at>=32)return 0;
        out[at++]=(uint8_t)(value&63);
        if(value&128)return 1;
    }
    return 0;
}
static int safe(void) {
    /* Native Start is evaluated here only in the live player loop. The
     * frozen bit marks the retail menu; nonzero 9B is a Rush adapter form. */
    return s_cart && R(0x3a0) && R(0x3e5)<28 && !S.hurt && !mm6_proto_swapping() && !R(0x9a) && !R(0x9b) && !(R(0x3b7)&2);
}
int mm6_controls_hurt(void) {return S.hurt;}
int mm6_controls_restore_palette(void) {
    uint8_t colors[32];for(int i=0;i<32;i++)colors[i]=R(0x360+i);
    if(!palette(R(0xae),colors))return 0;
    for(int i=0;i<32;i++)P(0x360+i,colors[i]);P(0x46,R(0x46)+1);return 1;
}
static int hurt_begin(uint16_t address) {(void)address;S.hurt=1;S.pending=0;return 0;}
static int hurt_end(uint16_t address) {(void)address;S.hurt=0;S.pending=0;return 0;}
static int dispatch(uint16_t address) {
    (void)address;
    if(mm6_proto_dispatch()){S.pending=0;return 1;}
    unsigned fresh=S.pending;S.pending=0;
    if(!s_on || !safe() || (fresh!=CYC_GAME_L && fresh!=CYC_GAME_R))return 0;
    int current=R(0x699),next=current,step=fresh==CYC_GAME_L?-1:1;
    if(current>9)return 0;
    do{next=(next+step+10)%10;}while(next!=current && next && R(0x688+next)==0x80);
    if(next==current)return 0;
    unsigned base_graphics=prg(0x38,0x96c7),weapon_graphics=prg(0x3f,0xf708+next);
    unsigned base_palette=prg(0x38,0x96ca),weapon_palette=prg(0x38,0x962f+next);
    uint8_t colors[32];for(int i=0;i<32;i++)colors[i]=R(0x360+i);
    if(!graphics(base_graphics,0) || (next && !graphics(weapon_graphics,0)) ||
        !palette(base_palette,colors) || (next && !palette(weapon_palette,colors)))return 0;
    if(!graphics(base_graphics,1) || (next && !graphics(weapon_graphics,1)))return 0;
    /* Native 9617/9624: cancel the personal firing task and its actors. */
    P(0x699,next);P(0xa5,255);P(0x9e,0);P(0x22,0);
    for(int slot=1;slot<=7;slot++)P(0x3a0+slot,0);
    for(int i=0;i<32;i++)P(0x360+i,colors[i]);
    P(0xae,next?weapon_palette:base_palette);P(0x46,R(0x46)+1);
    S.changes++;return 0;
}
void mm6_controls_refresh(void) {
    int active=s_on || mm6_proto_enabled();
    nes_mod_set_function_hook_enabled("mm6.player.inventory",active);
    nes_mod_set_function_hook_enabled("mm6.controls.hurt",active);
    nes_mod_set_function_hook_enabled("mm6.controls.hurt-end",active);
    nes_mod_set_function_hook_enabled("mm6.controls.arrival",active);
}
static void enabled(int on) {s_on=on!=0;S.previous=S.pending=0;mm6_controls_refresh();}
static void input(void *ctx,uint8_t buttons[2]) {
    (void)ctx;(void)buttons;
    unsigned held=nes_input_game_buttons(1)&(CYC_GAME_L|CYC_GAME_R),fresh=held&~S.previous;
    if(!s_on || held==(CYC_GAME_L|CYC_GAME_R) || !safe())S.pending=0;
    else if(fresh)S.pending=(uint8_t)fresh;
    S.previous=(uint8_t)held;
}
static void power(void *ctx) {
    (void)ctx;memset(&S,0,sizeof S);mm6_proto_power();free(s_cart);s_cart=NULL;
    s_size=cyc_mod_copy_cart_payload(NULL,0);
    if(s_size==0x80000) {
        s_cart=malloc(s_size);
        if(s_cart && cyc_mod_copy_cart_payload(s_cart,s_size)!=s_size){free(s_cart);s_cart=NULL;}
    }
}
static void end(void *ctx) {
    (void)ctx;if(!safe())S.pending=0;mm6_proto_end();
    char fields[512];mm6_proto_json(fields,sizeof fields);
    if(s_log)fprintf(s_log,"{\"frame\":%u,\"weapon\":%u,\"form\":%u,\"terrain\":%u,\"pose_kind\":%u,\"hurt\":%u,\"hp\":%u,\"charge\":%u,\"held\":%u,\"changes\":%llu,%s}\n",
        cyc_ring_frame,R(0x699),R(0x9b),R(0xa7),R(0x94),S.hurt,R(0x3e5),R(0x9e),S.previous,(unsigned long long)S.changes,fields);
}
static bool option(void *ctx,const char *name,const char *value) {
    (void)ctx;
    if(!strcmp(name,"--mm6-controls-log")){if(s_log)fclose(s_log);s_log=fopen(value,"w");return s_log!=NULL;}
    if(!strcmp(name,"--protoman")) {
        if(strcmp(value,"on") && strcmp(value,"off"))return false;
        s_proto_on=!strcmp(value,"on");return mm6_proto_set(s_proto_on,s_proto_mode,s_proto_start);
    }
    if(!strcmp(name,"--proto-mode") || !strcmp(name,"--proto-start")) {
        if(strcmp(value,"megaman") && strcmp(value,"protoman"))return false;
        if(!strcmp(name,"--proto-mode"))s_proto_mode=!strcmp(value,"protoman");else s_proto_start=!strcmp(value,"protoman");
        return mm6_proto_set(s_proto_on,s_proto_mode,s_proto_start);
    }
    if(strcmp(name,"--weapon-cycle") || (strcmp(value,"on") && strcmp(value,"off")))return false;
    enabled(!strcmp(value,"on"));return true;
}
static int get(uint8_t *out,int capacity) {
    if(capacity<sizeof S)return -1;
    memcpy(S.magic,"M6W2",4);S.on=(uint8_t)s_on;memcpy(out,&S,sizeof S);return sizeof S;
}
static int valid(const uint8_t *in,int len) {
    if(!len)return !s_on;
    if(!in || len!=sizeof S)return 0;
    Controls p;memcpy(&p,in,sizeof p);
    return !memcmp(p.magic,"M6W2",4) && p.on==s_on && p.previous<=3 && p.pending<=3 && p.hurt<=1;
}
static int set(const uint8_t *in,int len) {if(!valid(in,len))return 0;if(len)memcpy(&S,in,sizeof S);else memset(&S,0,sizeof S);return 1;}
static void reset(void){enabled(0);s_proto_on=0;mm6_proto_set(0,s_proto_mode,s_proto_start);}
static void activate(void){enabled(1);}
NES_MOD_CONSTRUCTOR(register_mm6_controls) {
    mm6_proto_register();
    nes_mod_register_function_entry_plugin("mm6.player.inventory",0x924d,dispatch);
    nes_mod_register_function_entry_plugin("mm6.controls.hurt",0x84b0,hurt_begin);
    nes_mod_register_function_entry_plugin("mm6.controls.hurt-end",0x851a,hurt_end);
    nes_mod_register_function_entry_plugin("mm6.controls.arrival",0x802d,hurt_end);
    nes_mod_register_savestate_hook("mm6.controls",get,set);nes_mod_register_savestate_validator("mm6.controls",valid);
    nes_mod_register_reset_callback(reset);nes_mod_register_activation_plugin("mega-man-6-usa.weapons",activate);
}
const CycHostExtras *cyc_host_extras(void) {
    static const CycInputProfile profile={.id="megaman-weapons-1",.game_label={"Previous weapon (L)","Next weapon (R)"},
        .game_pad={"leftshoulder","rightshoulder"},.shortcut_pad={"none","leftstick+rightstick",NULL,NULL,NULL,NULL,NULL}};
    static const CycHostOption options[]={{"--weapon-cycle",true,"on|off: cycle acquired weapons and Beat in normal form"},
        {"--mm6-controls-log",true,"FILE: native equipment state for QA"},
        {"--protoman",true,"on|off: development solo character and swapping"},
        {"--proto-mode",true,"protoman|megaman: character traits or reskin"},
        {"--proto-start",true,"protoman|megaman: starting character"}};
    static const CycHostExtras extras={.power_on=power,.input=input,.frame_end=end,.option=option,
        .options=options,.option_count=5,.input_profile=&profile};
    return &extras;
}
