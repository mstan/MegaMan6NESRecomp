/* Native MM6 equipment, palette and CHR-RAM descriptors remain authoritative. */
#include "mm6_proto.h"
#include "mm6_world.h"
#include "mm6_wide.h"
#include "mm6_coop.h"
#include "mm6_charge.h"
#include "cyc_host_extras.h"
#include "cyc_tcp.h"
#ifdef CYC_WITH_RECOMP_UI
#include "recomp_runtime_ui.h"
#endif
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
static Controls s_players[2];
#define S (s_players[mm6_coop_seat()])
static int s_on;
static int s_proto_on,s_proto_mode=1,s_proto_start=1;
static int s_coop,s_roster;
static uint8_t *s_cart;
static size_t s_size;
static FILE *s_log;
static int s_wide;
static char s_aspect[16]="fit";
static unsigned prg(unsigned bank,unsigned address) {
    size_t at=(size_t)bank*8192+(address&8191);
    return at<s_size?s_cart[at]:255;
}
unsigned mm6_controls_prg(unsigned bank,unsigned address){return prg(bank,address);}
static int graphics(unsigned id,int commit,uint8_t *decoded) {
    /* C95C: count in 16-byte units, destination in 16-byte units, a 9-bit
     * source offset in 16-byte units, and the physical source PRG bank. */
    unsigned descriptor=0x8000+4*id;
    unsigned count=prg(0x37,descriptor)*16,dest=prg(0x37,descriptor+1)*16;
    unsigned low=prg(0x37,descriptor+2),flags=prg(0x37,descriptor+3);
    dest|=(flags&128)<<5;
    size_t source=(size_t)((flags&126)>>1)*8192+(low|((flags&1)<<8))*16;
    if(!count || source+count>s_size || dest+count>8192)return 0;
    if(decoded){
        if(dest<0x1000 || dest+count>0x1800)return 0;
        memcpy(decoded+dest-0x1000,s_cart+source,count);
    }
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
int mm6_controls_restore_graphics(void) {
    /* Retail 95CC..95F5 reloads this same base/form and weapon packet.
     * The two native personal pools share one physical CHR-RAM window. */
    unsigned form=R(0x9B),weapon=R(0x699);
    if(!s_cart || form>2 || weapon>9)return 0;
    unsigned base=prg(0x38,0x96C7+form),extra=prg(0x3F,0xF708+weapon);
    if(!graphics(base,0,NULL) || (weapon && !graphics(extra,0,NULL)))return 0;
    return graphics(base,1,NULL) && (!weapon || graphics(extra,1,NULL));
}
int mm6_controls_read_graphics(uint8_t out[2048]) {
    unsigned form=R(0x9B),weapon=R(0x699);
    if(!s_cart || !out || form>2 || weapon>9)return 0;
    unsigned base=prg(0x38,0x96C7+form),extra=prg(0x3F,0xF708+weapon);
    return graphics(base,0,out) && (!weapon || graphics(extra,0,out));
}
int mm6_controls_palette_colors(int seat,int id,uint8_t out[8]){
    uint8_t colors[32];for(int i=0;i<32;i++)colors[i]=R(0x360+i);
    for(int i=0;i<8;i++)colors[16+i]=(uint8_t)mm6_coop_read(seat,0x370+i);
    if(!s_cart || id<0 || id>255 || !palette((unsigned)id,colors))return 0;
    memcpy(out,colors+16,8);return 1;
}
static int safe_for(int seat) {
    /* Native Start is evaluated here only in the live player loop. The
     * frozen bit marks the retail menu; nonzero 9B is a Rush adapter form. */
    if(mm6_coop_enabled() && (!mm6_coop_ready() || (R(0x3B7)&2)))return 0;
    return s_cart && mm6_coop_read(seat,0x3A0) && mm6_coop_read(seat,0x3E5)<28 && !s_players[seat].hurt &&
        !mm6_proto_swapping() && !mm6_coop_read(seat,0x9A) && !mm6_coop_read(seat,0x9B) && !(mm6_coop_read(seat,0x3B7)&2);
}
static int safe(void){return safe_for(mm6_coop_seat());}
int mm6_controls_hurt(void) {return S.hurt;}
int mm6_controls_restore_palette(void) {
    uint8_t colors[32];for(int i=0;i<32;i++)colors[i]=R(0x360+i);
    if(!palette(R(0xae),colors))return 0;
    for(int i=0;i<32;i++)P(0x360+i,colors[i]);P(0x46,R(0x46)+1);return 1;
}
static int hurt_begin(uint16_t address) {(void)address;S.hurt=1;S.pending=0;return 0;}
static int hurt_end(uint16_t address) {S.hurt=0;S.pending=0;if(address==0x802D)mm6_coop_arrival();return 0;}
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
    if(!graphics(base_graphics,0,NULL) || (next && !graphics(weapon_graphics,0,NULL)) ||
        !palette(base_palette,colors) || (next && !palette(weapon_palette,colors)))return 0;
    if(!graphics(base_graphics,1,NULL) || (next && !graphics(weapon_graphics,1,NULL)))return 0;
    /* Native 9617/9624: cancel the personal firing task and its actors. */
    P(0x699,next);P(0xa5,255);P(0x9e,0);P(0x22,0);
    for(int slot=1;slot<=7;slot++)P(0x3a0+slot,0);
    for(int i=0;i<32;i++)P(0x360+i,colors[i]);
    P(0xae,next?weapon_palette:base_palette);P(0x46,R(0x46)+1);
    S.changes++;return 0;
}
void mm6_controls_refresh(void) {
    int active=s_on || mm6_proto_enabled() || mm6_coop_enabled();
    nes_mod_set_function_hook_enabled("mm6.player.inventory",active);
    nes_mod_set_function_hook_enabled("mm6.controls.hurt",active);
    nes_mod_set_function_hook_enabled("mm6.controls.hurt-end",active);
    nes_mod_set_function_hook_enabled("mm6.controls.arrival",active);
}
static void enabled(int on) {s_on=on!=0;for(int i=0;i<2;i++)s_players[i].previous=s_players[i].pending=0;mm6_controls_refresh();}
static void input(void *ctx,uint8_t buttons[2]) {
    (void)ctx;
    for(int i=0;i<2;i++){
        Controls *p=&s_players[i];
        unsigned held=i && !mm6_coop_enabled()?0:nes_input_game_buttons(i+1)&(CYC_GAME_L|CYC_GAME_R),fresh=held&~p->previous;
        if(!s_on || held==(CYC_GAME_L|CYC_GAME_R) || !safe_for(i))p->pending=0;
        else if(fresh)p->pending=(uint8_t)fresh;
        p->previous=(uint8_t)held;
    }
    mm6_coop_input(buttons);
}
static void power(void *ctx) {
    (void)ctx;memset(s_players,0,sizeof s_players);mm6_coop_power();mm6_proto_power();mm6_wide_power();free(s_cart);s_cart=NULL;
    s_size=cyc_mod_copy_cart_payload(NULL,0);
    if(s_size==0x80000) {
        s_cart=malloc(s_size);
        if(s_cart && cyc_mod_copy_cart_payload(s_cart,s_size)!=s_size){free(s_cart);s_cart=NULL;}
    }
}
static void end(void *ctx) {
    (void)ctx;if(!safe())S.pending=0;mm6_proto_end();mm6_coop_end();mm6_charge_end();
    char fields[512],coop[512],wide[256];mm6_proto_json(fields,sizeof fields);mm6_coop_json(coop,sizeof coop);mm6_wide_json(wide,sizeof wide);
    if(s_log)fprintf(s_log,"{\"frame\":%u,\"weapon\":%u,\"form\":%u,\"terrain\":%u,\"pose_kind\":%u,\"hurt\":%u,\"hp\":%u,\"charge\":%u,\"held\":%u,\"changes\":%llu,%s,%s,%s}\n",
        cyc_ring_frame,R(0x699),R(0x9b),R(0xa7),R(0x94),S.hurt,R(0x3e5),R(0x9e),S.previous,(unsigned long long)S.changes,fields,coop,wide);
}
static bool option(void *ctx,const char *name,const char *value) {
    (void)ctx;
    if(!strcmp(name,"--mm6-coop-poke"))return mm6_coop_test_poke(value)!=0;
    if(!strcmp(name,"--coop-view")){
        int mode=!strcmp(value,"shared")?0:!strcmp(value,"split")?1:!strcmp(value,"player1")?2:!strcmp(value,"player2")?3:-1;
        if(mode<0)return false;mm6_coop_view_set(mode);return true;
    }
    if(!strcmp(name,"--coop") || !strcmp(name,"--coop-roster")) {
        if(!strcmp(name,"--coop")) {
            if(strcmp(value,"on") && strcmp(value,"off"))return false;
            s_coop=!strcmp(value,"on");
        }else{
            if(strcmp(value,"megaman-protoman") && strcmp(value,"protoman-megaman"))return false;
            s_roster=!strcmp(value,"protoman-megaman");
        }
        if(!mm6_proto_set(s_proto_on || s_coop,s_proto_mode,s_proto_start))return false;
        mm6_coop_set(s_coop,s_roster);mm6_controls_refresh();return true;
    }
    if(!strcmp(name,"--mm6-controls-log")){if(s_log)fclose(s_log);s_log=fopen(value,"w");return s_log!=NULL;}
    if(!strcmp(name,"--world-audit"))return mm6_world_audit_file(value)!=0;
    if(!strcmp(name,"--wide")){
        if(strcmp(value,"on") && strcmp(value,"off"))return false;
        s_wide=!strcmp(value,"on");mm6_view_wide(s_wide,s_aspect);return true;
    }
    if(!strcmp(name,"--aspect")){
        if(strcmp(value,"fit") && strcmp(value,"stock") && strcmp(value,"16-9") && strcmp(value,"21-9") && strcmp(value,"32-9"))return false;
        snprintf(s_aspect,sizeof s_aspect,"%s",value);mm6_view_wide(s_wide,s_aspect);return true;
    }
    if(!strcmp(name,"--no-flicker")){
        if(strcmp(value,"on") && strcmp(value,"off"))return false;
        mm6_view_no_flicker(!strcmp(value,"on"));return true;
    }
    if(!strcmp(name,"--protoman")) {
        if(strcmp(value,"on") && strcmp(value,"off"))return false;
        s_proto_on=!strcmp(value,"on");return mm6_proto_set(s_proto_on || s_coop,s_proto_mode,s_proto_start);
    }
    if(!strcmp(name,"--proto-mode") || !strcmp(name,"--proto-start")) {
        if(strcmp(value,"megaman") && strcmp(value,"protoman"))return false;
        if(!strcmp(name,"--proto-mode"))s_proto_mode=!strcmp(value,"protoman");else s_proto_start=!strcmp(value,"protoman");
        return mm6_proto_set(s_proto_on || s_coop,s_proto_mode,s_proto_start);
    }
    if(strcmp(name,"--weapon-cycle") || (strcmp(value,"on") && strcmp(value,"off")))return false;
    enabled(!strcmp(value,"on"));return true;
}
static int get(uint8_t *out,int capacity) {
    if(capacity<sizeof s_players)return -1;
    for(int i=0;i<2;i++){memcpy(s_players[i].magic,"M6W3",4);s_players[i].on=(uint8_t)s_on;}
    memcpy(out,s_players,sizeof s_players);return sizeof s_players;
}
static int valid(const uint8_t *in,int len) {
    if(!len)return !s_on;
    if(!in || len!=sizeof s_players)return 0;
    Controls p[2];memcpy(p,in,sizeof p);
    for(int i=0;i<2;i++)if(memcmp(p[i].magic,"M6W3",4) || p[i].on!=s_on || p[i].previous>3 || p[i].pending>3 || p[i].hurt>1)return 0;
    return 1;
}
static int set(const uint8_t *in,int len) {if(!valid(in,len))return 0;if(len)memcpy(s_players,in,sizeof s_players);else memset(s_players,0,sizeof s_players);return 1;}
static void reset(void){enabled(0);s_coop=0;mm6_coop_set(0,s_roster);s_proto_on=0;mm6_proto_set(0,s_proto_mode,s_proto_start);mm6_view_no_flicker(0);s_wide=0;mm6_view_wide(0,s_aspect);}
static void activate(void){enabled(1);}
static void no_flicker(void){mm6_view_no_flicker(1);}
NES_MOD_CONSTRUCTOR(register_mm6_controls) {
    mm6_proto_register();
    mm6_coop_register();
    mm6_wide_register();
    nes_mod_register_function_entry_plugin("mm6.player.inventory",0x924d,dispatch);
    nes_mod_register_function_entry_plugin("mm6.controls.hurt",0x84b0,hurt_begin);
    nes_mod_register_function_entry_plugin("mm6.controls.hurt-end",0x851a,hurt_end);
    nes_mod_register_function_entry_plugin("mm6.controls.arrival",0x802d,hurt_end);
    nes_mod_register_savestate_hook("mm6.controls",get,set);nes_mod_register_savestate_validator("mm6.controls",valid);
    nes_mod_register_reset_callback(reset);nes_mod_register_activation_plugin("mega-man-6-usa.weapons",activate);
    nes_mod_register_activation_plugin("mega-man-6-usa.noflicker",no_flicker);
}
static void tcp_state(int id,const char *line){
    char view[16];if(cyc_tcp_str(line,"view",view,sizeof view)){
        int mode=!strcmp(view,"shared")?0:!strcmp(view,"split")?1:!strcmp(view,"player1")?2:!strcmp(view,"player2")?3:-1;
        if(mode<0 || !mm6_coop_enabled()){cyc_tcp_err(id,"view needs active co-op and shared|split|player1|player2");return;}
        mm6_coop_view_set(mode);
    }
    char fields[1024];int n=snprintf(fields,sizeof fields,"\"view\":%d,",mm6_coop_view_mode());
    mm6_coop_json(fields+n,sizeof fields-n);cyc_tcp_ok(id,fields);
}
static void tcp(void *ctx){(void)ctx;cyc_tcp_register("mm6_state","MM6 co-op state and local view selection",tcp_state);}
#ifdef CYC_WITH_RECOMP_UI
static int menu_get(void *ctx,const RecompRuntimeUiItem *item,int *out){
    (void)ctx;if(strcmp(item->key,"mm6.coop_view"))return 0;*out=mm6_coop_view_mode();return 1;
}
static int menu_set(void *ctx,const RecompRuntimeUiItem *item,int value){
    (void)ctx;if(strcmp(item->key,"mm6.coop_view") || !mm6_coop_enabled() || value<0 || value>3)return 0;
    mm6_coop_view_set(value);return mm6_coop_view_mode()==value;
}
static int menu_enabled(void *ctx,const RecompRuntimeUiItem *item){
    (void)ctx;return !strcmp(item->key,"mm6.coop_view") && mm6_coop_enabled();
}
static const char *const view_choices[]={"Shared screen","Split screen","Player one","Player two"};
static const RecompRuntimeUiItem menu_items[]={
    {.key="mm6.coop_view",.section="Display",.label="Two-player view",
     .description="Choose a shared screen, split screen, or either player.",
     .type=RECOMP_RUNTIME_UI_CHOICE,.minimum=0,.maximum=3,.step=1,.choices=view_choices,.choice_count=4}
};
static const RecompRuntimeUiCallbacks menu_callbacks={.get_value=menu_get,.set_value=menu_set,.is_enabled=menu_enabled};
#endif
const CycHostExtras *cyc_host_extras(void) {
    static const CycInputProfile profile={.id="megaman-weapons-1",.game_label={"Previous weapon (L)","Next weapon (R)"},
        .game_pad={"leftshoulder","rightshoulder"},.shortcut_pad={"none","leftstick+rightstick",NULL,NULL,NULL,NULL,NULL}};
    static const CycHostOption options[]={{"--weapon-cycle",true,"on|off: cycle acquired weapons and Beat in normal form"},
        {"--mm6-controls-log",true,"FILE: native equipment state for QA"},
        {"--protoman",true,"on|off: development solo character and swapping"},
        {"--proto-mode",true,"protoman|megaman: character traits or reskin"},
        {"--proto-start",true,"protoman|megaman: starting character"},
        {"--coop",true,"on|off: development native coroutine controls prototype"},
        {"--coop-roster",true,"megaman-protoman|protoman-megaman: development roster"},
        {"--no-flicker",true,"on|off: complete native sprite presentation"},
        {"--world-audit",true,"FILE: native horizontal terrain conformance for QA"},
        {"--wide",true,"on|off: development authored horizontal terrain"},
        {"--aspect",true,"fit|stock|16-9|21-9|32-9: development terrain aspect"},
        {"--coop-view",true,"shared|split|player1|player2: local co-op presentation"},
        {"--mm6-coop-poke",true,"FRAME:SEAT:HEX_ADDRESS:HEX_VALUE: personal QA prerequisite"}};
    static const CycHostExtras extras={.power_on=power,.input=input,.frame_end=end,.option=option,.tcp_setup=tcp,
        .options=options,.option_count=sizeof options/sizeof *options,.input_profile=&profile,
#ifdef CYC_WITH_RECOMP_UI
        .menu_items=menu_items,.menu_item_count=sizeof menu_items/sizeof *menu_items,.menu_callbacks=&menu_callbacks,
#endif
    };
    return &extras;
}
