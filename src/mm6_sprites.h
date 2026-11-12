#pragma once
#include "mm6_proto.h"
#include <stdint.h>
void mm6_sprites_register(void);
void mm6_sprites_power(void);
void mm6_sprites_active(int active);
int mm6_sprites_capture(void);
void mm6_sprites_part(void);
void mm6_sprites_latch(void);
int mm6_sprites_ready(void);
void mm6_sprites_draw(uint32_t *out,int width,int origin,const Mm6ProtoBody *body,int replace,const uint8_t *opaque,uint8_t *claimed,int limited,const uint8_t *hud_lines,int hud_parts);
