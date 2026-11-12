#pragma once
#include <stdint.h>
typedef struct {uint8_t tile,palette,metatile,flags;} Mm6WorldTile;
int mm6_world_tile(int x,int y,Mm6WorldTile *out);
int mm6_world_bounds(int *left,int *right);
int mm6_world_background(uint32_t *out,int width,const uint32_t *native,uint8_t *opaque,int *origin);
int mm6_world_audit_file(const char *path);
void mm6_world_audit(const uint32_t *background);
