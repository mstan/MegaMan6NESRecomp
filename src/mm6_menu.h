#pragma once
#include <stdint.h>
void mm6_menu_register(void);
void mm6_menu_power(void);
void mm6_menu_input(uint8_t buttons[2]);
void mm6_menu_frame(void);
int mm6_menu_draw(uint32_t *out,int width,int height,const uint32_t *native);
