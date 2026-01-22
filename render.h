#ifndef RENDER_H
#define RENDER_H

#include "cards.h"

void render_index(char *out, size_t out_cap, const CardList *cards,
                  const char *sort_key, const char *order,
                  const char *tag, const char *rarity_min,
                  const char *imgfx);

#endif
