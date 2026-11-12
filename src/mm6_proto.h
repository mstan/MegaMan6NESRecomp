#pragma once
#include "cyc_protoman_atlas.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    int16_t x,y;
    uint8_t pose,right,flipped,behind,active,weapon,charge,form;
    uint8_t palette[8];
} Mm6ProtoBody;
bool mm6_proto_set(int enabled,int mode,int start);
void mm6_proto_register(void);
void mm6_proto_power(void);
void mm6_proto_end(void);
int mm6_proto_dispatch(void);
int mm6_proto_enabled(void);
int mm6_proto_selected(void);
int mm6_proto_swapping(void);
int mm6_proto_json(char *out,int capacity);
void mm6_proto_body(Mm6ProtoBody *body);
void mm6_proto_animation(int animation);
void mm6_proto_clone_animation(int from,int to);
const CycProtoAtlas *mm6_proto_atlas(void);
void mm6_view_enable(int enabled);
void mm6_view_power(void);
void mm6_view_register(void);
void mm6_view_coop(int enabled);
void mm6_view_draw_character(uint32_t *out,int width,int origin,const Mm6ProtoBody *body);
int mm6_controls_hurt(void);
void mm6_controls_refresh(void);
int mm6_controls_restore_palette(void);
int mm6_controls_restore_graphics(void);
int mm6_controls_read_graphics(uint8_t out[2048]);
