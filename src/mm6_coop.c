/* Each personal coroutine runs on the real clock; shared actors run once. */
#include "mm6_coop.h"
#include "mm6_proto.h"
#include "mm6_charge.h"
#include "mm6_world.h"
#include "mm6_wide.h"
#include "mm6_menu.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "cyc_video.h"
#include "cyc_ring.h"
#include "mod_function_hooks.h"
#include "mod_runtime.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
enum { PERSONAL_CAP=MM6_PERSONAL_CAP,PART_CAP=512 };
typedef struct {uint8_t ram[PERSONAL_CAP];} Player;
typedef struct {int16_t x,y;uint8_t owner,attr,colors[4],pixels[64];} Part;
typedef struct {uint8_t valid,seat;uint16_t count,camera;Mm6ProtoBody body;Part parts[PART_CAP];} Picture;
typedef struct {
    char magic[4];uint8_t enabled,roster,initialized,eligible,owner,anchor,guest_round;
    uint8_t raw[2],previous[2],pending[2];
    Player player[2];uint64_t world,updates[2];
    uint8_t dead[2],retry,retry_ammo[2][10];
    uint8_t playing,scene,scene_stage,scene_page,scene_leader,damage_owner[23];
    uint64_t transitions[2],finishers[2];
    uint64_t room_checks;uint16_t room_address;uint8_t room_flags;
    uint8_t inventory_pending,inventory_ammo[2][10];
    uint8_t source_pending,source_stack[4];CycModRegs source_regs;
    uint64_t source_handoffs[2];
    uint8_t target_active,target_owner,target_actor,target_input[4];
    uint64_t targets[2];
    uint8_t withdrawn,teleport[2],arriving,select_hold[2],select_armed[2],checkpoint,revive_pending,deactivate_pending;
    uint8_t wait_mode,waiting,wait_page;uint16_t wait_address;
    uint64_t withdrawals[2],rejoins[2],revivals[2];
    Picture build,frame;
} Coop;
static Coop C;
static int s_on,s_roster,s_count,s_animating,s_view,s_wait_both;
static uint8_t personal_camera_read(uint16_t pc,uint16_t address,uint8_t value);
static uint16_t s_addr[PERSONAL_CAP],s_index[2048];
static void life_tick(void);
static void finish_wait(void){C.waiting=0;C.wait_address=0;C.wait_page=0;}
static void continuation_hooks(void){
    static const char *ids[]={"dying","pit","retry","death-music","global-scene"};
    for(unsigned i=0;i<sizeof ids/sizeof *ids;i++){char id[64];snprintf(id,sizeof id,"mm6.coop.%s",ids[i]);nes_mod_set_function_hook_enabled(id,s_on || C.deactivate_pending);}
}
int mm6_coop_enabled(void){return s_on;}
int mm6_coop_ready(void){return s_on && C.initialized;}
int mm6_coop_seat(void){return s_on?C.owner:0;}
int mm6_coop_character(void){return s_on?(s_roster^C.owner):-1;}
int mm6_coop_character_for(int seat){return s_on && seat>=0 && seat<2?(s_roster^seat):0;}
int mm6_coop_independent(void){return s_on && s_view!=0;}
int mm6_coop_view_mode(void){return s_on?s_view:0;}
int mm6_coop_live(int seat){return s_on && C.initialized && seat>=0 && seat<2 && !C.dead[seat] && !((C.withdrawn|C.waiting)&(1u<<seat)) && !C.teleport[seat] && mm6_coop_read(seat,0x3A0)==4 && mm6_coop_read(seat,0x3E5)<28;}
int mm6_coop_wait_both(void){return s_wait_both;}
void mm6_coop_wait_set(int both){s_wait_both=both!=0;}
int mm6_coop_teleport(int seat){return s_on && seat>=0 && seat<2?(C.arriving&(1u<<seat)?-C.teleport[seat]:C.teleport[seat]):0;}
int mm6_coop_scene_locked(void){return C.source_pending || C.scene || R(0x47) || R(0x58) || R(0x59) || R(0x60) || R(0x642);}
void mm6_coop_view_set(int mode){
    if(mode<0 || mode>3)return;
    s_view=mode;mm6_view_coop(s_on);mm6_wide_views_changed();
    cyc_mod_set_ram_read_hook(s_on && s_view?personal_camera_read:NULL);
    cyc_video_override_width(mm6_coop_independent()?(s_view==1?512:256):0);
}
int mm6_coop_view_rects(int left[2]){
    if(!s_on || !C.initialized)return 0;
    int camera=(R(0x57)<<8)|R(0x56),lo,hi;
    int free_camera=!mm6_coop_scene_locked() && mm6_world_bounds(&lo,&hi);
    for(int seat=0;seat<2;seat++){
        int who=C.dead[seat] || ((C.withdrawn&(1u<<seat)) && !C.teleport[seat])?C.anchor:seat;left[seat]=camera;
        if(free_camera){
            int x=(mm6_coop_read(who,0x46F)<<8)|mm6_coop_read(who,0x486);
            left[seat]=x-128;if(left[seat]<lo)left[seat]=lo;if(left[seat]>hi-256)left[seat]=hi-256;
        }
    }
    return 2;
}
typedef struct {uint32_t frame;uint16_t address;uint8_t seat,value;} TestPoke;
static TestPoke s_test_pokes[64];static int s_test_poke_count;
static void addresses(void){
    if(s_count)return;
    /* Native Buster1..3, Beat4, hurt effect5 and friendly ricochets6/7.
     * Level actors8..21 and the director22 remain in the shared loop. */
    for(int base=0x3A0;base<=0x63B;base+=23)for(int slot=0;slot<8;slot++)s_addr[s_count++]=(uint16_t)(base+slot);
    /* AA..AC are the one shared room director's pointer and parser cursor,
     * not player scratch. A mid-script leader transfer must retain them. */
    for(int a=0x94;a<=0xAE;a++)if(a!=0xA9 && (a<0xAA || a>0xAC))s_addr[s_count++]=(uint16_t)a;
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
/* Shared actors retain their native coroutine, slots, RNG and one update.
 * Only the body they observe is selected; friendly actors/ammo stay owned. */
static int target_field(int a){
    return (a>=0x3A0 && a<=0x63B && (a-0x3A0)%23==0) ||
        (a>=0x94 && a<=0xAE && a!=0xA9 && (a<0xAA || a>0xAC)) ||
        a==0x22 || (a>=0x370 && a<=0x377);
}
static void target_capture(Player *p){for(int i=0;i<s_count;i++)if(target_field(s_addr[i]))p->ram[i]=R(s_addr[i]);}
static void target_project(const Player *p){for(int i=0;i<s_count;i++)if(target_field(s_addr[i]))P(s_addr[i],p->ram[i]);}
static int target_begin(uint16_t address){
    (void)address;
    if(!s_on || !C.initialized || C.target_active || C.source_pending || C.scene || cyc_mod_isolated() || (R(0x22)&128) || (R(0x3B7)&2))return 0;
    CycModRegs r;cyc_mod_regs(&r);if(r.x<8 || r.x>=22 || !R(0x3A0+r.x))return 0;
    target_capture(&C.player[C.owner]);
    int chosen=C.owner,best=0x7FFFFFFF,x=(R(0x46F+r.x)<<8)|R(0x486+r.x),y=R(0x4CB+r.x);
    for(int seat=0;seat<2;seat++)if(mm6_coop_live(seat)){
        int distance=abs(x-((personal(&C.player[seat],0x46F)<<8)|personal(&C.player[seat],0x486)))+abs(y-personal(&C.player[seat],0x4CB));
        if(distance<best){chosen=seat;best=distance;}
    }
    if(best==0x7FFFFFFF)return 0;
    C.targets[chosen]++;
    if(chosen==C.owner)return 0;
    C.target_active=1;C.target_owner=C.owner;C.target_actor=r.x;
    static const uint16_t input[]={0x40,0x42,0x99,0x9A};
    for(int i=0;i<4;i++)C.target_input[i]=R(input[i]);
    C.owner=(uint8_t)chosen;target_project(&C.player[chosen]);
    P(0x40,C.raw[chosen]&~0x30);P(0x42,C.pending[chosen]&~0x30);
    static const uint8_t horizontal[]={0,1,2,2};P(0x99,horizontal[C.raw[chosen]&3]);P(0x9A,C.pending[chosen]&128);
    return 0;
}
static void target_end(void){
    if(!C.target_active || R(0x8F)!=C.target_actor || cyc_mod_isolated())return;
    target_capture(&C.player[C.owner]);C.owner=C.target_owner;target_project(&C.player[C.owner]);
    static const uint16_t input[]={0x40,0x42,0x99,0x9A};
    for(int i=0;i<4;i++)P(input[i],C.target_input[i]);
    C.target_active=0;
    if(!s_on)mm6_coop_set(0,s_roster);
}
int mm6_coop_test_poke(const char *value){
    unsigned frame,seat,address,byte;int n=0;addresses();
    if(!value || s_test_poke_count>=64 || sscanf(value,"%u:%u:%x:%x%n",&frame,&seat,&address,&byte,&n)!=4 ||
        value[n] || seat>1 || address>=2048 || !s_index[address] || byte>255)return 0;
    s_test_pokes[s_test_poke_count++]=(TestPoke){frame,(uint16_t)address,(uint8_t)seat,(uint8_t)byte};
    nes_mod_set_local_only("MM6 personal RAM QA fixture",1);return 1;
}
static void test_pokes(void){
    for(int i=0;i<s_test_poke_count;i++)if(s_test_pokes[i].frame==cyc_ring_frame){
        const TestPoke *p=&s_test_pokes[i];put(&C.player[p->seat],p->address,p->value);
        if(p->seat==C.owner)P(p->address,p->value);
    }
}
int mm6_coop_read(int seat,int a){
    if(seat<0 || seat>1 || a<0 || a>=2048)return 0;
    return C.initialized && (seat!=C.owner || (C.target_active && !target_field(a)))?personal(&C.player[seat],a):R(a);
}
void mm6_coop_menu_copy(int seat,uint8_t out[MM6_PERSONAL_CAP]){
    memset(out,0,MM6_PERSONAL_CAP);for(int i=0;i<s_count;i++)out[i]=(uint8_t)mm6_coop_read(seat,s_addr[i]);
}
void mm6_coop_menu_capture(uint8_t out[MM6_PERSONAL_CAP]){
    memset(out,0,MM6_PERSONAL_CAP);for(int i=0;i<s_count;i++)out[i]=R(s_addr[i]);
}
void mm6_coop_menu_project(const uint8_t in[MM6_PERSONAL_CAP]){
    for(int i=0;i<s_count;i++)P(s_addr[i],in[i]);
}
void mm6_coop_menu_commit(int seat,const uint8_t in[MM6_PERSONAL_CAP]){
    if(!C.initialized || seat<0 || seat>1)return;
    memcpy(C.player[seat].ram,in,MM6_PERSONAL_CAP);
    if(seat==C.owner)project(&C.player[seat]);
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
static int world_class(int x,int y){
    Mm6WorldTile tile;if(x<0 || y<0 || y>=240 || !mm6_world_tile(x,y,&tile))return -1;
    int table=mm6_controls_prg(0x3E,0xDAFF+R(0x51));
    return mm6_controls_prg(0x3E,0xDB0F+table*16+(tile.flags>>4));
}
static int safe_arrival(int *x,int *y){
    if(!mm6_coop_live(C.owner) || R(0x94)!=0 || R(0x96) || R(0x4B4))return 0;
    int yy=R(0x4CB),base=(R(0x46F)<<8)|R(0x486),lo,hi;
    if(yy<16 || yy>222 || !mm6_world_bounds(&lo,&hi))return 0;
    static const int offset[]={24,-24,0,8,-8,16,-16,32,-32};
    for(unsigned i=0;i<sizeof offset/sizeof *offset;i++){
        int xx=base+offset[i];if(xx<lo+8 || xx>hi-9)continue;
        if(world_class(xx-7,yy-12)==0 && world_class(xx+7,yy-12)==0 && world_class(xx,yy)==0 &&
           world_class(xx-6,yy+12)==1 && world_class(xx+6,yy+12)==1){*x=xx;*y=yy;return 1;}
    }
    return 0;
}
static void start_arrival(int seat,int x,int y,int heal){
    Player *p=&C.player[seat];int hp=personal(p,0x3E5),form=personal(p,0x9B),weapon=personal(p,0x699),fuel=personal(p,0x9C);
    /* The survivor supplies only a real standing body's native continuation.
     * HP, equipment and ammo belong to the returning personal record. */
    for(int base=0x3A0;base<=0x63B;base+=23){put(p,base,R(base));for(int slot=1;slot<8;slot++)put(p,base+slot,0);}
    for(int a=0x94;a<=0xAE;a++)if(a!=0xA9 && (a<0xAA || a>0xAC))put(p,a,R(a));
    put(p,0x3A0,4);put(p,0x3E5,heal?27:hp);put(p,0x9B,heal?0:form);put(p,0x699,heal?0:weapon);
    put(p,0x9C,fuel);put(p,0x98,0);put(p,0x9D,0);put(p,0xA0,0);put(p,0xA1,0);
    put(p,0x486,x);put(p,0x46F,x>>8);put(p,0x4CB,y);put(p,0x4B4,0);put(p,0x49D,0);put(p,0x4E2,0);
    put(p,0x3B7,R(0x3B7)&~3);put(p,0x94,0);put(p,0x9E,0);put(p,0xA5,255);put(p,0xA2,60);put(p,0x22,0);
    put(p,0x9F,x);
    put(p,0x6A2,0);put(p,0x6A3,0);put(p,0x6A4,0);
    form=personal(p,0x9B);weapon=personal(p,0x699);
    int packet=mm6_controls_prg(0x38,form || !weapon?0x96CA+form:0x962F+weapon);uint8_t colors[8];
    put(p,0xAE,packet);put(p,0x6AB,mm6_controls_prg(0x38,0x9614+form));
    if(mm6_controls_palette_colors(seat,packet,colors))for(int i=0;i<8;i++)put(p,0x370+i,colors[i]);
    C.dead[seat]=0;C.withdrawn|=(uint8_t)(1u<<seat);C.arriving|=(uint8_t)(1u<<seat);C.teleport[seat]=1;
    C.revive_pending&=(uint8_t)~(1u<<seat);C.select_hold[0]=C.select_hold[1]=0;C.select_armed[seat]=0;
    if(heal)C.revivals[seat]++;else C.rejoins[seat]++;
    mm6_proto_clone_animation(C.owner,seat);P(0xDC,0x24);
    if(seat==C.owner)project(p);
}
static void depart(int seat){
    Player *p=&C.player[seat];C.withdrawn|=(uint8_t)(1u<<seat);C.arriving&=(uint8_t)~(1u<<seat);
    C.teleport[seat]=1;C.withdrawals[seat]++;C.select_hold[0]=C.select_hold[1]=0;C.select_armed[seat]=0;
    for(int slot=1;slot<8;slot++)put(p,0x3A0+slot,0);
    put(p,0x9E,0);put(p,0xA5,255);put(p,0xA2,60);P(0xDC,0x24);
    if(seat==C.owner)project(p);
}
static void life_tick(void){
    int checkpoint=R(0x684);
    if(checkpoint!=C.checkpoint){
        if(checkpoint>C.checkpoint)for(int seat=0;seat<2;seat++)if(C.dead[seat])C.revive_pending|=(uint8_t)(1u<<seat);
        C.checkpoint=(uint8_t)checkpoint;
    }
    if(C.scene || C.source_pending || C.target_active || (R(0x3B7)&2) || (R(0x22)&128) || R(0x47) || R(0x58) || R(0x59) || R(0x60))return;
    for(int seat=0;seat<2;seat++){
        if(C.waiting&(1u<<seat)){C.select_hold[seat]=0;continue;}
        if(C.teleport[seat]){
            if(++C.teleport[seat]>32){
                C.teleport[seat]=0;
                if(C.arriving&(1u<<seat)){C.withdrawn&=(uint8_t)~(1u<<seat);C.arriving&=(uint8_t)~(1u<<seat);}
                else put(&C.player[seat],0x3A0,0);
            }
            continue;
        }
        if((C.revive_pending&(1u<<seat)) && C.dead[seat] && mm6_coop_live(seat^1)){
            int x,y;if(safe_arrival(&x,&y))start_arrival(seat,x,y,1);
        }else if(C.withdrawn&(1u<<seat)){
            if(C.select_armed[seat] && (C.pending[seat]&0x20) && mm6_coop_live(seat^1)){
                int x,y;if(safe_arrival(&x,&y))start_arrival(seat,x,y,0);
            }
        }else if(mm6_coop_live(seat) && mm6_coop_live(seat^1) && (C.raw[seat]&0x20)){
            if(C.select_hold[seat]<90 && ++C.select_hold[seat]==90)depart(seat);
        }else C.select_hold[seat]=0;
    }
}
static int suspended_body(uint16_t address){
    (void)address;
    if((!s_on && !C.deactivate_pending) || !C.initialized || cyc_mod_isolated() || R(0x8F)>=8 ||
       (!((C.withdrawn|C.waiting)&(1u<<C.owner)) && !C.teleport[C.owner]))return 0;
    CycModRegs r;cyc_mod_regs(&r);
    return (R(0x100|(uint8_t)(r.s+1))|(R(0x100|(uint8_t)(r.s+2))<<8))==0xE452;
}
static void input_for(int seat){
    static const uint8_t horizontal[]={0,1,2,2};
    int blocked=((C.withdrawn|C.waiting)&(1u<<seat)) || C.teleport[seat];
    P(0x40,blocked?0:C.raw[seat]&~0x20);P(0x42,blocked?0:C.pending[seat]&~0x20);
    P(0x99,blocked?0:horizontal[C.raw[seat]&3]);P(0x9A,blocked?0:C.pending[seat]&128);
    C.pending[seat]=0;
}
static void promote(int seat){
    if(seat==C.owner)return;
    capture(&C.player[C.owner]);C.anchor=C.owner=(uint8_t)seat;C.guest_round=0;
    project(&C.player[seat]);P(0x46,R(0x46)+1);input_for(seat);
}
static int source_event(int seat,int page){
    int x=mm6_coop_read(seat,0x486),y=mm6_coop_read(seat,0x4CB);
    for(int id=0;id<128;id++){
        Mm6WorldActor a;if(!mm6_world_actor(id,&a))break;
        if(a.kind!=128 || a.x/256!=page || a.y>=128)continue;
        int ptr=mm6_controls_prg(0x39,0x82EB+a.y)|(mm6_controls_prg(0x39,0x836B+a.y)<<8);
        if(ptr<0x8000 || ptr>=0x9F00)continue;
        int at=ptr;if(mm6_controls_prg(0x39,at)>=128)at+=2;
        if(mm6_controls_prg(0x39,at)>3)continue;
        int frame=(mm6_controls_prg(0x39,at+1)<<8)|mm6_controls_prg(0x39,at+2);
        if((frame==0x8480 || frame==0x848E) && x>=240)return id;
        /* Read only the original director's bounded transition list. This
         * requests its source; the original script still accepts contact. */
        at+=3;
        for(int n=0;n<8 && at<0x9FFB;n++){
            int kind=mm6_controls_prg(0x39,at);if(kind<1 || kind>6)break;
            if(((kind==1 || kind==3) && y>=227) ||
                ((kind==2 || kind==4) && y<11 && mm6_coop_read(seat,0x94)==4) ||
                (kind==5 && x>=240) || (kind==6 && x<17))return id;
            at+=3;if(mm6_controls_prg(0x39,at)>=128)at++;
        }
    }
    return -1;
}
static int source_begin(void){
    if(!s_view || C.source_pending || C.guest_round || mm6_coop_scene_locked() || (R(0x3B7)&2))return 0;
    int lo,hi;if(!mm6_world_bounds(&lo,&hi))return 0;
    int camera=(R(0x57)<<8)|R(0x56),seat=C.anchor^1;
    if(!mm6_coop_live(seat) || mm6_coop_read(seat,0x4B4))return 0;
    int world=(mm6_coop_read(seat,0x46F)<<8)|mm6_coop_read(seat,0x486),page=world>>8;
    if(world>=camera+4 && world<camera+256)return 0;
    if(page*256<lo || (page+1)*256>hi)return 0;
    int id=source_event(seat,page);if(id<0)return 0;
    CycModRegs original;cyc_mod_regs(&original);
    /* Construct only native actor22 in scratch RAM; no director wait runs
     * in isolation. Its real continuation starts in the ordinary actor loop. */
    if(!cyc_mod_isolate_begin())return 0;
    CycModRegs r=original;r.y=0x3A;int ok=cyc_mod_call(0xCBC0,&r);
    r.y=(uint8_t)id;if(ok)ok=cyc_mod_call(0x81A7,&r);
    uint8_t fields[30];int n=0;
    ok=ok && r.x==22 && R(0x3B6)==128 && R(0x412)==id;
    if(ok)for(int base=0x3A0;base<=0x63B;base+=23)fields[n++]=R(base+22);
    cyc_mod_isolate_end();if(!ok)return 0;
    n=0;for(int base=0x3A0;base<=0x63B;base+=23)P(base+22,fields[n++]);
    promote(seat);C.source_regs=original;C.source_pending=1;C.source_handoffs[seat]++;
    P(0x56,0);P(0x57,page);P(0xF7,0);P(0x70,page*256>=camera?1:2);
    /* Handled RTS enters the original DC4E phase46 + DC66 loader on the
     * real CPU clock. Its native queue/NMI streams all visible/prefetch rows
     * and CHR. DC5A returns before the stage-load-only PLA suffix. A second
     * RTS returns here with the original caller frame and registers intact. */
    for(int i=0;i<4;i++){int a=0x100|((original.s-3+i)&255);C.source_stack[i]=R(a);}
    P(0x100|((original.s-3)&255),0x4D);P(0x100|((original.s-2)&255),0xDC);
    P(0x100|((original.s-1)&255),0x1B);P(0x100|original.s,0xE4);
    r=original;r.s=(uint8_t)(original.s-4);r.y=(uint8_t)page;cyc_mod_set_regs(&r);return 1;
}
static int source_loaded(uint16_t address){
    /* This return belongs to the pending native call even if its package
     * was disabled while the queue/NMI loader yielded. */
    (void)address;if(cyc_mod_isolated() || C.source_pending!=1)return 0;
    C.source_pending=2;return 1;
}
static void follow_room(void){
    if(!C.scene || C.scene==3 || C.dead[C.anchor^1] || ((C.withdrawn&(1u<<(C.anchor^1))) && !(C.arriving&(1u<<(C.anchor^1)))))return;
    Player *p=&C.player[C.anchor^1];
    /* The native controller's body/continuation follows the actual room
     * script. Health, weapon, ammo, form, palette and cursor stay personal. */
    for(int base=0x3A0;base<=0x63B;base+=23)if(base!=0x3E5)put(p,base,R(base));
    for(int a=0x94;a<=0xAE;a++)if(a!=0x9B && a!=0xA2 && a!=0xA9 && a!=0xAE)put(p,a,R(a));
    int x=(R(0x46F)<<8)|R(0x486),camera=(R(0x57)<<8)|R(0x56);
    /* Vertical arrivals may stand on a one-tile ledge or ladder. Retain
     * that authored entry column rather than placing the follower in a pit. */
    if(C.scene!=2)x+=x-camera<48?16:-16;
    put(p,0x486,x);put(p,0x46F,x>>8);
    for(int slot=1;slot<8;slot++)put(p,0x3A0+slot,0);
    put(p,0x9E,0);put(p,0xA5,255);put(p,0x22,0);
}
static int room_candidate(int seat,uint16_t address){
    if(!mm6_coop_live(seat))return 0;
    int camera=(R(0x57)<<8)|R(0x56),world=(mm6_coop_read(seat,0x46F)<<8)|mm6_coop_read(seat,0x486);
    /* Retail scroll can leave camera57 one page behind the body at a
     * shutter. Match its actual 256-pixel contact window, not page equality. */
    if(world-camera<4 || world-camera>255)return 0;
    int x=mm6_coop_read(seat,0x486),y=mm6_coop_read(seat,0x4CB);
    if(address==0xD441 || address==0xD64F)return x>=240;
    if(address==0xD6AF)return x<17;
    if(address==0xD53C || address==0xD583)return y>=227;
    return y<11 && mm6_coop_read(seat,0x94)==4;
}
static int peer_near(int seat,uint16_t address){
    int other=seat^1;
    if(!mm6_coop_live(other) || mm6_coop_read(seat,0x46F)!=mm6_coop_read(other,0x46F) || mm6_coop_read(other,0x4B4))return 0;
    int x=mm6_coop_read(other,0x486),y=mm6_coop_read(other,0x4CB);
    if(address==0xD441 || address==0xD64F)return x>=224;
    if(address==0xD6AF)return x<=32;
    if(address==0xD53C || address==0xD583)return y>=211;
    return y<=27 && mm6_coop_read(other,0x94)==4;
}
static int room_reject(uint16_t address){
    /* These native probes return carry clear when contact is not admitted.
     * The vertical/plain exits also consume the director's palette commands. */
    CycModRegs r;cyc_mod_regs(&r);r.p&=(uint8_t)~1;
    if(address!=0xD441){P(0x72,0);P(0x73,0);r.a=0;r.p=(uint8_t)((r.p&~128)|2);}
    cyc_mod_set_regs(&r);return 1;
}
static int room_check(uint16_t address){
    C.room_checks++;C.room_address=address;
    C.room_flags=(uint8_t)((s_on?1:0)|(C.initialized?2:0)|(C.scene?4:0)|(C.guest_round?8:0)|(cyc_mod_isolated()?16:0));
    if(!s_on || !C.initialized || C.scene || C.guest_round || cyc_mod_isolated())return 0;
    capture(&C.player[C.owner]);
    int seat=C.anchor;
    if(C.waiting && C.wait_address==address){
        int waiter=C.waiting==1?0:1;
        if(C.wait_page==mm6_coop_read(waiter,0x46F) && peer_near(waiter,address)){finish_wait();seat=waiter;goto admitted;}
    }
    if(!room_candidate(seat,address)){seat^=1;if(!room_candidate(seat,address))return 0;}
    if(s_wait_both && (mm6_coop_live(seat^1) || C.waiting) && !peer_near(seat,address)){
        /* Keep the real body/coroutine at its contact point. Only its personal
         * dispatch sleeps; the other controller and shared world keep running.
         * A competing exit releases the previous waiter before holding this
         * one, so at least one body always remains controllable. */
        finish_wait();C.waiting=(uint8_t)(1u<<seat);C.wait_address=address;C.wait_page=(uint8_t)mm6_coop_read(seat,0x46F);
        for(int slot=1;slot<8;slot++)put(&C.player[seat],0x3A0+slot,0);
        put(&C.player[seat],0x9E,0);put(&C.player[seat],0xA5,255);put(&C.player[seat],0x22,0);
        C.select_hold[0]=C.select_hold[1]=0;promote(seat^1);return room_reject(address);
    }
admitted:
    finish_wait();
    /* This is the original director's accepted contact, before any wait,
     * palette upload or room stream. Keep its real CPU/stack/bank frame. */
    promote(seat);C.scene=(uint8_t)((address==0xD441 || address==0xD64F || address==0xD6AF)?1:2);
    C.scene_leader=(uint8_t)seat;C.scene_stage=R(0x51);C.scene_page=R(0x57);C.transitions[seat]++;
    follow_room();return 0;
}
static int room_end(uint16_t address){
    (void)address;
    if(!s_on || !C.initialized || !C.scene || C.scene==3 || cyc_mod_isolated())return 0;
    follow_room();C.scene=0;return 0;
}
static int personal_camera(uint16_t address){
    (void)address;
    /* 8C09 already retained this body's previous-X and movement probe. Its
     * tail JMP DBAD may scroll the one cartridge camera only for the actual
     * scene leader; a second controller otherwise scrolls it a second time
     * and changes the native director's destination-page calculation. */
    return s_on && C.initialized && C.owner!=C.anchor && !cyc_mod_isolated();
}
static uint8_t personal_camera_read(uint16_t pc,uint16_t address,uint8_t value){
    if((address!=0x56 && address!=0x57) || !s_on || !s_view || !C.initialized ||
        !C.guest_round || C.scene || (R(0x3B7)&2) || R(0x8F)>7 || R(0x58) || R(0x59) || R(0x642) || cyc_mod_isolated())return value;
    /* These are the original personal viewport bounds, helper constructors
     * and friendly-actor cull reads. Never project a camera into shared RAM:
     * the native NMI, renderer, director and pending wait keep the real source.
     * Both CPU backends use this same operand boundary and original clocks. */
    int personal=pc==0xE495 || pc==0xE49B;
    if(R(0x4A)==0x38)switch(pc){
        case 0x8F2E:case 0x8F3E:case 0x9C94:case 0x9EBB:case 0x9EC0:
        case 0x9FA1:case 0x9FA6:case 0x9FD7:case 0x9FDC:personal=1;break;
    }
    int lo,hi;if(!personal || !mm6_world_bounds(&lo,&hi))return value;
    int left=((R(0x46F)<<8)|R(0x486))-128;
    if(left<lo)left=lo;if(left>hi-256)left=hi-256;
    return (uint8_t)(address==0x56?left:left>>8);
}
static int shot_hit(uint16_t address){
    (void)address;
    if(!s_on || !C.initialized || cyc_mod_isolated() || R(0x4A)!=0x38 || R(0x8F)>7)return 0;
    CycModRegs r={0};cyc_mod_regs(&r);
    if(r.y>=8 && r.y<23)C.damage_owner[r.y]=(uint8_t)(r.a?C.owner+1:0);
    return 0;
}
static int boss_lethal(uint16_t address){
    if(address==0x9A18 || address==0x9AFA){if(R(0x4A)!=0x31)return 0;}
    if(!s_on || !C.initialized || cyc_mod_isolated() || !R(0x642) || R(0x3ED)<128 || !C.damage_owner[8])return 0;
    int seat=C.damage_owner[8]-1;
    if(!mm6_coop_live(seat))return 0;
    promote(seat);C.scene=3;C.scene_leader=(uint8_t)seat;C.finishers[seat]++;return 0;
}
static int stage_begin(uint16_t address){
    (void)address;
    if(s_on && !cyc_mod_isolated())C.playing=1;
    return 0;
}
static int global_scene(uint16_t address){
    (void)address;
    if(!s_on && C.deactivate_pending && !cyc_mod_isolated()){
        finish_wait();C.deactivate_pending=C.initialized=0;mm6_coop_set(0,s_roster);return 0;
    }
    if(s_on && C.initialized && !cyc_mod_isolated()){
        capture(&C.player[C.owner]);C.inventory_pending=(uint8_t)(R(0xF0)==1);
        if(C.inventory_pending)for(int seat=0;seat<2;seat++)for(int i=0;i<10;i++)
            C.inventory_ammo[seat][i]=(uint8_t)mm6_coop_read(seat,0x688+i);
        finish_wait();C.playing=C.initialized=C.eligible=C.guest_round=C.scene=0;
        memset(&C.build,0,sizeof C.build);memset(&C.frame,0,sizeof C.frame);
    }
    return 0;
}
static int global_ammo(uint16_t address){
    if(!s_on || !C.inventory_pending || C.initialized || cyc_mod_isolated())return 0;
    if(address==0xCD32){C.inventory_pending=0;return 0;}
    if(R(0x4B)!=0x3B)return 0;
    CycModRegs r={0};cyc_mod_regs(&r);
    int weapon=address==0xBA75?9:address==0xBE39?r.y:r.x;
    if(weapon<0 || weapon>=10)return 0;
    /* Only actual retail grants/refills change both retained inventories.
     * BA3F refills acquired weapons before the early stages, but deliberately
     * skips stage9+. BE39 grants the demonstrated new weapon; neither the
     * demo actor nor the next native body may clone one seat's spent ammo. */
    for(int seat=0;seat<2;seat++)C.inventory_ammo[seat][weapon]=r.a;
    return 0;
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
    }else if(C.inventory_pending){
        for(int seat=0;seat<2;seat++)for(int i=0;i<10;i++)put(&C.player[seat],0x688+i,C.inventory_ammo[seat][i]);
        project(&C.player[0]);
    }
    C.dead[0]=C.dead[1]=C.retry=C.inventory_pending=0;
    C.withdrawn=C.arriving=C.revive_pending=C.deactivate_pending=0;C.checkpoint=R(0x684);
    finish_wait();
    memset(C.teleport,0,sizeof C.teleport);memset(C.select_hold,0,sizeof C.select_hold);memset(C.select_armed,0,sizeof C.select_armed);
}
static int begin(uint16_t address){
    (void)address;if(cyc_mod_isolated())return 0;
    if(C.source_pending==2){
        for(int i=0;i<4;i++)P(0x100|((C.source_regs.s-3+i)&255),C.source_stack[i]);
        cyc_mod_set_regs(&C.source_regs);C.source_pending=0;
    }
    if(C.deactivate_pending && !C.source_pending && !C.target_active){
        finish_wait();
        int chosen=C.anchor;
        for(int i=0;i<2;i++){int seat=C.anchor^i;if(!C.dead[seat] && !(C.withdrawn&(1u<<seat)) && personal(&C.player[seat],0x3A0)==4 && personal(&C.player[seat],0x3E5)<28){chosen=seat;break;}}
        project(&C.player[chosen]);C.deactivate_pending=C.initialized=0;mm6_coop_set(0,s_roster);return 0;
    }
    if(!s_on)return 0;
    if(!C.initialized && C.eligible && R(0x3A0)==4 && R(0x3E5)<28 && !R(0x94) && !(R(0x3B7)&2))initialize();
    if(!C.initialized)return 0;
    if(C.waiting){int waiter=C.waiting==1?0:1;if(!s_wait_both || !mm6_coop_live(waiter^1))finish_wait();}
    if(source_begin())return 1;
    C.owner=C.anchor;C.guest_round=0;capture(&C.player[C.owner]);C.world++;life_tick();input_for(C.owner);return 0;
}
static int actor_end(uint16_t address){
    (void)address;
    target_end();
    if((!s_on && !C.deactivate_pending) || !C.initialized || cyc_mod_isolated() || R(0x8F)!=0)return 0;
    mm6_charge_actor_end();
    capture(&C.player[C.owner]);C.updates[C.owner]++;
    if(!C.guest_round){
        C.guest_round=1;C.owner=(uint8_t)(C.anchor^1);project(&C.player[C.owner]);input_for(C.owner);
        /* E47A decrements before dispatch. Reuse only personal actors7..0;
         * E477 has already restored the original native stack base. */
        P(0x8F,8);
    }else{
        int promoted=0;
        if(!mm6_coop_live(C.anchor) && mm6_coop_live(C.anchor^1)){
            C.anchor^=1;promoted=1;P(0x46,R(0x46)+1);
        }
        C.guest_round=0;C.owner=C.anchor;project(&C.player[C.owner]);
        if(promoted && mm6_coop_independent() && !mm6_coop_scene_locked()){
            int lo,hi;
            if(mm6_world_bounds(&lo,&hi)){
                int previous=(R(0x57)<<8)|R(0x56),camera=((R(0x46F)<<8)|R(0x486))-128;
                if(camera<lo)camera=lo;if(camera>hi-256)camera=hi-256;
                /* The surviving seat now owns the real native actor loop.
                 * Transfer its horizontal camera before the native draw and
                 * scroll code sees a body several pages away. 71's low bits
                 * are the native right/left scroll permissions in this room. */
                P(0x56,camera);P(0x57,camera>>8);P(0xF7,camera);
                P(0x70,camera>=previous?1:2);
                P(0x71,(R(0x71)&~3)|(camera<hi-256?1:0)|(camera>lo?2:0));
            }
        }
        P(0x40,C.raw[C.owner]);P(0x42,0);
    }
    return 0;
}
void mm6_coop_arrival(void){
    if(!s_on || !C.playing || cyc_mod_isolated())return;
    C.initialized=0;C.eligible=1;C.owner=C.anchor=C.guest_round=0;
    if(C.retry==1)C.retry=2;
}
static int dying(uint16_t address){
    (void)address;if((s_on || C.deactivate_pending) && C.initialized && !cyc_mod_isolated())C.dead[C.owner]=1;
    return 0;
}
static int death_music(uint16_t address){
    (void)address;
    if((!s_on && !C.deactivate_pending) || !C.initialized || cyc_mod_isolated() || !C.dead[C.owner] || C.dead[C.owner^1])return 0;
    /* ED3E's first request stops the common music. Keep the native death
     * cue and timed particles, while the surviving seat keeps its stage BGM. */
    CycModRegs r;cyc_mod_regs(&r);r.a=255;cyc_mod_set_regs(&r);return 0;
}
static int retry(uint16_t address){
    (void)address;if((!s_on && !C.deactivate_pending) || !C.initialized || cyc_mod_isolated())return 0;
    int seat=C.owner,other=seat^1;C.dead[seat]=2;
    if((C.dead[other] || (C.withdrawn&(1u<<other))) && !C.retry){
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
    if(!s_on && !C.deactivate_pending)return;
    /* A native world iteration can span several output frames. Scheduled
     * personal fixtures belong to the frame input boundary, not E41C. */
    if(C.initialized)test_pokes();
    for(int i=0;i<2;i++){
        C.raw[i]=buttons[i];
        if(C.initialized && !(R(0x3B7)&2))C.pending[i]|=(uint8_t)(buttons[i]&~C.previous[i]);
        else C.pending[i]=0;
        C.previous[i]=buttons[i];
        if((C.withdrawn&(1u<<i)) && !(buttons[i]&0x20))C.select_armed[i]=1;
    }
    /* Title, stage selection and the completed team-wipe menu belong to
     * both controllers. A live personal inventory keeps its pauser's input. */
    int global=!C.initialized || (C.dead[0] && C.dead[1]);
    buttons[0]=global?(uint8_t)(C.raw[0]|C.raw[1]):((C.withdrawn|C.waiting)&(1u<<C.owner)) || C.teleport[C.owner]?0:C.raw[C.owner]&~0x20;buttons[1]=0;
    if(global){
        if((buttons[0]&3)==3)buttons[0]&=(uint8_t)~3;
        if((buttons[0]&12)==12)buttons[0]&=(uint8_t)~12;
    }
}
void mm6_coop_end(void){if(s_on && C.initialized){if(C.target_active)target_capture(&C.player[C.owner]);else capture(&C.player[C.owner]);follow_room();}}
static int animation_stop(uint16_t address){(void)address;return s_animating && cyc_mod_isolated();}
void mm6_coop_build(void){
    if(C.target_active)return;
    memset(&C.build,0,sizeof C.build);
    if(!s_on || !C.initialized || R(0x3A0)!=4 || (R(0x3B7)&2) || C.guest_round)return;
    if(!cyc_mod_isolate_begin())return;
    int previous=C.owner,seat=C.anchor^1;C.owner=(uint8_t)seat;project(&C.player[seat]);
    uint8_t patterns[2048];
    int camera=(R(0x57)<<8)|R(0x56),ok=mm6_controls_read_graphics(patterns),shown=0;
    C.build.camera=(uint16_t)camera;C.build.seat=(uint8_t)seat;
    if(mm6_coop_independent()){
        int left[2];if(mm6_coop_view_rects(left)){camera=left[seat];P(0x56,camera);P(0x57,camera>>8);P(0xF7,camera);}
    }
    /* CF4A also owns the native passive Jet-fuel recharge. The anchor runs
     * it in D1D4; the other seat needs that same once-per-draw operation. */
    P(0x8C,0);CycModRegs hud;cyc_mod_regs(&hud);if(ok && !((C.withdrawn|C.waiting)&(1u<<seat)) && !C.teleport[seat])ok=cyc_mod_call(0xCF4A,&hud);
    cyc_mod_allow_isolated_hooks(true);
    for(int slot=0;slot<8 && ok;slot++){
        if(!R(0x3A0+slot) || (R(0x3B7+slot)&2))continue;
        CycModRegs regs;cyc_mod_regs(&regs);regs.x=(uint8_t)slot;
        uint8_t invulnerable=R(0xA2);
        if(!slot && C.teleport[seat]){P(0xA2,0);P(0x3B7,R(0x3B7)&~1);}
        s_animating=1;ok=cyc_mod_call(0xE21A,&regs);s_animating=0;
        if(!slot && C.teleport[seat])P(0xA2,invulnerable);
        int relative=((R(0x46F+slot)<<8)|R(0x486+slot))-camera;
        if(!ok || (R(0x3B7+slot)&1) || R(0x4B4+slot) || relative<4 || relative>255)continue;
        for(int offset=0;offset<256;offset+=4)P(0x200+offset,240);
        P(0x8C,0);P(0x00,R(0x486+slot)-R(0xF7));regs.x=(uint8_t)slot;
        ok=cyc_mod_call(0xE2A9,&regs);int end=R(0x8C);if(!end)end=256;
        if(!slot){mm6_proto_body(&C.build.body);C.build.body.x=(int16_t)(relative+camera-C.build.camera);shown=1;}
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
        if(!p->owner && C.frame.body.active==1){if(!body){mm6_view_draw_character(out,width,origin,&C.frame.body);body=1;}continue;}
        if(!p->owner && C.frame.body.active==2 && !body){mm6_view_draw_adapter_head(out,width,origin,&C.frame.body);body=1;}
        for(int y=0;y<8;y++)for(int x=0;x<8;x++){
            int value=p->pixels[y*8+x],px=p->x-C.frame.camera+origin+x,py=p->y+y;
            if(!value || px<0 || px>=width || py<0 || py>=240)continue;
            if(!p->owner && mm6_view_adapter_head_mask(&C.frame.body,px-origin,py))continue;
            int at=py*width+px;if(claimed[at])continue;claimed[at]=1;
            if(!(p->attr&32) || !opaque[at])out[at]=cyc_render_color_value(p->colors[value]);
        }
    }
}
void mm6_coop_power(void){memset(&C,0,sizeof C);addresses();mm6_charge_power();mm6_menu_power();}
void mm6_coop_set(int enabled,int roster){
    if(!enabled && C.initialized)C.deactivate_pending=1;
    if(enabled)C.deactivate_pending=0;
    s_on=enabled!=0;s_roster=roster!=0;
    if(!s_on)mm6_menu_power();
    /* Keep ownership of the injected return frame until both native RTSes
     * complete. All ordinary co-op hooks may stop immediately. */
    if(!C.source_pending && !C.target_active && !C.deactivate_pending && !C.initialized){
        C.eligible=(uint8_t)(s_on && R(0x3A0)==4 && R(0x441)==0x38);C.owner=C.anchor=C.guest_round=C.scene=C.inventory_pending=0;
    }
    cyc_mod_set_ram_read_hook(s_on && s_view?personal_camera_read:NULL);
    mm6_charge_set(s_on);
    nes_mod_set_function_hook_enabled("mm6.coop.begin",s_on || C.source_pending || C.deactivate_pending);nes_mod_set_function_hook_enabled("mm6.coop.actor-end",s_on || C.target_active || C.deactivate_pending);
    nes_mod_set_function_hook_enabled("mm6.coop.suspended-body",s_on || C.deactivate_pending);
    nes_mod_set_function_hook_enabled("mm6.coop.target-begin",s_on);
    nes_mod_set_function_hook_enabled("mm6.coop.animation-stop",s_on);mm6_view_coop(s_on);
    nes_mod_set_function_hook_enabled("mm6.coop.dying",s_on);nes_mod_set_function_hook_enabled("mm6.coop.pit",s_on);
    nes_mod_set_function_hook_enabled("mm6.coop.retry",s_on);
    nes_mod_set_function_hook_enabled("mm6.coop.death-music",s_on);
    nes_mod_set_function_hook_enabled("mm6.coop.source-loaded",s_on || C.source_pending);
    static const char *hooks[]={"room-door","room-right","room-left","room-down-left","room-down-right","room-up-left","room-up-right","room-end-right","room-end-left","room-end-down-right","room-end-down-left","room-end-up-right","room-end-up-left","personal-camera","hit-normal","hit-piercing","hit-clear","hit-reflected","boss-lethal","robot-lethal","robot-jet-lethal","global-scene","stage-begin","ammo-refill","beat-refill","weapon-grant","new-game-inventory"};
    for(unsigned i=0;i<sizeof hooks/sizeof *hooks;i++){char id[80];snprintf(id,sizeof id,"mm6.coop.%s",hooks[i]);nes_mod_set_function_hook_enabled(id,s_on);}
    continuation_hooks();
    nes_mod_set_local_only("MM6 native co-op prototype (unqualified netplay)",s_on);
    mm6_coop_view_set(s_view);
}
static int get(uint8_t *out,int cap){if(cap<sizeof C)return -1;memcpy(C.magic,"M6CA",4);C.enabled=(uint8_t)s_on;C.roster=(uint8_t)s_roster;C.wait_mode=(uint8_t)s_wait_both;memcpy(out,&C,sizeof C);return sizeof C;}
static int valid(const uint8_t *in,int len){
    if(!len)return !s_on;if(!in || len!=sizeof C)return 0;Coop p;memcpy(&p,in,sizeof p);
    if(memcmp(p.magic,"M6CA",4) || p.enabled!=s_on || p.roster!=s_roster || p.initialized>1 || p.eligible>1 || p.owner>1 || p.anchor>1 || p.guest_round>1 || p.dead[0]>2 || p.dead[1]>2 || p.retry>2 || p.playing>1 || p.scene>3 || p.scene_leader>1 || p.inventory_pending>1 || p.source_pending>2 || (p.source_pending && (!p.initialized || p.guest_round || p.scene)) || (p.initialized && !p.target_active && p.owner!=(p.anchor^p.guest_round)))return 0;
    if(p.wait_mode!=s_wait_both || p.waiting>2 || (p.waiting && (!p.initialized || p.scene || (p.withdrawn&p.waiting) || (p.wait_address!=0xD441 && p.wait_address!=0xD64F && p.wait_address!=0xD6AF && p.wait_address!=0xD53C && p.wait_address!=0xD583 && p.wait_address!=0xD5CD && p.wait_address!=0xD60E))))return 0;
    if(p.target_active>1 || p.target_owner>1 || (p.target_active && (!p.initialized || p.guest_round || p.source_pending || p.scene || p.target_actor<8 || p.target_actor>=22 || p.target_owner!=p.anchor || p.owner==p.target_owner)))return 0;
    if(p.withdrawn>3 || p.arriving>3 || p.checkpoint>127 || p.revive_pending>3 || p.deactivate_pending>1)return 0;
    for(int seat=0;seat<2;seat++)if(p.teleport[seat]>32 || p.select_hold[seat]>90 || p.select_armed[seat]>1 ||
        ((p.arriving&(1u<<seat)) && (!(p.withdrawn&(1u<<seat)) || !p.teleport[seat])))return 0;
    for(int i=0;i<23;i++)if(p.damage_owner[i]>2)return 0;
    const Picture *pictures[2]={&p.build,&p.frame};
    for(int k=0;k<2;k++){
        const Picture *q=pictures[k];if(q->valid>1 || q->seat>1 || q->count>PART_CAP || q->body.pose>=CYC_PROTO_POSE_COUNT || q->body.active>2 || q->body.form>2 || q->body.native_palette>1 || (q->body.active==2 && !q->body.form))return 0;
        for(int j=0;j<q->count;j++){const Part *part=&q->parts[j];if(part->owner>7)return 0;for(int n=0;n<4;n++)if(part->colors[n]>63)return 0;for(int n=0;n<64;n++)if(part->pixels[n]>3)return 0;}
    }
    return 1;
}
static int set(const uint8_t *in,int len){
    if(!valid(in,len))return 0;if(len)memcpy(&C,in,sizeof C);else memset(&C,0,sizeof C);
    nes_mod_set_function_hook_enabled("mm6.coop.begin",s_on || C.source_pending || C.deactivate_pending);
    nes_mod_set_function_hook_enabled("mm6.coop.source-loaded",s_on || C.source_pending);
    nes_mod_set_function_hook_enabled("mm6.coop.actor-end",s_on || C.target_active || C.deactivate_pending);
    nes_mod_set_function_hook_enabled("mm6.coop.suspended-body",s_on || C.deactivate_pending);
    continuation_hooks();
    return 1;
}
void mm6_coop_register(void){
    mm6_menu_register();
    mm6_charge_register();
    addresses();nes_mod_register_function_entry_plugin("mm6.coop.begin",0xE41C,begin);nes_mod_register_function_entry_plugin("mm6.coop.actor-end",0xE47A,actor_end);
    nes_mod_register_function_entry_plugin("mm6.coop.target-begin",0xE43F,target_begin);
    nes_mod_register_function_entry_plugin("mm6.coop.suspended-body",0xE482,suspended_body);
    nes_mod_register_function_entry_plugin("mm6.coop.animation-stop",0xE24B,animation_stop);
    nes_mod_register_function_entry_plugin("mm6.coop.dying",0x8E62,dying);
    nes_mod_register_function_entry_plugin("mm6.coop.pit",0x8F03,dying);
    nes_mod_register_function_entry_plugin("mm6.coop.retry",0x8E83,retry);
    nes_mod_register_function_entry_plugin("mm6.coop.death-music",0xED40,death_music);
    nes_mod_register_function_entry_plugin("mm6.coop.source-loaded",0xDC5A,source_loaded);
    static const struct {const char *id;uint16_t address;} rooms[]={
        {"mm6.coop.room-door",0xD441},{"mm6.coop.room-right",0xD64F},{"mm6.coop.room-left",0xD6AF},
        {"mm6.coop.room-down-left",0xD53C},{"mm6.coop.room-down-right",0xD583},{"mm6.coop.room-up-left",0xD5CD},{"mm6.coop.room-up-right",0xD60E}};
    static const struct {const char *id;uint16_t address;} ends[]={
        {"mm6.coop.room-end-right",0xD75A},{"mm6.coop.room-end-left",0xD7AE},{"mm6.coop.room-end-down-right",0xD7EB},
        {"mm6.coop.room-end-down-left",0xD828},{"mm6.coop.room-end-up-right",0xD86D},{"mm6.coop.room-end-up-left",0xD8AA}};
    for(unsigned i=0;i<sizeof rooms/sizeof *rooms;i++)nes_mod_register_function_entry_plugin(rooms[i].id,rooms[i].address,room_check);
    for(unsigned i=0;i<sizeof ends/sizeof *ends;i++)nes_mod_register_function_entry_plugin(ends[i].id,ends[i].address,room_end);
    nes_mod_register_function_entry_plugin("mm6.coop.hit-normal",0x9024,shot_hit);
    nes_mod_register_function_entry_plugin("mm6.coop.hit-piercing",0x906A,shot_hit);
    nes_mod_register_function_entry_plugin("mm6.coop.hit-clear",0x9093,shot_hit);
    nes_mod_register_function_entry_plugin("mm6.coop.hit-reflected",0x90E1,shot_hit);
    nes_mod_register_function_entry_plugin("mm6.coop.boss-lethal",0xEFAB,boss_lethal);
    nes_mod_register_function_entry_plugin("mm6.coop.robot-lethal",0x9A18,boss_lethal);
    nes_mod_register_function_entry_plugin("mm6.coop.robot-jet-lethal",0x9AFA,boss_lethal);
    nes_mod_register_function_entry_plugin("mm6.coop.global-scene",0xCD67,global_scene);
    nes_mod_register_function_entry_plugin("mm6.coop.stage-begin",0xCDC3,stage_begin);
    nes_mod_register_function_entry_plugin("mm6.coop.personal-camera",0x8C39,personal_camera);
    nes_mod_register_function_entry_plugin("mm6.coop.ammo-refill",0xBA5D,global_ammo);
    nes_mod_register_function_entry_plugin("mm6.coop.beat-refill",0xBA75,global_ammo);
    nes_mod_register_function_entry_plugin("mm6.coop.weapon-grant",0xBE39,global_ammo);
    nes_mod_register_function_entry_plugin("mm6.coop.new-game-inventory",0xCD32,global_ammo);
    nes_mod_register_savestate_hook("mm6.coop",get,set);nes_mod_register_savestate_validator("mm6.coop",valid);
}
int mm6_coop_json(char *out,int cap){
    int n=snprintf(out,cap,"\"coop\":{\"wait_mode\":%d,\"waiting\":%u,\"wait_address\":%u,\"wait_page\":%u,\"on\":%d,\"ready\":%u,\"owner\":%u,\"round\":%u,\"anchor\":%u,\"dead\":[%u,%u],\"retry\":%u,\"lives\":%u,\"world\":%llu,\"updates\":[%llu,%llu],\"scene\":%u,\"room_checks\":%llu,\"room_address\":%u,\"room_flags\":%u,\"inventory_pending\":%u,\"source_pending\":%u,\"source_handoffs\":[%llu,%llu],\"transitions\":[%llu,%llu],\"finishers\":[%llu,%llu],\"pages\":[%d,%d],\"ammo1\":[%d,%d],\"ammo7\":[%d,%d],\"players\":[[%d,%d,%d,%d,%d],[%d,%d,%d,%d,%d]]}",
        s_wait_both,C.waiting,C.wait_address,C.wait_page,s_on,C.initialized,C.owner,C.guest_round,C.anchor,C.dead[0],C.dead[1],C.retry,R(0xA9),(unsigned long long)C.world,(unsigned long long)C.updates[0],(unsigned long long)C.updates[1],
        C.scene,(unsigned long long)C.room_checks,C.room_address,C.room_flags,C.inventory_pending,C.source_pending,(unsigned long long)C.source_handoffs[0],(unsigned long long)C.source_handoffs[1],(unsigned long long)C.transitions[0],(unsigned long long)C.transitions[1],(unsigned long long)C.finishers[0],(unsigned long long)C.finishers[1],mm6_coop_read(0,0x46F),mm6_coop_read(1,0x46F),mm6_coop_read(0,0x689),mm6_coop_read(1,0x689),mm6_coop_read(0,0x68F),mm6_coop_read(1,0x68F),
        mm6_coop_read(0,0x486),mm6_coop_read(0,0x4CB),mm6_coop_read(0,0x3E5),mm6_coop_read(0,0x94),mm6_coop_read(0,0x699),
        mm6_coop_read(1,0x486),mm6_coop_read(1,0x4CB),mm6_coop_read(1,0x3E5),mm6_coop_read(1,0x94),mm6_coop_read(1,0x699));
    if(n>0 && n<cap && out[n-1]=='}')return n-1+snprintf(out+n-1,cap-n+1,",\"targets\":[%llu,%llu],\"target_active\":%u,\"withdrawn\":%u,\"teleport\":[%d,%d],\"select_hold\":[%u,%u],\"withdrawals\":[%llu,%llu],\"rejoins\":[%llu,%llu],\"revivals\":[%llu,%llu],\"checkpoint\":%u,\"revive_pending\":%u,\"deactivate_pending\":%u}",(unsigned long long)C.targets[0],(unsigned long long)C.targets[1],C.target_active,C.withdrawn,mm6_coop_teleport(0),mm6_coop_teleport(1),C.select_hold[0],C.select_hold[1],(unsigned long long)C.withdrawals[0],(unsigned long long)C.withdrawals[1],(unsigned long long)C.rejoins[0],(unsigned long long)C.rejoins[1],(unsigned long long)C.revivals[0],(unsigned long long)C.revivals[1],C.checkpoint,C.revive_pending,C.deactivate_pending);
    return n;
}
