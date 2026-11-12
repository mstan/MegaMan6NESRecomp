/* Each personal coroutine runs on the real clock; shared actors run once. */
#include "mm6_coop.h"
#include "mm6_proto.h"
#include "mm6_charge.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
enum { PERSONAL_CAP=320,PART_CAP=512 };
typedef struct {uint8_t ram[PERSONAL_CAP];} Player;
typedef struct {int16_t x,y;uint8_t owner,attr,colors[4],pixels[64];} Part;
typedef struct {uint8_t valid,seat;uint16_t count,camera;Mm6ProtoBody body;Part parts[PART_CAP];} Picture;
typedef struct {
    char magic[4];uint8_t enabled,roster,initialized,eligible,owner,anchor,guest_round;
    uint8_t raw[2],previous[2],pending[2];
    Player player[2];uint64_t world,updates[2];
    uint8_t dead[2],retry,retry_ammo[2][10];
    Picture build,frame;
} Coop;
static Coop C;
static int s_on,s_roster,s_count,s_animating;
static uint16_t s_addr[PERSONAL_CAP],s_index[2048];
int mm6_coop_enabled(void){return s_on;}
int mm6_coop_ready(void){return s_on && C.initialized;}
int mm6_coop_seat(void){return s_on?C.owner:0;}
int mm6_coop_character(void){return s_on?(s_roster^C.owner):-1;}
int mm6_coop_character_for(int seat){return s_on && seat>=0 && seat<2?(s_roster^seat):0;}
static void addresses(void){
    if(s_count)return;
    /* Native Buster1..3, Beat4, hurt effect5 and friendly ricochets6/7.
     * Level actors8..21 and the director22 remain in the shared loop. */
    for(int base=0x3A0;base<=0x63B;base+=23)for(int slot=0;slot<8;slot++)s_addr[s_count++]=(uint16_t)(base+slot);
    for(int a=0x94;a<=0xAE;a++)if(a!=0xA9)s_addr[s_count++]=(uint16_t)a;
    s_addr[s_count++]=0x22;
    for(int a=0x688;a<=0x691;a++)s_addr[s_count++]=(uint16_t)a;
    s_addr[s_count++]=0x699;
    for(int a=0x370;a<=0x377;a++)s_addr[s_count++]=(uint16_t)a;
    /* Native inventory page/column/row are personal. E-tanks at6A1 and
     * stage progress remain shared; a peer's cursor must not select ours. */
    for(int a=0x6A2;a<=0x6A4;a++)s_addr[s_count++]=(uint16_t)a;
    s_addr[s_count++]=0x6AB;
    for(int i=0;i<s_count;i++)s_index[s_addr[i]]=(uint16_t)(i+1);
}
static void capture(Player *p){for(int i=0;i<s_count;i++)p->ram[i]=R(s_addr[i]);}
static void project(const Player *p){
    for(int i=0;i<s_count;i++)P(s_addr[i],p->ram[i]);
    if(!cyc_mod_isolated() && R(0x3A0)==4 && !(R(0x3B7)&2))mm6_controls_restore_graphics();
}
static int personal(const Player *p,int a){int i=s_index[a];return i?p->ram[i-1]:R(a);}
static void put(Player *p,int a,int v){int i=s_index[a];if(i)p->ram[i-1]=(uint8_t)v;}
int mm6_coop_read(int seat,int a){
    if(seat<0 || seat>1 || a<0 || a>=2048)return 0;
    return C.initialized && seat!=C.owner?personal(&C.player[seat],a):R(a);
}
void mm6_coop_colors(int seat,const uint8_t colors[8]){
    if(!C.initialized || seat<0 || seat>1)return;
    int changed=0;
    for(int i=0;i<8;i++){
        if(mm6_coop_read(seat,0x370+i)!=colors[i])changed=1;
        put(&C.player[seat],0x370+i,colors[i]);
        if(C.owner==seat)P(0x370+i,colors[i]);
    }
    if(changed && C.owner==seat)P(0x46,R(0x46)+1);
}
static void input_for(int seat){
    static const uint8_t horizontal[]={0,1,2,2};
    P(0x40,C.raw[seat]);P(0x42,C.pending[seat]);
    P(0x99,horizontal[C.raw[seat]&3]);P(0x9A,C.pending[seat]&128);
    C.pending[seat]=0;
}
static void initialize(void){
    C.owner=C.anchor=C.guest_round=0;C.initialized=1;
    capture(&C.player[0]);C.player[1]=C.player[0];
    mm6_proto_clone_animation(0,1);
    int x=((R(0x46F)<<8)|R(0x486))-16;if(x<16)x=16;
    put(&C.player[1],0x486,x);put(&C.player[1],0x46F,x>>8);
    for(int slot=1;slot<8;slot++)put(&C.player[1],0x3A0+slot,0);
    put(&C.player[1],0x9E,0);put(&C.player[1],0xA5,255);put(&C.player[1],0x22,0);
    if(C.retry==2){
        for(int seat=0;seat<2;seat++)for(int i=0;i<10;i++)put(&C.player[seat],0x688+i,C.retry_ammo[seat][i]);
        project(&C.player[0]);
    }
    C.dead[0]=C.dead[1]=C.retry=0;
}
static int begin(uint16_t address){
    (void)address;if(!s_on || cyc_mod_isolated())return 0;
    if(!C.initialized && C.eligible && R(0x3A0)==4 && R(0x3E5)<28 && !R(0x94) && !(R(0x3B7)&2))initialize();
    if(!C.initialized)return 0;
    C.owner=C.anchor;C.guest_round=0;capture(&C.player[C.owner]);C.world++;input_for(C.owner);return 0;
}
static int actor_end(uint16_t address){
    (void)address;
    if(!s_on || !C.initialized || cyc_mod_isolated() || R(0x8F)!=0)return 0;
    mm6_charge_actor_end();
    capture(&C.player[C.owner]);C.updates[C.owner]++;
    if(!C.guest_round){
        C.guest_round=1;C.owner=(uint8_t)(C.anchor^1);project(&C.player[C.owner]);input_for(C.owner);
        /* E47A decrements before dispatch. Reuse only personal actors7..0;
         * E477 has already restored the original native stack base. */
        P(0x8F,8);
    }else{
        if(C.dead[C.anchor] && !C.dead[C.anchor^1]){
            C.anchor^=1;P(0x46,R(0x46)+1);
        }
        C.guest_round=0;C.owner=C.anchor;project(&C.player[C.owner]);
        P(0x40,C.raw[C.owner]);P(0x42,0);
    }
    return 0;
}
void mm6_coop_arrival(void){
    if(!s_on || cyc_mod_isolated())return;
    C.initialized=0;C.eligible=1;C.owner=C.anchor=C.guest_round=0;
    if(C.retry==1)C.retry=2;
}
static int dying(uint16_t address){
    (void)address;if(s_on && C.initialized && !cyc_mod_isolated())C.dead[C.owner]=1;
    return 0;
}
static int death_music(uint16_t address){
    (void)address;
    if(!s_on || !C.initialized || cyc_mod_isolated() || !C.dead[C.owner] || C.dead[C.owner^1])return 0;
    /* ED3E's first request stops the common music. Keep the native death
     * cue and timed particles, while the surviving seat keeps its stage BGM. */
    CycModRegs r;cyc_mod_regs(&r);r.a=255;cyc_mod_set_regs(&r);return 0;
}
static int retry(uint16_t address){
    (void)address;if(!s_on || !C.initialized || cyc_mod_isolated())return 0;
    int seat=C.owner,other=seat^1;C.dead[seat]=2;
    if(C.dead[other] && !C.retry){
        /* The first completed native death owns the one shared-life retry.
         * Its original E83/E456 path, stack and high-level scene keep running. */
        capture(&C.player[seat]);
        for(int p=0;p<2;p++)for(int i=0;i<10;i++)C.retry_ammo[p][i]=(uint8_t)mm6_coop_read(p,0x688+i);
        C.retry=1;return 0;
    }
    /* A surviving partner keeps the real world active. Return through the
     * original actor caller; no guest clocks, stack or bank state are copied. */
    P(0x3A0,0);P(0x3FC,255);P(0x3E5,255);return 1;
}
void mm6_coop_input(uint8_t buttons[2]){
    if(!s_on)return;
    for(int i=0;i<2;i++){
        C.raw[i]=buttons[i];
        if(C.initialized && !(R(0x3B7)&2))C.pending[i]|=(uint8_t)(buttons[i]&~C.previous[i]);
        else C.pending[i]=0;
        C.previous[i]=buttons[i];
    }
    /* Title, stage selection and the completed team-wipe menu belong to
     * both controllers. A live personal inventory keeps its pauser's input. */
    int global=!C.initialized || (C.dead[0] && C.dead[1]);
    buttons[0]=global?(uint8_t)(C.raw[0]|C.raw[1]):C.raw[C.owner];buttons[1]=0;
    if(global){
        if((buttons[0]&3)==3)buttons[0]&=(uint8_t)~3;
        if((buttons[0]&12)==12)buttons[0]&=(uint8_t)~12;
    }
}
void mm6_coop_end(void){if(s_on && C.initialized)capture(&C.player[C.owner]);}
static int animation_stop(uint16_t address){(void)address;return s_animating && cyc_mod_isolated();}
void mm6_coop_build(void){
    memset(&C.build,0,sizeof C.build);
    if(!s_on || !C.initialized || R(0x3A0)!=4 || (R(0x3B7)&2) || C.guest_round)return;
    if(!cyc_mod_isolate_begin())return;
    int previous=C.owner,seat=C.anchor^1;C.owner=(uint8_t)seat;project(&C.player[seat]);
    uint8_t patterns[2048];
    int camera=(R(0x57)<<8)|R(0x56),ok=mm6_controls_read_graphics(patterns),shown=0;C.build.camera=(uint16_t)camera;C.build.seat=(uint8_t)seat;
    /* CF4A also owns the native passive Jet-fuel recharge. The anchor runs
     * it in D1D4; the other seat needs that same once-per-draw operation. */
    P(0x8C,0);CycModRegs hud;cyc_mod_regs(&hud);if(ok)ok=cyc_mod_call(0xCF4A,&hud);
    cyc_mod_allow_isolated_hooks(true);
    for(int slot=0;slot<8 && ok;slot++){
        if(!R(0x3A0+slot) || (R(0x3B7+slot)&2))continue;
        CycModRegs regs;cyc_mod_regs(&regs);regs.x=(uint8_t)slot;
        s_animating=1;ok=cyc_mod_call(0xE21A,&regs);s_animating=0;
        int relative=((R(0x46F+slot)<<8)|R(0x486+slot))-camera;
        if(!ok || (R(0x3B7+slot)&1) || R(0x4B4+slot) || relative<4 || relative>255)continue;
        for(int offset=0;offset<256;offset+=4)P(0x200+offset,240);
        P(0x8C,0);P(0x00,R(0x486+slot)-R(0xF7));regs.x=(uint8_t)slot;
        ok=cyc_mod_call(0xE2A9,&regs);int end=R(0x8C);if(!end)end=256;
        if(!slot){mm6_proto_body(&C.build.body);C.build.body.x=(int16_t)relative;shown=1;}
        for(int offset=0;ok && offset<end && C.build.count<PART_CAP;offset+=4){
            int top=R(0x200+offset)+1;if(top>=240)continue;
            Part *p=&C.build.parts[C.build.count++];p->x=(int16_t)(camera+R(0x203+offset));p->y=(int16_t)top;
            p->owner=(uint8_t)slot;p->attr=R(0x202+offset);int tile=R(0x201+offset);
            for(int i=0;i<4;i++)p->colors[i]=R(0x370+(p->attr&3)*4+i)&63;
            for(int y=0;y<8;y++){
                int row=(p->attr&128)?7-y:y,address=0x1000+tile*16+row;
                int low=address<0x1800?patterns[address-0x1000]:cyc_render_chr((uint16_t)address);
                int high=address<0x1800?patterns[address+8-0x1000]:cyc_render_chr((uint16_t)(address+8));
                for(int x=0;x<8;x++){int bit=(p->attr&64)?x:7-x;p->pixels[y*8+x]=(uint8_t)(((low>>bit)&1)|(((high>>bit)&1)<<1));}
            }
        }
    }
    if(ok){capture(&C.player[seat]);C.build.valid=1;if(!shown)C.build.body.active=0;}
    cyc_mod_allow_isolated_hooks(false);cyc_mod_isolate_end();C.owner=(uint8_t)previous;
}
void mm6_coop_latch(void){C.frame=C.build;}
int mm6_coop_has_frame(void){return s_on && C.frame.valid;}
void mm6_coop_draw(uint32_t *out,int width,int origin,const uint8_t *opaque,uint8_t *claimed){
    if(!mm6_coop_has_frame())return;int body=0;
    for(int i=0;i<C.frame.count;i++){
        const Part *p=&C.frame.parts[i];
        if(!p->owner && C.frame.body.active){if(!body){mm6_view_draw_character(out,width,origin,&C.frame.body);body=1;}continue;}
        for(int y=0;y<8;y++)for(int x=0;x<8;x++){
            int value=p->pixels[y*8+x],px=p->x-C.frame.camera+origin+x,py=p->y+y;
            if(!value || px<0 || px>=width || py<0 || py>=240)continue;
            int at=py*width+px;if(claimed[at])continue;claimed[at]=1;
            if(!(p->attr&32) || !opaque[at])out[at]=cyc_render_color_value(p->colors[value]);
        }
    }
}
void mm6_coop_power(void){memset(&C,0,sizeof C);addresses();mm6_charge_power();}
void mm6_coop_set(int enabled,int roster){
    s_on=enabled!=0;s_roster=roster!=0;C.initialized=C.eligible=C.owner=C.anchor=C.guest_round=0;
    mm6_charge_set(s_on);
    nes_mod_set_function_hook_enabled("mm6.coop.begin",s_on);nes_mod_set_function_hook_enabled("mm6.coop.actor-end",s_on);
    nes_mod_set_function_hook_enabled("mm6.coop.animation-stop",s_on);mm6_view_coop(s_on);
    nes_mod_set_function_hook_enabled("mm6.coop.dying",s_on);nes_mod_set_function_hook_enabled("mm6.coop.pit",s_on);
    nes_mod_set_function_hook_enabled("mm6.coop.retry",s_on);
    nes_mod_set_function_hook_enabled("mm6.coop.death-music",s_on);
    nes_mod_set_local_only("MM6 native co-op prototype (unqualified netplay)",s_on);
}
static int get(uint8_t *out,int cap){if(cap<sizeof C)return -1;memcpy(C.magic,"M6C4",4);C.enabled=(uint8_t)s_on;C.roster=(uint8_t)s_roster;memcpy(out,&C,sizeof C);return sizeof C;}
static int valid(const uint8_t *in,int len){
    if(!len)return !s_on;if(!in || len!=sizeof C)return 0;Coop p;memcpy(&p,in,sizeof p);
    if(memcmp(p.magic,"M6C4",4) || p.enabled!=s_on || p.roster!=s_roster || p.initialized>1 || p.eligible>1 || p.owner>1 || p.anchor>1 || p.guest_round>1 || p.dead[0]>2 || p.dead[1]>2 || p.retry>2 || (p.initialized && p.owner!=(p.anchor^p.guest_round)))return 0;
    const Picture *pictures[2]={&p.build,&p.frame};
    for(int k=0;k<2;k++){
        const Picture *q=pictures[k];if(q->valid>1 || q->seat>1 || q->count>PART_CAP || q->body.pose>=CYC_PROTO_POSE_COUNT || q->body.active>1 || q->body.form>2)return 0;
        for(int j=0;j<q->count;j++){const Part *part=&q->parts[j];if(part->owner>7)return 0;for(int n=0;n<4;n++)if(part->colors[n]>63)return 0;for(int n=0;n<64;n++)if(part->pixels[n]>3)return 0;}
    }
    return 1;
}
static int set(const uint8_t *in,int len){if(!valid(in,len))return 0;if(len)memcpy(&C,in,sizeof C);else memset(&C,0,sizeof C);return 1;}
void mm6_coop_register(void){
    mm6_charge_register();
    addresses();nes_mod_register_function_entry_plugin("mm6.coop.begin",0xE41C,begin);nes_mod_register_function_entry_plugin("mm6.coop.actor-end",0xE47A,actor_end);
    nes_mod_register_function_entry_plugin("mm6.coop.animation-stop",0xE24B,animation_stop);
    nes_mod_register_function_entry_plugin("mm6.coop.dying",0x8E62,dying);
    nes_mod_register_function_entry_plugin("mm6.coop.pit",0x8F03,dying);
    nes_mod_register_function_entry_plugin("mm6.coop.retry",0x8E83,retry);
    nes_mod_register_function_entry_plugin("mm6.coop.death-music",0xED40,death_music);
    nes_mod_register_savestate_hook("mm6.coop",get,set);nes_mod_register_savestate_validator("mm6.coop",valid);
}
int mm6_coop_json(char *out,int cap){
    return snprintf(out,cap,"\"coop\":{\"on\":%d,\"ready\":%u,\"owner\":%u,\"round\":%u,\"anchor\":%u,\"dead\":[%u,%u],\"retry\":%u,\"lives\":%u,\"world\":%llu,\"updates\":[%llu,%llu],\"players\":[[%d,%d,%d,%d,%d],[%d,%d,%d,%d,%d]]}",
        s_on,C.initialized,C.owner,C.guest_round,C.anchor,C.dead[0],C.dead[1],C.retry,R(0xA9),(unsigned long long)C.world,(unsigned long long)C.updates[0],(unsigned long long)C.updates[1],
        mm6_coop_read(0,0x486),mm6_coop_read(0,0x4CB),mm6_coop_read(0,0x3E5),mm6_coop_read(0,0x94),mm6_coop_read(0,0x699),
        mm6_coop_read(1,0x486),mm6_coop_read(1,0x4CB),mm6_coop_read(1,0x3E5),mm6_coop_read(1,0x94),mm6_coop_read(1,0x699));
}
