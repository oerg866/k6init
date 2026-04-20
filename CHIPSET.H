#ifndef CHIPSET_H
#define CHIPSET_H

#include "types.h"

typedef struct {
    bool    setLfb;
    u32     offset;
    u32     sizeKB;
    bool    setVgaFb;
} chipset_GfxTweakConfig;

bool chipset_doFramebufferTweaks(const chipset_GfxTweakConfig *cfg);

#endif

