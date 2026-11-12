/* Native D284/D29B terrain, including the shared metatile replacement bitmap. */
#include "mm6_world.h"
#include "mm6_proto.h"
#include "cyc_mod.h"
#include "cyc_render.h"
#include "cyc_ring.h"
#include <stdio.h>
#define R(a) cyc_mod_peek((uint16_t)(a))
static FILE *s_audit;
static int s_reported;
static int stage_byte(int address){return mm6_controls_prg(address<0xA000?0x3A:0x3B,address);}
int mm6_world_bounds(int *left,int *right){
    if(!left || !right || R(0x51)>=16 || R(0x58) || R(0x59))return 0;
    int pages=R(0x74)|(R(0x75)<<8),xs=R(0x78)|(R(0x79)<<8);
    int ys=R(0x7C)|(R(0x7D)<<8),types=R(0x7E)|(R(0x7F)<<8),count=xs-pages;
    if(pages<0x8000 || types+count>0xC000 || count<1 || count>128 || ys-xs!=count || types-ys!=count)return 0;
    int start=-1,page=R(0x57);
    for(int i=0;i<count;i++){
        int p=stage_byte(pages+i);if(p>=128)break;
        if(stage_byte(types+i)!=128)continue;
        int id=stage_byte(ys+i);if(id>=128)return 0;
        int ptr=mm6_controls_prg(0x39,0x82EB+id)|(mm6_controls_prg(0x39,0x836B+id)<<8);
        if(ptr<0x8000 || ptr>=0xA000){start=-1;continue;}
        int flags=mm6_controls_prg(0x39,ptr);if(flags>=128)flags=mm6_controls_prg(0x39,ptr+2);
        if(flags==1)start=p;
        else if(flags==2){
            if(start>=0 && page>=start && page<=p){*left=start*256;*right=(p+1)*256;return 1;}
            start=-1;
        }else{
            start=-1;if(p==page){*left=p*256;*right=(p+1)*256;return 1;}
        }
    }
    return 0;
}
int mm6_world_tile(int x,int y,Mm6WorldTile *out){
    int stage=R(0x51),art=R(0x6E),map=R(0x6F),base=R(0x52)|(R(0x53)<<8);
    if(!out || stage>=16 || art>=64 || map>=64 || x<0 || x>=8192 || y<0 || y>255 || base<0xA000 || base+0x7FF>=0xC000)return 0;
    int cell=base+((y>>5)<<8)+(x>>5);
    int block=mm6_controls_prg(map,cell),quadrant=((x>>4)&1)|((y>>3)&2);
    int meta=mm6_controls_prg(art,(R(0x67)<<8)+(R(0x66)&252)+block*4+quadrant);
    int replaced=R(0x340+(meta>>3))&mm6_controls_prg(0x3E,0xDFB1+(meta&7));
    /* D1FB/D31E temporarily advance63 while emitting a row/column. Its
     * stage-table origin is stable even if an NMI stops inside that routine. */
    int tiles=mm6_controls_prg(0x3E,0xDF61+stage);
    int plane=((x>>3)&1)|((y>>2)&2),tile_base=(replaced?0x9B:tiles)+plane;
    int flags=mm6_controls_prg(art,(replaced?0x9F00:(tiles+4)*256)+meta);
    out->tile=(uint8_t)mm6_controls_prg(art,tile_base*256+R(0x62)+meta);
    out->palette=(uint8_t)(flags&3);out->metatile=(uint8_t)meta;out->flags=(uint8_t)flags;return 1;
}
int mm6_world_background(uint32_t *out,int width,const uint32_t *native,uint8_t *opaque,int *origin){
    int lo,hi;if(!out || !native || !opaque || !origin || width<=256 || !mm6_world_bounds(&lo,&hi))return 0;
    int camera=(R(0x57)<<8)|R(0x56),left=camera-(width-256)/2;
    if(hi-lo<width)left=lo-(width-(hi-lo))/2;
    else{if(left<lo)left=lo;if(left+width>hi)left=hi-width;}
    *origin=camera-left;const uint8_t *native_opaque=cyc_frame_bg_opaque();
    for(int y=0;y<240;y++){
        int delta=(cyc_render_line_scroll_x(y)-(camera&511)+256)&511,actual=camera+delta-256;
        int sy=cyc_render_line_scroll_y(y)%240;
        for(int x=0;x<width;x++){
            int nx=x-*origin,at=y*width+x;
            /* The console's left-eight mask belongs at the picture edge;
             * retaining it inside a wider world would create a black seam. */
            int internal_clip=nx>=0 && nx<8 && *origin>0 && !(cyc_render_line_mask(y)&2);
            if(nx>=0 && nx<256 && !internal_clip){out[at]=native[y*256+nx];opaque[at]=native_opaque[y*256+nx];continue;}
            int wx=actual+nx,value=0;Mm6WorldTile t;uint32_t color=0xFF000000u;
            if(wx>=lo && wx<hi && mm6_world_tile(wx,sy,&t)){
                if(cyc_render_line_mask(y)&8){
                    int a=cyc_render_line_bg_table(y)+t.tile*16+(sy&7),bit=7-(wx&7);
                    value=((cyc_render_chr(a)>>bit)&1)|(((cyc_render_chr(a+8)>>bit)&1)<<1);
                }
                color=cyc_render_color(value?t.palette*4+value:0);
            }
            out[at]=color;opaque[at]=(uint8_t)(value!=0);
        }
    }
    return 1;
}
int mm6_world_audit_file(const char *path){
    if(s_audit)fclose(s_audit);s_audit=fopen(path,"w");s_reported=0;
    if(s_audit)fputs("frame,stage,camera,compared,mismatched,native_edge_pixels\n",s_audit);return s_audit!=NULL;
}
void mm6_world_audit(const uint32_t *background){
    if(!s_audit || !background || R(0x3A0)!=4 || (R(0x3B7)&2) || R(0x51)>=16 || R(0x58) || R(0x59))return;
    int camera=(R(0x57)<<8)|R(0x56),compared=0,mismatch=0,edges=0;
    for(int y=0;y<240;y++){
        int delta=(cyc_render_line_scroll_x(y)-(camera&511)+256)&511,actual=camera+delta-256;
        int sy=cyc_render_line_scroll_y(y)%240;
        for(int x=0;x<256;x++){
            int wx=actual+x;Mm6WorldTile t;if(!mm6_world_tile(wx,sy,&t))continue;
            int value=0,mask=cyc_render_line_mask(y);
            if((mask&8) && (x>=8 || (mask&2))){
                int a=cyc_render_line_bg_table(y)+t.tile*16+(sy&7),bit=7-(wx&7);
                value=((cyc_render_chr(a)>>bit)&1)|(((cyc_render_chr(a+8)>>bit)&1)<<1);
            }
            uint32_t color=cyc_render_color(value?t.palette*4+value:0);compared++;
            if(color!=background[y*256+x]){
                int nt=0x2000|(((wx>>8)&1)<<10),tile=cyc_render_nametable((uint16_t)(nt+(sy/8)*32+((wx&255)/8)));
                int attr=cyc_render_nametable((uint16_t)(nt+0x3C0+(sy/32)*8+((wx&255)/32))),pal=(attr>>(((sy&16)?4:0)+((wx&16)?2:0)))&3;
                int a=cyc_render_line_bg_table(y)+tile*16+(sy&7),bit=7-(wx&7);
                int live=((cyc_render_chr(a)>>bit)&1)|(((cyc_render_chr(a+8)>>bit)&1)<<1);
                /* Retain raw differences. Count a native edge discrepancy
                 * only when live CIRAM independently reproduces its pixel. */
                if((x<16 || x>=240) && (tile!=t.tile || pal!=t.palette) && cyc_render_color(live?pal*4+live:0)==background[y*256+x])edges++;
                if(!s_reported && mismatch<12)fprintf(stderr,"terrain mismatch x=%d y=%d wx=%d sy=%d tile=%02x pal=%u live_tile=%02x live_pal=%u decoded=%08x native=%08x\n",x,y,wx,sy,t.tile,t.palette,tile,pal,color,background[y*256+x]);
                mismatch++;
            }
        }
    }
    if(mismatch)s_reported=1;fprintf(s_audit,"%u,%u,%d,%d,%d,%d\n",cyc_ring_frame,R(0x51),camera,compared,mismatch,edges);
}
