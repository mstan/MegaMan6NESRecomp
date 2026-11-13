#pragma once
#include <stdint.h>
typedef struct {int coop,independent,roster,proto,mode,start,weapons,wide,flicker,lines,wait;} Mm6Settings;
int mm6_net_locked(void);
int mm6_net_view_allowed(int mode);
int mm6_net_simulation_width(int local_width);
void mm6_net_note_option(const char *name);
Mm6Settings mm6_host_settings(void);
int mm6_host_apply_settings(Mm6Settings settings,int local_seat);
int mm6_host_view_preference(void);
void mm6_host_view_set(int mode);
int mm6_proto_source_hash(uint8_t out[32]);
