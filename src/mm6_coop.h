#pragma once
#include <stdint.h>
void mm6_coop_register(void);
void mm6_coop_power(void);
void mm6_coop_input(uint8_t buttons[2]);
void mm6_coop_end(void);
void mm6_coop_arrival(void);
void mm6_coop_set(int enabled,int roster);
void mm6_coop_build(void);
void mm6_coop_latch(void);
int mm6_coop_has_frame(void);
void mm6_coop_draw(uint32_t *out,int width,int origin,const uint8_t *opaque,uint8_t *claimed);
int mm6_coop_enabled(void);
int mm6_coop_ready(void);
int mm6_coop_seat(void);
int mm6_coop_character(void);
int mm6_coop_read(int seat,int address);
void mm6_coop_colors(int seat,const uint8_t colors[8]);
int mm6_coop_json(char *out,int cap);
