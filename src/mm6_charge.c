/* The retail charge task only sequences palette packets and its start cue.
 * Keep the native Buster physics; each seat owns this small presentation task. */
#include "mm6_charge.h"
#include "mm6_coop.h"
#include "mm6_proto.h"
#include "cyc_mod.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
typedef struct {
    char magic[4];
    uint8_t on,active,stage,origin,cursor,delay,tail;
} Fx;
static Fx F[2];
static int s_on;
static int packet(int seat,int id){
    uint8_t colors[8];
    if(!mm6_controls_palette_colors(seat,id,colors))return 0;
    mm6_coop_colors(seat,colors);return 1;
}
static int start(uint16_t address){
    (void)address;CycModRegs r;cyc_mod_regs(&r);
    if(!s_on || !mm6_coop_ready() || cyc_mod_isolated() || r.x!=2)return 0;
    Fx *f=&F[mm6_coop_seat()];
    f->active=1;f->stage=0;f->origin=(uint8_t)(R(0x9B)?0x2D:0);f->tail=(uint8_t)(R(0x9B)?0x23:0x1E);
    f->cursor=(uint8_t)(f->origin+1);f->delay=0;
    P(0x22,0);P(0xDC,0x29);
    /* Exact C5C7 return values for initializer2; the shared task's stack and
     * saved PC/banks are unnecessary for a per-seat palette sequencer. */
    r.a=0x3B;r.y=1;r.p&=(uint8_t)~0x82;cyc_mod_set_regs(&r);return 1;
}
void mm6_charge_actor_end(void){
    if(!s_on || !mm6_coop_ready())return;
    int seat=mm6_coop_seat();Fx *f=&F[seat];
    if(f->active && (R(0x9E)<0x24 || R(0x3E5)>=28 || R(0x3A0)!=4)){
        f->active=0;
        if(R(0x3A0)==4)packet(seat,R(0xAE));
    }
}
void mm6_charge_end(void){
    /* DEBD pauses task1 before the inventory fade hides the body. */
    if(!s_on || !mm6_coop_ready() || (R(0x22)&128) || (R(0x3B7)&2))return;
    for(int seat=0;seat<2;seat++){
        Fx *f=&F[seat];if(!f->active)continue;
        if(mm6_coop_read(seat,0x3A0)!=4 || mm6_coop_read(seat,0x3E5)>=28 || mm6_coop_read(seat,0x9E)<0x24){f->active=0;continue;}
        if(f->delay && --f->delay)continue;
        int id=mm6_controls_prg(0x3B,0x9143+f->cursor++);
        if(!id){f->cursor=(uint8_t)(f->origin+1);id=mm6_controls_prg(0x3B,0x9143+f->cursor++);}
        if(id==255){
            if(f->stage){f->active=0;continue;}
            f->stage=1;f->origin=f->tail;f->cursor=(uint8_t)(f->origin+1);
            id=mm6_controls_prg(0x3B,0x9143+f->cursor++);
        }
        if(!packet(seat,id)){f->active=0;continue;}
        f->delay=(uint8_t)mm6_controls_prg(0x3B,0x9143+f->origin);
    }
}
void mm6_charge_power(void){memset(F,0,sizeof F);}
void mm6_charge_set(int enabled){s_on=enabled!=0;nes_mod_set_function_hook_enabled("mm6.coop.charge-start",s_on);if(!s_on)memset(F,0,sizeof F);}
static int get(uint8_t *out,int cap){
    if(cap<sizeof F)return -1;
    for(int i=0;i<2;i++){memcpy(F[i].magic,"M6F1",4);F[i].on=(uint8_t)s_on;}
    memcpy(out,F,sizeof F);return sizeof F;
}
static int valid(const uint8_t *in,int len){
    if(!len)return !s_on;if(!in || len!=sizeof F)return 0;
    Fx f[2];memcpy(f,in,sizeof f);
    for(int i=0;i<2;i++)if(memcmp(f[i].magic,"M6F1",4) || f[i].on!=s_on || f[i].active>1 || f[i].stage>1 || f[i].origin>0x5F || f[i].cursor>0x70 || f[i].delay>32 || (f[i].tail && f[i].tail!=0x1E && f[i].tail!=0x23))return 0;
    return 1;
}
static int set(const uint8_t *in,int len){if(!valid(in,len))return 0;if(len)memcpy(F,in,sizeof F);else memset(F,0,sizeof F);return 1;}
void mm6_charge_register(void){
    nes_mod_register_function_entry_plugin("mm6.coop.charge-start",0xC5C7,start);
    nes_mod_register_savestate_hook("mm6.charge-fx",get,set);nes_mod_register_savestate_validator("mm6.charge-fx",valid);
}
