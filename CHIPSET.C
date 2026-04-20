#include "chipset.h"

#include "pci.h"
#include "vgacon.h"
#include "util.h"

#define __LIB866D_TAG__ "CHIPSET"
#include "debug.h"

#define retPrintErrorIf(condition, message, value) if (condition) { vgacon_printError(message "\n", value); return false; }

/* Function pointer to tweaking action for each chipset */
typedef bool (*chipsetTweakHandler)(const chipset_GfxTweakConfig*, pci_Device);

typedef struct {
    u16                 vendor;
    u16                 device;
    const char         *name;
    chipsetTweakHandler handler;
} chipset_KnownChipset;


/*  ALI ALADDIN IV/V:
    The lower 3 bits of the "PCI Programmable Frame Buffer Memory Region" register
    maps to the size of the framebuffer.
    Officially this ends at 100 (-> 16MB) but I suspect
    that it actually supports bigger ones. */
static u16 aliGetFbSizeRegValue(u32 sizeKB) {
    u32 sizeMB = sizeKB / 1024UL;
    u16 ret = 0;

    if (sizeMB > 16) {
        vgacon_printWarning("Frame Buffer size > 16MB not officially supported by chipset!\n");
    } else if (sizeMB > 128) {
        vgacon_printWarning("Frame Buffer (%lu MB) too big! Clamping to 128MB\n", sizeMB);
        return 0x7;
    }

    while ((1 << ret) < (u16) sizeMB) {
        ret++;
    }

    return ret;
}


/*  ALI ALADDIN III / IV: Write registers to enable Frame Buffer cycles for given parameters */
static bool aliWriteAladdin34Regs(u32 offset, u32 sizeKB, bool vgaFb, pci_Device pciDev) {
    struct {                /* Bitfield for register 54-55, PCI Programmable Frame Buffer Memory Region */
        u16 fbSize          : 3;
        u16 __rsvd          : 1;
        u16 a31_20          : 12;
    } fbMemReg;

    struct {                /* Bitfield for register 56, CPU to PCI Write Buffer Option */
        u8 fbEnable         : 1;
        u8 vgaFbEnable      : 1;
        u8 pciWriteBurst    : 1;
        u8 pciFastBackToBack: 1;
        u8 fbByteMerge      : 1;
        u8 fbWordMerge      : 1;
        u8 fbLinearMerge    : 1;
        u8 allPCIMemory     : 1;
    } cpuPciWriteBufferReg;

    fbMemReg.a31_20 = (u16) (offset >> 20UL);
    fbMemReg.fbSize = aliGetFbSizeRegValue(sizeKB);
    fbMemReg.__rsvd = 0;

    cpuPciWriteBufferReg.fbEnable           = true;
    cpuPciWriteBufferReg.vgaFbEnable        = vgaFb;
    cpuPciWriteBufferReg.pciWriteBurst      = true;
    cpuPciWriteBufferReg.pciFastBackToBack  = true;
    cpuPciWriteBufferReg.fbByteMerge        = true;
    cpuPciWriteBufferReg.fbWordMerge        = true;
    cpuPciWriteBufferReg.fbLinearMerge      = false;
    cpuPciWriteBufferReg.allPCIMemory       = false;

    pci_writeBytes(pciDev, &fbMemReg, 0x54, 2);
    pci_writeBytes(pciDev, &cpuPciWriteBufferReg, 0x56, 1);
}

/*  ALI ALADDIN V: Write registers to enable Frame Buffer cycles for given parameters */
static bool aliWriteAladdin5Regs(u32 offset, u32 sizeKB, bool vgaFb, pci_Device pciDev) {
    struct {                /* Bitfield for register 84/85, PCI Programmable Frame Buffer Memory Region */
        u16 fbSize          : 3;
        u16 allPCIMemory    : 1;
        u16 a31_20          : 12;
    } fbMemReg;

    struct {                /* Bitfield for register 86, CPU to PCI Write Buffer Option */
        u8 fbEnable         : 1;
        u8 vgaFbEnable      : 1;
        u8 fbPciWriteBurst  : 1;
        u8 fbLinearMerge    : 1;
        u8 __rsvd           : 4;
    } cpuPciWriteBufferReg;

    pci_Device  secondaryDev;
    bool        secondaryDevFound = pci_findDevByID(0x10b9, 0x5243, &secondaryDev);

    L866_ASSERT(secondaryDevFound);

    fbMemReg.a31_20                         = (u16) (offset >> 20UL);
    fbMemReg.fbSize                         = aliGetFbSizeRegValue(sizeKB);
    fbMemReg.allPCIMemory                   = false;

    cpuPciWriteBufferReg.fbEnable           = true;
    cpuPciWriteBufferReg.vgaFbEnable        = vgaFb;
    cpuPciWriteBufferReg.fbPciWriteBurst    = true;
    cpuPciWriteBufferReg.fbLinearMerge      = false;
    cpuPciWriteBufferReg.__rsvd             = 0;

    pci_writeBytes(pciDev, &fbMemReg, 0x84, 2);
    pci_writeBytes(pciDev, &cpuPciWriteBufferReg, 0x86, 1);

    /* PCI Bridge done, do the same for the AGP bridge */
    pci_writeBytes(secondaryDev, &fbMemReg, 0x84, 2);
    pci_writeBytes(secondaryDev, &cpuPciWriteBufferReg, 0x86, 1);
}

static bool aliAladdinTweaks(const chipset_GfxTweakConfig *cfg, pci_Device pciDev, bool isAladdin5) {
    if (cfg->setLfb) {
        /* We found a suitable FB region to set up for the chipset */
        if (isAladdin5) aliWriteAladdin5Regs (cfg->offset, cfg->sizeKB, cfg->setVgaFb, pciDev);
        else            aliWriteAladdin34Regs(cfg->offset, cfg->sizeKB, cfg->setVgaFb, pciDev);
    } else if (cfg->setVgaFb) {
        vgacon_printWarning("This chipset can't do VGA burst cycles without another linear FB region!\n");
    }

    return true;
}

static bool chipset_aliAladdin34(const chipset_GfxTweakConfig *cfg, pci_Device pciDev) {
    return aliAladdinTweaks(cfg, pciDev, false);
}

static bool chipset_aliAladdin5(const chipset_GfxTweakConfig *cfg, pci_Device pciDev) {
    return aliAladdinTweaks(cfg, pciDev, true);
}

/* Like Aladdin chipsets; power of 1 boundary.*/
static u16 sisGetFbSizeRegValue(u32 sizeKB) {
    u16 ret = 0xFFF;
    u16 i = 0;

    /*  0b1111111111111111 = 1MB 
        0b0000000000000000 = 4GB */
    while ((1 << i) < (u16) (sizeKB / 1024UL)) {
        ret <<= 1;
        ret &= 0xFFF;
        i++;
    }

    return ret;
}

static void sisWrite5591Regs(u32 offset, u32 sizeKB, pci_Device pciDev) {
    struct {                /* Bitfield for register 83, CPU-To-PCI Characteristics Register */
        u8 __dontcare1      : 4;
        u8 fastBackToBack   : 1;
        u8 __dontcare2      : 3;
    } cpuPciCharacteristicsReg;

    struct {                /* Bitfield for register 88-89, Frame Buffer Base Register */
        u16 __rsvd          : 4;
        u16 a31_20          : 12;
    } fbBaseReg;

    struct {                /* Bitfield for register 8A-8B, Frame Buffer Size Register */
        u16 __rsvd          : 4;
        u16 fbSizeMask      : 12;
    } fbSizeReg;

    /* PCI bridge parameters register must retain the irrelevant values */
    pci_readBytes(pciDev, &cpuPciCharacteristicsReg, 0x83, 1);

    cpuPciCharacteristicsReg.fastBackToBack = true;

    fbBaseReg.a31_20 = (u16) (offset >> 20UL);
    fbBaseReg.__rsvd = 0;

    fbSizeReg.fbSizeMask = sisGetFbSizeRegValue(sizeKB);
    fbSizeReg.__rsvd = 0;

    pci_writeBytes(pciDev, &fbBaseReg, 0x88, 2);
    pci_writeBytes(pciDev, &fbSizeReg, 0x8A, 2);
    pci_writeBytes(pciDev, &cpuPciCharacteristicsReg, 0x83, 1);
}


static void sisWrite530Regs(u32 offset, u32 sizeKB, pci_Device pciDev) {
    struct {                /* Bitfield for Prefetchable Memory Base / Limit registers */
        u16 __rsvd          : 4;
        u16 a31_20          : 12;
    } pmReg[2];

    u32 limit = offset + (sizeKB * 1024UL);

    pmReg[0].a31_20 = (u16) (offset >> 20UL);
    pmReg[0].__rsvd = 0;
    pmReg[1].a31_20 = (u16) (limit >> 20UL);
    pmReg[1].__rsvd = 0;

    pci_writeBytes(pciDev, &pmReg[0], 0x22, 2);
    pci_writeBytes(pciDev, &pmReg[1], 0x24, 2);
}


/*  SiS 557, 5581, 5591, 5597 chipset tweaks
    5591 calls it PCI Fast back to back frame buffer
    5597 and 5581 generically call it "fast back to back area"
    registers are the same though. */
static bool chipset_sis559x(const chipset_GfxTweakConfig *cfg, pci_Device pciDev) {
    if (cfg->setVgaFb) {
        vgacon_printWarning("Chipset does not support VGA region acceleration.\n");
    }

    if (cfg->setLfb) {
        sisWrite5591Regs(cfg->offset, cfg->sizeKB, pciDev);
    }

    return true;
}

/*  SiS 530/540 chipset tweaks
    Not sure if this works at all. The datasheet is confusing to read. */
static bool chipset_sis5x0(const chipset_GfxTweakConfig *cfg, pci_Device pciDev) {
    if (cfg->setVgaFb) {
        vgacon_printWarning("Chipset does not support VGA region acceleration.\n");
    }

    if (cfg->setLfb) {
        sisWrite530Regs(cfg->offset, cfg->sizeKB, pciDev);
    }

    return true;
}

static const chipset_KnownChipset chipset_knownChipsets[] = {
    { 0x10B9, 0x1521, "ALI Aladdin III",    chipset_aliAladdin34 },
    { 0x10B9, 0x1531, "ALI Aladdin IV",     chipset_aliAladdin34 },
    { 0x10B9, 0x1541, "ALI Aladdin V",      chipset_aliAladdin5 },
    { 0x1039, 0x5571, "SiS 5571",           chipset_sis559x },
    { 0x1039, 0x5581, "SiS 5581/5582",      chipset_sis559x },
    { 0x1039, 0x5591, "SiS 5591/5592",      chipset_sis559x },
    { 0x1039, 0x5597, "SiS 5597/5598",      chipset_sis559x },
    { 0x1039, 0x0001, "SiS 530/540",        chipset_sis5x0 },
};

bool chipset_doFramebufferTweaks(const chipset_GfxTweakConfig *cfg) {
    size_t  i;
    size_t  chipsetCount    = ARRAY_SIZE(chipset_knownChipsets);
    bool    found           = false;

    L866_NULLCHECK(cfg);

    if (!cfg->setLfb && !cfg->setVgaFb) {
        /* Leave everything untouched if not wanted */
        vgacon_printWarning("Framebuffer setup not requested, nothing to set up in the chipset.\n");
        return true;
    }

    if (!pci_test()) {
        vgacon_printWarning("PCI Bus inaccessible, skipping chipset tweaks\n");
        return true;
    }

    for (i = 0; i < chipsetCount; i++) {
        const chipset_KnownChipset  *cs      = &chipset_knownChipsets[i];
        pci_Device                   pciDev;

        if (pci_findDevByID(chipset_knownChipsets[i].vendor, chipset_knownChipsets[i].device, &pciDev)) {
            L866_NULLCHECK(cs->handler);
            vgacon_print("Found supported chipset '%s', applying tweaks...\n", cs->name);
            retPrintErrorIf(false == cs->handler(cfg, pciDev), "Error applying tweaks for '%s'!", cs->name);
            vgacon_printOK("Chipset register setup successful.\n");
            if (cfg->setLfb) {
                vgacon_printOK("Frame buffer @ 0x%08lx, size %lu KB\n", cfg->offset, cfg->sizeKB);
            }

            return true;
        }
    }

    vgacon_printWarning("No supported chipset found; skipping chipset tweaks\n");
    return true;
}