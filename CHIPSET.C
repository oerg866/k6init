#include "k6init.h"

#include "pci.h"
#include "vgacon.h"
#include "util.h"

#define __LIB866D_TAG__ "CHIPSET"
#include "debug.h"

typedef bool (*chipsetTweakHandler)(const k6init_Parameters *, const k6init_SysInfo*, pci_Device);   /* Function pointer to action to execute if condition is true */

typedef struct {
    u16                 vendor;
    u16                 device;
    const char         *name;
    chipsetTweakHandler handler;
} chipset_KnownChipset;

/*  The lower 3 bits of the "PCI Programmable Frame Buffer Memory Region" register
    maps to the size of the framebuffer.
    Officially this ends at 100 (-> 16MB) but I suspect
    that it actually supports bigger ones.*/
static u16 aliVGetFbSizeRegValue(u32 sizeKB) {
    u32 sizeMB = sizeKB / 1024UL;
    u16 ret;
    if (sizeMB < 1) {
        return 0; /* Minimum value */
    } else if (sizeMB > 16) {
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

static bool chipset_aliAladdinV(const k6init_Parameters *params, const k6init_SysInfo *sysInfo, pci_Device pciDev) {
    struct {                /* Bitfield for register 0x84-85, PCI Programmable Frame Buffer Memory Region */
        u16 fbSize          : 3;
        u16 allPCIMemory    : 1;
        u16 a31_20          : 12;
    } fbMemReg;

    struct {                /* Bitfield for register 0x86, CPU to PCI Write Buffer Option */
        u8 fbEnable         : 1;
        u8 vgaFbEnable      : 1;
        u8 fbPciWriteBurst  : 1;
        u8 fbLinearMerge    : 1;
        u8 __rsvd           : 4;
    } cpuPciWriteBufferReg;

    size_t  i;
    u32     offset = 0;
    u32     sizeKB = 0;
    bool    setVGAFB = false;
    bool    found = false;

    pci_readBytes(pciDev, &fbMemReg, 0x84, 2);
    pci_readBytes(pciDev, &cpuPciWriteBufferReg, 0x86, 1);

    /* Set up PCI frame buffer cycles in the chipset IF:
        - MTRR setup is wished
        - One of the regions is write combined */
    if (!params->mtrr.setup) {
        /* Leave everything untouched if not wanted */
        vgacon_printWarning("MTRR setup not requested, nothing to set up in the chipset.\n");
        return true;
    }

    /*  If we end up activating this, we need PCI Write bursting. */
    cpuPciWriteBufferReg.fbPciWriteBurst = true;

    for (i = 0; i < params->mtrr.count; i++) {
        /* Skip this MTRR if it's not WC */
        if (!params->mtrr.toSet.configs[i].writeCombine) {
            continue;
        }

        offset = params->mtrr.toSet.configs[i].offset;
        sizeKB = params->mtrr.toSet.configs[i].sizeKB;

        /*  If WC for VGA region is active, mark it, since 
            the chipset supports setting it up in addition to another LFB region */
        if (offset == 0xA0000UL) {
            vgacon_print("Activating FB write cycles for VGA region...\n");
            cpuPciWriteBufferReg.vgaFbEnable = true;
            continue;
        }
        
        if (offset == 0UL || sizeKB == 0UL) {
            continue;
        }

        if (offset & 0xFFFFFUL != 0UL) {
            vgacon_printWarning("LFB offset 0x%08lx not aligned to 20 bits, ignoring\n", offset);
            continue;
        }

        found = true;
        break;
    }

    /* We found a suitable FB region to set up for the chipset */
    if (found) {
        vgacon_print("Setting chipset registers for FB region 0x%08lx...\n", offset);
        fbMemReg.allPCIMemory = false;
        fbMemReg.fbSize = aliVGetFbSizeRegValue(sizeKB);
        fbMemReg.a31_20 = (u16) (offset >> 20UL);
        cpuPciWriteBufferReg.fbEnable = 1;
    }

    /* Write back new registers to the chipset */
    pci_writeBytes(pciDev, &fbMemReg, 0x84, 2);
    pci_writeBytes(pciDev, &cpuPciWriteBufferReg, 0x86, 1);
    return true;
}

static const chipset_KnownChipset chipset_knownChipsets[] = {
    { 0x10B9, 0x1541, "ALI Aladdin V", chipset_aliAladdinV },
};

bool chipset_autoConfig(const k6init_Parameters *params, const k6init_SysInfo *sysInfo) {
    size_t i;
    size_t chipsetCount = ARRAY_SIZE(chipset_knownChipsets);

    L866_NULLCHECK(params);
    L866_NULLCHECK(sysInfo);

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
            retPrintErrorIf(false == cs->handler(params, sysInfo, pciDev), "Error applying tweaks for '%s'!", cs->name);
            return true;
        }
    }

    vgacon_print("No supported chipset found; skipping chipset tweaks\n");
    return true;
}