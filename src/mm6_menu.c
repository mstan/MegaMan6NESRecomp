/* The live native inventory owns the common pause. Its other pane uses the
 * same native construction/cursor routines in a disposable machine scope. */
#include "mm6_menu.h"
#include "mm6_coop.h"
#include "mm6_proto.h"
#include "cyc_core.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "mod_function_hooks.h"
#include "mod_savestate.h"
#include <stdio.h>
#include <string.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
#define P(a,v) cyc_mod_poke((uint16_t)(a),(uint8_t)(v))
typedef struct {uint8_t page,column,row,form,weapon,hp;} Choice;
typedef struct {
    char magic[4];uint8_t active,owner,closing,ready[2],usable[2],previous[2],pending[2],heal,heal_clock,equipped_form[2],sound[2];
    uint32_t ticks;uint64_t opens[2],changes[2],tanks[2],failures;
    uint8_t preview_valid[2],preview_form[2],preview[2][30];
    uint8_t base[2][MM6_PERSONAL_CAP];Choice choice[2];
    uint8_t image[2][256*240],palette[2][32];
} Menu;
static Menu M;
static int s_capture,s_stop,s_confirm,s_heal_boundary,s_heal_step,s_operation;
static uint8_t s_stack;
static int build(int seat,uint8_t input,int healing,int finish);
static Choice read_choice(void){Choice c={R(0x6A4),R(0x6A2),R(0x6A3),R(0x9B),R(0x699),R(0x3E5)};return c;}
static void project_choice(Choice c){P(0x6A4,c.page);P(0x6A2,c.column);P(0x6A3,c.row);P(0x9B,c.form);P(0x699,c.weapon);P(0x3E5,c.hp);}
static int stop(void){CycModRegs r;cyc_mod_regs(&r);r.s=s_stack;cyc_mod_set_regs(&r);s_stop=1;return 1;}
static int boundary(uint16_t a){
    if(cyc_mod_isolated()){
        if(!s_capture)return 0;
        if(a==0xC5F6 || a==0xC62B)return 1; /* Private waits, never the live clock. */
        if(a==0x9366 && s_operation!=0)return 0;
        if(a==0x953D){
            if(s_heal_step){s_heal_step=0;return 0;}
            s_heal_boundary=1;return stop();
        }
        if(a==0x958A || a==0x958E || a==0x965F)s_confirm=1;
        return stop();
    }
    if(a==0x9366 && mm6_coop_ready() && !M.active){
        M.active=1;M.closing=0;M.owner=(uint8_t)mm6_coop_seat();M.ticks=0;M.heal=M.heal_clock=0;M.opens[M.owner]++;
        for(int seat=0;seat<2;seat++){
            mm6_coop_menu_copy(seat,M.base[seat]);
            M.choice[seat]=(Choice){(uint8_t)mm6_coop_read(seat,0x6A4),(uint8_t)mm6_coop_read(seat,0x6A2),(uint8_t)mm6_coop_read(seat,0x6A3),
                (uint8_t)mm6_coop_read(seat,0x9B),(uint8_t)mm6_coop_read(seat,0x699),(uint8_t)mm6_coop_read(seat,0x3E5)};
            M.ready[seat]=M.pending[seat]=0;
            M.preview_valid[seat]=0;
            M.sound[seat]=0xFF;
            M.equipped_form[seat]=M.choice[seat].form;
            M.usable[seat]=(uint8_t)mm6_coop_live(seat);
            if(M.choice[seat].hp>27)M.choice[seat].hp=0;
        }
    }
    /* A changed adapter starts its original animated exit before958E.
     * Finish the other personal selection before that live display script. */
    if((a==0x958E || a==0x965F) && M.active && !M.closing){if(mm6_coop_independent() && M.usable[M.owner^1])build(M.owner^1,0x10,0,1);M.closing=1;}
    return 0;
}
typedef struct {const char *id;int enabled;} Hook;
static Hook s_hooks[256];
static int scope_hooks(int on){
    int n=nes_mod_function_hook_count();if(n>256)return 0;
    for(int i=0;i<n;i++){
        if(on){nes_mod_function_hook_at(i,&s_hooks[i].id,NULL,&s_hooks[i].enabled,NULL);
            nes_mod_set_function_hook_enabled(s_hooks[i].id,!strncmp(s_hooks[i].id,"mm6.menu.",9));}
        else nes_mod_set_function_hook_enabled(s_hooks[i].id,s_hooks[i].enabled);
    }
    cyc_mod_allow_isolated_hooks(on!=0);return 1;
}
static int call(uint16_t address){
    CycModRegs r;cyc_mod_regs(&r);s_stack=(uint8_t)(r.s-2);s_stop=0;
    return cyc_mod_call(address,&r) && s_stop;
}
static int flush(void){CycModRegs r;cyc_mod_regs(&r);return cyc_mod_call(0xC6D7,&r);}
static int pixels(int seat){
    static uint8_t opaque[256*240],claimed[256*240];
    size_t nt_size,chr_size;const uint8_t *nt=cyc_ppu_ciram(&nt_size),*chr=cyc_chr_ram(&chr_size);
    if(!nt || nt_size<1024 || !chr || chr_size<8192)return 0;
    for(int c=0;c<32;c++)M.palette[seat][c]=R(0x360+c)&63;
    for(int y=0;y<240;y++)for(int x=0;x<256;x++){
        int row=y/8,col=x/8,tile=nt[row*32+col],attr=nt[960+row/4*8+col/4],pal=(attr>>((row&2)*2+(col&2)))&3;
        int at=(R(0xFC)&16?4096:0)+tile*16+(y&7),bit=7-(x&7),v=((chr[at]>>bit)&1)|(((chr[at+8]>>bit)&1)<<1);
        if(x<8 && !(cyc_render_line_mask(y)&2))v=0;
        M.image[seat][y*256+x]=(uint8_t)(v?pal*4+v:0);
        opaque[y*256+x]=(uint8_t)(v!=0);
    }
    memset(claimed,0,sizeof claimed);
    for(int i=0;i<64;i++){
        int top=R(0x200+i*4)+1,tile=R(0x201+i*4),attr=R(0x202+i*4),left=R(0x203+i*4);
        if(top>=240)continue;
        int height=cyc_render_line_sprite16(top)?16:8;
        for(int y=0;y<height && top+y<240;y++)for(int x=0;x<8 && left+x<256;x++){
            int row=attr&128?height-1-y:y,bit=attr&64?x:7-x;
            int at=height==16?(tile&1)*4096+((tile&254)+row/8)*16+row%8:
                cyc_render_line_sprite_table(top+y)+tile*16+row;
            int v=((chr[at]>>bit)&1)|(((chr[at+8]>>bit)&1)<<1);
            int p=(top+y)*256+left+x;
            if(left+x<8 && !(cyc_render_line_mask(top+y)&4))continue;
            if(v && !claimed[p]){claimed[p]=1;if(!(attr&32) || !opaque[p])M.image[seat][p]=(uint8_t)(16+(attr&3)*4+v);}
        }
    }
    return 1;
}
static int build(int seat,uint8_t input,int healing,int finish){
    uint8_t result[MM6_PERSONAL_CAP],sound=0xFF;Choice next=M.choice[seat];int before=R(0x6A1),after=before,commit=0;
    if(!cyc_mod_isolate_begin())return 0;
    int ok=scope_hooks(1);s_capture=1;s_confirm=s_heal_boundary=0;
    mm6_coop_menu_project(M.base[seat]);
    P(0x3E5,next.hp);
    CycModRegs r;cyc_mod_regs(&r);r.y=0x38;
    if(ok)ok=cyc_mod_call(0xCBC0,&r) && cyc_mod_call(0x96D0,&r);
    P(0xFD,0);P(0x48,0x50); /* The native entry's completed fade-out precondition. */
    s_operation=0;if(ok)ok=call(0x9282); /* Full native construction; no saved display stack. */
    if(ok){
        project_choice(next);P(0x04,M.equipped_form[seat]);P(0x40,input);P(0x42,input);P(0x47,0);P(0xFD,0);
        if(input || healing){
            P(0xDC,0xFF);
            s_heal_step=healing;uint16_t pc=healing?0x953D:next.page==0?0x93A4:next.page==1?0x9477:0x9521;
            s_operation=2;ok=call(pc);next=read_choice();
            if(ok && healing && next.hp==27 && s_heal_boundary){s_heal_step=1;s_heal_boundary=0;ok=call(0x953D);next=read_choice();}
            sound=R(0xDC);
        }
    }
    if(ok && (s_confirm || finish)){
        P(0xFD,0);s_confirm=0;s_operation=3;
        ok=call(0x95CC); /* Native personal graphics/charge cleanup; owner restores the world. */
        cyc_mod_regs(&r);if(ok)ok=cyc_mod_call(0x96DE,&r);
        if(ok){mm6_coop_menu_capture(result);commit=1;}
    }
    if(ok){
        project_choice(next);P(0x47,0);s_operation=1;ok=call(0x9366) && flush();next=read_choice();
        if(ok && next.page==0 && M.preview_valid[seat] && M.preview_form[seat]==next.form)
            for(int field=0;field<30;field++)P(0x3A6+field*23,M.preview[seat][field]);
        cyc_mod_regs(&r);if(ok)ok=cyc_mod_call(0xE1DD,&r) && pixels(seat);
        if(ok){
            M.preview_valid[seat]=(uint8_t)(next.page==0);M.preview_form[seat]=next.form;
            if(M.preview_valid[seat])for(int field=0;field<30;field++)M.preview[seat][field]=R(0x3A6+field*23);
        }
    }
    after=R(0x6A1);
    s_capture=0;scope_hooks(0);cyc_mod_isolate_end();
    if(ok){
        if(memcmp(&next,&M.choice[seat],sizeof next))M.changes[seat]++;
        M.choice[seat]=next;M.ready[seat]=1;
        if(sound!=0xFF)M.sound[seat]=sound;
        if(commit){mm6_coop_menu_commit(seat,result);memcpy(M.base[seat],result,sizeof result);M.equipped_form[seat]=next.form;}
        if(after==before-1){P(0x6A1,after);M.tanks[seat]++;}
        if(s_heal_boundary)M.heal=(uint8_t)(next.hp<27);
        else if(healing)M.heal=0;
    }else{M.ready[seat]=0;M.failures++;}
    return ok;
}
void mm6_menu_power(void){memset(&M,0,sizeof M);}
void mm6_menu_input(uint8_t buttons[2]){
    for(int seat=0;seat<2;seat++){
        if(M.active && !M.closing && mm6_coop_independent() && M.usable[seat])M.pending[seat]|=(uint8_t)(buttons[seat]&~M.previous[seat]);
        M.previous[seat]=buttons[seat];
    }
    if(M.active && M.heal)buttons[M.owner]&=(uint8_t)~0x90u;
}
void mm6_menu_frame(void){
    if(!M.active)return;
    if(!mm6_coop_ready() || !(R(0x3B7)&2)){M.active=M.closing=0;return;}
    for(int seat=0;seat<2;seat++)if(M.sound[seat]!=0xFF && R(0xDC)==0xFF){P(0xDC,M.sound[seat]);M.sound[seat]=0xFF;}
    if(M.closing)return;
    int other=M.owner^1;M.ticks++;
    M.choice[M.owner]=read_choice();
    M.ready[M.owner]=1;
    if(!mm6_coop_independent())return;
    int healing=M.heal && ++M.heal_clock>=4;if(healing)M.heal_clock=0;
    build(other,M.pending[other],healing,0);M.pending[other]=0;
}
static uint32_t portrait_color(uint32_t c,int gray,int shade){
    if(gray){
        int r=(c>>16)&255,g=(c>>8)&255,b=c&255,hi=r>g?r:g;if(b>hi)hi=b;
        int index=hi>220?0x20:hi>64?0x10:hi?0:0x0F;
        return cyc_render_color_value(index-shade<0?15:(index-shade)&63);
    }
    for(int index=0;index<64;index++)if((cyc_render_color_value(index)&0xFFFFFFu)==(c&0xFFFFFFu))
        return cyc_render_color_value(index-shade<0?15:(index-shade)&63);
    int scale=shade>=0x30?0:0x30-shade;
    return 0xFF000000u|((((c>>16)&255)*scale/0x30)<<16)|((((c>>8)&255)*scale/0x30)<<8)|((c&255)*scale/0x30);
}
static void portrait(uint32_t *out,int width,int origin,int span,int seat){
    const CycProtoAtlas *atlas=mm6_proto_atlas();
    if(!atlas || !mm6_coop_character_for(seat))return;
    const CycProtoPose *pose=&atlas->poses[CYC_PROTO_IDLE0];
    int shade=M.closing?R(0x48):0;
    for(int form=0;form<3;form++){
        if(form && !(R(form==1?0x695:0x694)&1))continue;
        int gray=M.choice[seat].page!=0 || M.choice[seat].form!=form;
        uint32_t colors[6];for(int c=0;c<6;c++)colors[c]=portrait_color(atlas->colors[c],gray,shade);
        /* Native header anchors are forty pixels apart. Normal gets the
         * credited idle pose; adapters retain their native bodies and gear. */
        int first=form?4:0,last=form?20:pose->width,top=form?8:0,bottom=form?22:pose->height;
        int clear_left=form?32+40*form:28,clear_right=form?48+40*form:56,clear_bottom=form?46:56;
        for(int y=32;y<clear_bottom;y++)for(int x=clear_left;x<clear_right;x++)
            for(int dx=origin+x*span/256;dx<origin+(x+1)*span/256;dx++)if(dx>=0 && dx<width)out[y*width+dx]=0xFF000000u;
        for(int y=top;y<bottom;y++)for(int x=first;x<last;x++){
            int c=pose->pixels[y*32+x];if(!c)continue;
            int px=40+40*form+x-pose->origin_x,py=44+pose->top_y+y;
            if(py<32 || py>=56 || !(cyc_render_line_mask(py)&8))continue;
            for(int dx=origin+px*span/256;dx<origin+(px+1)*span/256;dx++)if(dx>=0 && dx<width)out[py*width+dx]=colors[c-1];
        }
    }
}
int mm6_menu_draw(uint32_t *out,int width,int height,const uint32_t *native){
    if(!M.active || !native || width<256 || height!=240)return 0;
    if(!mm6_coop_independent()){
        if(!native || !mm6_coop_character_for(M.owner))return 0;
        int left=(width-256)/2;for(int i=0;i<width*height;i++)out[i]=0xFF000000u;
        for(int y=0;y<height;y++)memcpy(out+y*width+left,native+y*256,256*sizeof *out);
        portrait(out,width,left,256,M.owner);
        cyc_render_set_native_origin(left);return 1;
    }
    if(!M.ready[0] || !M.ready[1])return 0;
    int mode=mm6_coop_view_mode(),half=mode==1?width/2:width;
    for(int y=0;y<240;y++)for(int x=0;x<width;x++){
        int seat=mode==1?(x>=half):(mode==3),left=mode==1 && seat?half:0,span=mode==1 && seat?width-half:half;
        int sample=y*256+(x-left)*256/span;
        if(seat==M.owner)out[y*width+x]=native[sample];
        else{
            int index=M.image[seat][sample],color=(int)M.palette[seat][index&3?index:0]-(M.closing?R(0x48):0);
            out[y*width+x]=M.closing && !(cyc_render_line_mask(y)&8)?0xff000000u:cyc_render_color_value(color<0?15:color&63);
        }
    }
    if(mode==1){portrait(out,width,0,half,0);portrait(out,width,half,width-half,1);}
    else portrait(out,width,0,width,mode==3);
    cyc_render_set_native_origin(0);return 1;
}
static int get(uint8_t *out,int cap){if(cap<sizeof M)return -1;memcpy(M.magic,"M6N2",4);memcpy(out,&M,sizeof M);return sizeof M;}
static int valid(const uint8_t *in,int len){
    if(!len)return !mm6_coop_enabled();if(!in || len!=sizeof M)return 0;Menu n;memcpy(&n,in,len);
    if(memcmp(n.magic,"M6N2",4) || n.active>1 || n.owner>1 || n.closing>1 || n.heal>1 || n.heal_clock>4)return 0;
    for(int seat=0;seat<2;seat++){
        if(n.ready[seat]>1 || n.usable[seat]>1 || n.choice[seat].page>2 || n.choice[seat].column>1 || n.choice[seat].row>4 || n.choice[seat].form>2 || n.choice[seat].weapon>9)return 0;
        if(n.preview_valid[seat]>1 || n.preview_form[seat]>2)return 0;
        for(int i=0;i<32;i++)if(n.palette[seat][i]>63)return 0;
        for(int i=0;i<256*240;i++)if(n.image[seat][i]>31)return 0;
    }
    return 1;
}
static int set(const uint8_t *in,int len){if(!valid(in,len))return 0;if(len)memcpy(&M,in,len);else mm6_menu_power();return 1;}
void mm6_menu_register(void){
    static const struct{const char *name;uint16_t address;} sites[]={
        {"open",0x9366},{"close",0x958E},{"form-loop",0x9391},{"weapon-loop",0x9465},{"tank-loop",0x950F},
        {"confirm",0x958A},{"form-confirm",0x965F},{"heal",0x953D},{"personal-end",0x95FD},{"wait",0xC5F6},{"wait-input",0xC62B}};
    for(unsigned i=0;i<sizeof sites/sizeof *sites;i++){static char ids[11][48];snprintf(ids[i],sizeof ids[i],"mm6.menu.%s",sites[i].name);nes_mod_register_function_entry_plugin(ids[i],sites[i].address,boundary);nes_mod_set_function_hook_enabled(ids[i],1);}
    nes_mod_register_savestate_hook("mm6.menu",get,set);nes_mod_register_savestate_validator("mm6.menu",valid);
}
