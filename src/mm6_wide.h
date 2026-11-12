#pragma once
#include <stddef.h>
void mm6_wide_register(void);
void mm6_wide_set(int enabled);
void mm6_wide_power(void);
int mm6_wide_active(void);
void mm6_wide_views_changed(void);
int mm6_wide_json(char *out,size_t cap);
