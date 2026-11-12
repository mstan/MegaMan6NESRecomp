#pragma once
#include <stdint.h>
void mm6_hud_register(void);
void mm6_hud_power(void);
void mm6_hud_capture(void);
void mm6_hud_latch(void);
int mm6_hud_native_part(const uint8_t *part);
void mm6_hud_draw(uint32_t *out,int width,int height,int own);
