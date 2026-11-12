/* The shared session barrier owns transport, CPU capabilities and rollback. */
#include "mm6_net.h"
#include "mm6_coop.h"
#include "mm6_proto.h"
#include "cyc_core.h"
#include "cyc_video.h"
#include "cyc_session.h"
#include "cyc_host_extras.h"
#include "mod_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int s_sealed,s_applying,s_width=256;
static unsigned s_cli;
enum {CLI_COOP=1u<<0,CLI_VIEW=1u<<1,CLI_ROSTER=1u<<2,CLI_PROTO=1u<<3,
      CLI_MODE=1u<<4,CLI_START=1u<<5,CLI_WEAPONS=1u<<6,CLI_WIDE=1u<<7,
      CLI_FLICKER=1u<<8,CLI_LINES=1u<<9,CLI_QA=1u<<10};
int mm6_net_locked(void){return s_sealed && !s_applying;}
int mm6_net_simulation_width(int local){return s_sealed?s_width:local;}
int mm6_net_view_allowed(int mode){return mode>=0 && mode<=3 && (!mm6_net_locked() || (mm6_coop_independent() && mode>0));}
void mm6_net_note_option(const char *name){
    if(s_applying || s_sealed)return;
    if(!strcmp(name,"--coop"))s_cli|=CLI_COOP;
    if(!strcmp(name,"--coop-view"))s_cli|=CLI_VIEW;
    if(!strcmp(name,"--coop-roster"))s_cli|=CLI_ROSTER;
    if(!strcmp(name,"--protoman"))s_cli|=CLI_PROTO;
    if(!strcmp(name,"--proto-mode"))s_cli|=CLI_MODE;
    if(!strcmp(name,"--proto-start"))s_cli|=CLI_START;
    if(!strcmp(name,"--weapon-cycle"))s_cli|=CLI_WEAPONS;
    if(!strcmp(name,"--wide"))s_cli|=CLI_WIDE;
    if(!strcmp(name,"--no-flicker"))s_cli|=CLI_FLICKER;
    if(!strcmp(name,"--slowdown-reduction"))s_cli|=CLI_LINES;
    if(!strcmp(name,"--mm6-coop-poke"))s_cli|=CLI_QA;
}
#ifdef NESRECOMP_NET
#include "cyc_net.h"
#include "nes_netplay.h"
#include "nes_netplay_identity.h"
#ifdef CYC_WITH_RECOMP_UI
#include "recomp_launcher.h"
#endif
#ifndef MM6_NETPLAY_QUALIFIED
#define MM6_NETPLAY_QUALIFIED 0
#endif
static Mm6Settings s_offline,s_offline_offer;
static int s_saved,s_offline_view,s_offline_video,s_offline_width;
static int s_offline_draw_w,s_offline_draw_h;
static unsigned s_offline_cli;
static int selected(const char *package,const char *feature){return nes_mod_feature_selected(package,feature)!=0;}
static int option_is(const char *package,const char *feature,const char *key,const char *want,const char *fallback){
    char value[32];if(!nes_mod_option_value(package,feature,key,value,sizeof value))snprintf(value,sizeof value,"%s",fallback);return !strcmp(value,want);
}
static Mm6Settings offer(void){
    Mm6Settings c=mm6_host_settings();if(!cyc_session_mods_enabled()){if(c.coop)c.proto=1;return c;}
    const char *proto="mega-man-6-usa.character.protoman",*coop="mega-man-6-usa.coop";
    if(!(s_cli&CLI_COOP))c.coop=selected(coop,"coop");
    c.independent=(s_cli&CLI_VIEW)?mm6_host_view_preference()!=0:!option_is(coop,"coop","coop-view","shared","shared");
    if(!(s_cli&CLI_ROSTER))c.roster=option_is(coop,"coop","player-one","protoman","megaman");
    if(!(s_cli&CLI_PROTO))c.proto=selected(proto,"protoman");
    if(!(s_cli&CLI_MODE))c.mode=option_is(proto,"protoman","mode","protoman","protoman");
    if(!(s_cli&CLI_START))c.start=option_is(proto,"protoman","start","protoman","protoman");
    if(!(s_cli&CLI_WEAPONS))c.weapons=selected("mega-man-6-usa.enhancement.weapons","weapons");
    if(!(s_cli&CLI_WIDE))c.wide=selected("mega-man-6-usa.enhancement.widescreen","widescreen");
    if(!(s_cli&CLI_FLICKER))c.flicker=selected("mega-man-6-usa.enhancement.noflicker","noflicker");
    if(!(s_cli&CLI_LINES))c.lines=selected("mega-man-6-usa.enhancement.slowdown","slowdown")?262:0;
    if(c.coop)c.proto=1;return c;
}
static int catalog_supported(void){
    if(!cyc_session_mods_enabled())return 1;
#ifdef CYC_WITH_RECOMP_UI
    const RecompLauncherCModProvider *p=nes_mod_runtime_launcher_provider_c();
    if(!p || !p->feature_count || !p->feature_get)return 0;
    static const char *known[][2]={{"mega-man-6-usa.coop","coop"},{"mega-man-6-usa.character.protoman","protoman"},
        {"mega-man-6-usa.enhancement.weapons","weapons"},{"mega-man-6-usa.enhancement.widescreen","widescreen"},
        {"mega-man-6-usa.enhancement.noflicker","noflicker"},{"mega-man-6-usa.enhancement.slowdown","slowdown"},
        {"nesrecomp.developer.replay","replay-capture"}};
    for(int i=0;i<p->feature_count(p->ctx);i++){
        RecompLauncherCModFeature f;if(!p->feature_get(p->ctx,i,&f))return 0;if(!f.enabled)continue;
        int found=0;for(unsigned j=0;j<sizeof known/sizeof *known;j++)if(!strcmp(f.package_id,known[j][0]) && !strcmp(f.id,known[j][1]))found=1;
        if(!found){fprintf(stderr,"[netplay] unqualified feature: %s/%s\n",f.package_id,f.id);return 0;}
    }return 1;
#else
    return 0;
#endif
}
static int describe(Mm6Settings c,char *out,int cap){
    /* Native charge, slide and both adapters are always retained (mask7). */
    return snprintf(out,cap,"1:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:7",c.coop,c.independent,c.roster,c.proto,c.mode,c.start,c.weapons,c.wide,c.flicker,c.lines);
}
static int settings_get(char *out,int cap){return describe(mm6_host_settings(),out,cap);}
static int settings_offer(char *out,int cap){return describe(offer(),out,cap);}
static void preserve_offline(void){
    if(s_saved)return;
    s_offline=mm6_host_settings();s_offline_offer=offer();s_offline_view=mm6_host_view_preference();
    s_offline_video=cyc_video_mode();s_offline_width=cyc_video_width();s_offline_cli=s_cli;s_saved=1;
    cyc_video_drawable_size(&s_offline_draw_w,&s_offline_draw_h);
}
static int bit(int n){return n==0 || n==1;}
static int settings_apply(const char *value){
    int v[12];const char *cursor=value;
    if((s_cli&CLI_QA) || !catalog_supported())return 0;
    for(int i=0;i<12;i++){
        char *end;long n=strtol(cursor,&end,10);
        if(end==cursor || n<0 || n>262 || (i<11?*end!=':':*end))return 0;
        v[i]=(int)n;cursor=end+1;
    }
    Mm6Settings c={v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10]};
    if(v[0]!=1 || v[11]!=7 || c.coop!=1 || c.proto!=1 || !bit(c.independent) || !bit(c.roster) || !bit(c.mode) || !bit(c.start) ||
        !bit(c.weapons) || !bit(c.wide) || !bit(c.flicker) || (c.lines!=0 && c.lines!=262) || (c.independent && s_width!=256))return 0;
    preserve_offline();s_applying=1;
    int ok=mm6_host_apply_settings(c,nes_netplay_local_slot());
    /* Online catalog activation is deferred; its local view preference must
     * still survive adoption of the host's common independent camera class. */
    if(ok && c.independent && !(s_cli&CLI_VIEW) && cyc_session_mods_enabled()){
        const char *coop="mega-man-6-usa.coop";
        if(option_is(coop,"coop","coop-view","split","shared"))mm6_host_view_set(1);
        else if(option_is(coop,"coop","coop-view","p1","shared"))mm6_host_view_set(2);
        else if(option_is(coop,"coop","coop-view","p2","shared"))mm6_host_view_set(3);
    }
    if(ok){const CycHostExtras *x=cyc_host_extras();if(x && x->power_on)x->power_on(x->ctx);}
    s_applying=0;if(!ok)return 0;
    s_sealed=1;nes_netplay_require_capabilities(1);nes_netplay_require_player_count(2);
    if(!c.independent)cyc_video_request_width(s_width);
    if(!getenv("NES_NET_INPUT_PLAYER")){
        char binding[16]="seat";
        if(cyc_session_mods_enabled())nes_mod_option_value("mega-man-6-usa.coop","coop","online-input",binding,sizeof binding);
        nes_netplay_set_input_player(!strcmp(binding,"device1")?0:!strcmp(binding,"device2")?1:nes_netplay_local_slot());
    }
#if MM6_NETPLAY_QUALIFIED
    nes_mod_set_local_only("MM6 native co-op prototype (unqualified netplay)",0);
    nes_mod_set_local_only("MM6 Proto Man (unqualified netplay)",0);
    nes_mod_set_local_only("MM6 horizontal terrain prototype (unqualified netplay)",0);
#else
    nes_mod_set_local_only("MM6 unqualified simulation profile",1);
#endif
    return cyc_extra_scanlines()==(unsigned)c.lines;
}
static void settings_restore(void){
    s_sealed=0;
    if(s_saved){
        s_applying=1;int restored=mm6_host_apply_settings(s_offline,0);mm6_host_view_set(s_offline_view);s_applying=0;
        s_cli=s_offline_cli;
        /* Session exit is a state boundary: restore the exact prior logical
         * canvas and selected aspect rather than retaining online Stock. */
        restored=cyc_video_restore(s_offline_video,s_offline_width,s_offline_draw_w,s_offline_draw_h) && restored;
        char expected[192],actual[192],original[192],next[192];
        describe(s_offline,expected,sizeof expected);settings_get(actual,sizeof actual);
        describe(s_offline_offer,original,sizeof original);settings_offer(next,sizeof next);
        fprintf(stderr,"MM6_NET_RESTORE applied=%d expected=%s actual=%s expected_offer=%s next_offer=%s expected_view=%d actual_view=%d expected_lines=%d actual_lines=%u expected_video=%d actual_video=%d expected_width=%d actual_width=%d cli=%u\n",
            restored,expected,actual,original,next,s_offline_view,mm6_host_view_preference(),s_offline.lines,cyc_extra_scanlines(),s_offline_video,cyc_video_mode(),s_offline_width,cyc_video_width(),s_cli);
        s_saved=0;
    }
    nes_netplay_require_capabilities(0);nes_netplay_require_player_count(0);nes_netplay_set_input_player(-1);
    nes_mod_set_local_only("MM6 unqualified simulation profile",0);
}
static int width_get(char *out,int cap){return snprintf(out,cap,"%d",s_sealed?s_width:(mm6_coop_independent()?256:cyc_video_width()));}
static int width_offer(char *out,int cap){Mm6Settings c=offer();int w=c.independent || !c.wide?256:cyc_video_width();if(c.wide && !c.independent && w<=256)w=426;return snprintf(out,cap,"%d",w);}
static int width_apply(const char *value){char *end;long n=strtol(value,&end,10);if(end==value || *end || n<256 || n>CYC_VIDEO_MAX_WIDTH || (n&1))return 0;preserve_offline();s_width=(int)n;return 1;}
static int timing_get(char *out,int cap){return snprintf(out,cap,"%d:%u",(int)cyc_region(),cyc_extra_scanlines());}
static int timing_offer(char *out,int cap){return snprintf(out,cap,"%d:%d",(int)cyc_region(),offer().lines);}
static int timing_apply(const char *value){int region;unsigned lines;char tail;if(sscanf(value,"%d:%u%c",&region,&lines,&tail)!=2 || region!=(int)cyc_region() || (lines!=0 && lines!=262))return 0;preserve_offline();return cyc_set_extra_scanlines(lines);}
static int sheet_get(char *out,int cap){uint8_t digest[32];if(cap<65 || !mm6_proto_source_hash(digest))return 0;for(int i=0;i<32;i++)snprintf(out+i*2,3,"%02x",digest[i]);return 64;}
static int sheet_apply(const char *value){char actual[65];return sheet_get(actual,sizeof actual)==64 && !strcmp(value,actual);}
NES_MOD_CONSTRUCTOR(register_mm6_net){
    nes_runner_register_session_keys();
    nes_netplay_session_register("timing",timing_get,timing_apply,settings_restore);nes_netplay_session_set_offer("timing",timing_offer);
    nes_netplay_session_register("vw",width_get,width_apply,settings_restore);nes_netplay_session_set_offer("vw",width_offer);
    nes_netplay_session_register("mm6",settings_get,settings_apply,settings_restore);nes_netplay_session_set_offer("mm6",settings_offer);
    nes_netplay_session_register("sheet",sheet_get,sheet_apply,NULL);
}
#endif
