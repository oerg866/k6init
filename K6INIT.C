#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include "k6init.h"

#include "vesabios.h"
#include "vgacon.h"
#include "util.h"
#include "args.h"
#include "sys.h"
#include "pci.h"
#include "cpu_k6.h"

#define __LIB866D_TAG__ "K6INIT"
#include "debug.h"

static k6init_Parameters    s_params;
static k6init_SysInfo       s_sysInfo;
static char                 s_multiToParse[4] = {0,};
static u32                  s_MTRRCfgQueue[4];

static const char   k6init_versionString[] = "K6INIT Version 1.4a - (C) 2021-2026 Eric Voirin (oerg866)";

static bool k6init_areAllMTRRsUsed(void) {
    return s_params.mtrr.count >= 2;
}

static bool k6init_isKnownMTRRAddress(u32 address) {
    return s_params.mtrr.setup && (address == s_params.mtrr.toSet.configs[0].offset || address == s_params.mtrr.toSet.configs[1].offset);
}

static bool k6init_addMTRRToConfig(u32 offset, u32 sizeKB, bool writeCombine, bool uncacheable) {
    retPrintErrorIf(s_params.mtrr.clear == true,    "Cannot clear MTRRs and set them up at the same time!",     0);
    retPrintErrorIf(sizeKB > 0x400000UL,            "Requested MTRR size of %lu KB too big!",                   sizeKB);
    retPrintErrorIf((offset % 131072UL) != 0UL,     "MTRR offset 0x%lx isn't aligned on a 128KB boundary!",     offset);
    retPrintErrorIf(sizeKB < 128UL,                 "Requested MTRR size of %lu KB is too small (< 128KB)!",    sizeKB);

    if (k6init_isKnownMTRRAddress(offset)) {
        vgacon_printWarning("MTRR address 0x%lx already known, ignoring....\n", offset);
        return true;
    }

    retPrintErrorIf(k6init_areAllMTRRsUsed(),       "MTRR list is full, cannot add any more!",                  0);

    s_params.mtrr.toSet.configs[s_params.mtrr.count].offset         = offset;
    s_params.mtrr.toSet.configs[s_params.mtrr.count].sizeKB         = sizeKB;
    s_params.mtrr.toSet.configs[s_params.mtrr.count].writeCombine   = writeCombine;
    s_params.mtrr.toSet.configs[s_params.mtrr.count].uncacheable    = uncacheable;
    s_params.mtrr.toSet.configs[s_params.mtrr.count].isValid        = true;

    DBG("k6init_addMTRRToConfig: 0x%08lx | %lu KB | %s | %s\n", offset, sizeKB, writeCombine ? "WC" : "  ", uncacheable ? "UC" : "  ");

    s_params.mtrr.count++;
    return true;
}

static bool k6init_argAutoSetup(const void *arg) {
    UNUSED_ARG(arg);

    s_params.wAlloc.setup       = true;
    s_params.wAlloc.size        = s_sysInfo.memSize / 1024UL;
    s_params.wAlloc.hole        = s_sysInfo.memHole;

    s_params.wOrder.setup       = s_sysInfo.cpu.supportsEFER;
    s_params.wOrder.mode        = CPU_K6_WRITEORDER_ALL_EXCEPT_UC_WC;

    s_params.l1Cache.setup      = true;
    s_params.l1Cache.enable     = true;

    s_params.l2Cache.setup      = s_sysInfo.cpu.supportsL2;
    s_params.l2Cache.enable     = s_sysInfo.cpu.supportsL2;

    s_params.prefetch.setup     = s_sysInfo.cpu.supportsEFER;
    s_params.prefetch.enable    = true;

    s_params.mtrr.setup         = s_sysInfo.cpu.supportsCxtFeatures;
    s_params.mtrr.pci           = true;
    s_params.mtrr.lfb           = true;

    return true;
}

static bool k6init_argSkipCPUStuff(const void *arg) {
    UNUSED_ARG(arg);
    s_params.l1Cache.setup  = false;
    s_params.l2Cache.setup  = false;
    s_params.prefetch.setup = false;
    return true;
}

static bool k6init_argSkipWAWO(const void *arg) {
    UNUSED_ARG(arg);
    s_params.wAlloc.setup   = false;
    s_params.wOrder.setup   = false;
    return true;
}

static bool k6init_argClearMTRRs(const void *arg) {
    UNUSED_ARG(arg);
    memset(&s_params.mtrr.toSet, 0, sizeof(cpu_K6_MemoryTypeRangeRegs));
    s_params.mtrr.count = 2;
    return true;
}

static bool k6init_argAddMTRR(const void *arg) {
    u32    *toParse     = (u32 *)arg;
    bool    formatOK    = (toParse[2] <= 1 && toParse[3] <= 1); /* WC/UC flags must be valid bools */

    retPrintErrorIf(formatOK == false, "MTRR Config Argument Format error.", 0);
    return k6init_addMTRRToConfig(toParse[0], toParse[1], (bool) toParse[2], (bool) toParse[3]);
}

/* Finds LFB addresses and stuff. */
bool k6init_findAndAddLFBsToMTRRConfig(void) {
    vesa_ModeInfo   currentMode;
    bool            vesaBiosValid = vesa_isValidVesaBios(&s_sysInfo.vesaBiosInfo);
    u32             vramSizeKB = vesa_getVRAMSize(&s_sysInfo.vesaBiosInfo) / 1024UL;
    size_t          lfbsFound = 0;
    size_t          i;

    retPrintErrorIf(vesaBiosValid == false, "No VESA BIOS found, cannot scan for LFBs!", 0);

    vgacon_print("Scanning %u VESA modes for Linear Frame Buffers...\n", (u16) vesa_getModeCount(&s_sysInfo.vesaBiosInfo));

    for (i = 0; i < vesa_getModeCount(&s_sysInfo.vesaBiosInfo); i++) {
        retPrintErrorIf(false == vesa_getModeInfoByIndex(&s_sysInfo.vesaBiosInfo, &currentMode, i),
            "Failed to get info for VESA mode 0x%x", i);

        /* Check if the address has LFB and it is an unknown location */
        if (!currentMode.attributes.hasLFB || k6init_isKnownMTRRAddress(currentMode.lfbAddress)) {
            continue;
        }

        if (k6init_areAllMTRRsUsed()) {
            vgacon_printWarning("MTRR list full, stopping search...\n");
            break;
        }

        vgacon_print("Found Linear Frame Buffer at: 0x%08lx\n", currentMode.lfbAddress);
        lfbsFound++;

        retPrintErrorIf(false == k6init_addMTRRToConfig(currentMode.lfbAddress, vramSizeKB, true, false),
            "Error adding LFB address to MTRR list!", 0);
    }

    vgacon_printOK("Added %u VESA Frame Buffers to MTRR list.\n", (unsigned) lfbsFound);
    return true;
}

bool k6init_findAndAddPCIFBsToMTRRConfig(void) {
    pci_Device     *curDevice = NULL;
    pci_DeviceInfo  curDeviceInfo;
    size_t          pciFbsFound = 0;
    u32             i;

    retPrintErrorIf(pci_test() == false, "FATAL: Unable to access PCI bus!", 0);

    while (NULL != (curDevice = pci_getNextDevice(curDevice))) {
        /* If this isn't a VGA card, continue searching */
        if (pci_getClass(*curDevice) != CLASS_DISPLAY || pci_getSubClass(*curDevice) != 0x00)
            continue;

        retPrintErrorIf(pci_populateDeviceInfo(&curDeviceInfo, *curDevice) == false, "Failed to read PCI device info!", 0);

        vgacon_print("Found Graphics Card, Vendor 0x%04x, Device 0x%04x\n", curDeviceInfo.vendor, curDeviceInfo.device);

        for (i = 0; i < PCI_BARS_MAX; i++) {
            if (curDeviceInfo.bars[i].type != PCI_BAR_MEMORY)               continue; /* Must be memory BAR */
            if (curDeviceInfo.bars[i].size < 1048576UL)                     continue; /* Must be at least 1MB */
            if (k6init_isKnownMTRRAddress(curDeviceInfo.bars[i].address))   continue; /* Must be unknown address */

            if (k6init_areAllMTRRsUsed()) {
                vgacon_printWarning("MTRR list full, stopping search...\n");
                break;
            }

            vgacon_print("Found PCI/AGP frame buffer at: 0x%08lx\n", curDeviceInfo.bars[i].address);
            pciFbsFound++;

            if (false == k6init_addMTRRToConfig(curDeviceInfo.bars[i].address, curDeviceInfo.bars[i].size / 1024UL, true, false)) {
                vgacon_printError("Error adding LFB address to MTRR list!\n");
                free(curDevice);
                return false;
            }
        }
    }

    if (curDevice != NULL)
        free(curDevice);

    vgacon_printOK("Added %u PCI/AGP Frame Buffers to MTRR list.\n", (unsigned) pciFbsFound);
    return true;
}

static bool k6init_argAddVGAMTRR(const void *arg) {
    UNUSED_ARG(arg);
    return k6init_addMTRRToConfig(0xA0000, 128, true, false);
}

static bool k6init_argWriteAllocate(const void *arg) {
    UNUSED_ARG(arg);
    if (s_params.wAlloc.size == 0) {
        s_params.wAlloc.size = s_sysInfo.memSize / 1024UL;
        s_params.wAlloc.hole = s_sysInfo.memHole;
    }
    return true;
}

static bool k6init_argForceWAHole(const void *arg) {
    UNUSED_ARG(arg);
    retPrintErrorIf(s_params.wAlloc.setup == false, "Can't force 15M Hole without setting Write Allocate!", 0);
    return true;
}

static bool k6init_argSetL2(const void *arg) {
    UNUSED_ARG(arg);
    retPrintErrorIf(!s_sysInfo.cpu.supportsL2, "Can't set L2; this CPU doesn't have on-die L2 cache.", 0);
    return true;
}

static bool k6init_argSetPrefetch(const void *arg) {
    UNUSED_ARG(arg);
    retPrintErrorIf(!s_sysInfo.cpu.supportsEFER, "This CPU doesn't support data prefetch control.", 0);
    return true;
}

static bool k6init_argWriteOrder(const void *arg) {
    UNUSED_ARG(arg);
    retPrintErrorIf(!s_sysInfo.cpu.supportsEFER, "This CPU doesn't support write ordering.", 0);
    retPrintErrorIf(s_params.wOrder.mode >= (u8) __CPU_K6_WRITEORDER_MODE_COUNT__,
        "Value %u for Write Order Mode out of range!\n", s_params.wOrder.mode);
    return true;
}

static bool k6init_argSetMulti(const void *arg) {
    bool formatOK =     (isdigit(s_multiToParse[0]))
                    &&  (s_multiToParse[1] == '.' && s_multiToParse[3] == 0x00)
                    &&  (s_multiToParse[2] == '5' || s_multiToParse[2] == '0');

    retPrintErrorIf(formatOK == false, "Multiplier argument ('%s') format error!", s_multiToParse);
    UNUSED_ARG(arg);

    s_params.multi.integer = (u8) (s_multiToParse[0] - '0');
    s_params.multi.decimal = (u8) (s_multiToParse[2] - '0');

    return true;
}

void k6init_populateCPUInfo() {
    static const k6init_CPUCaps supportedCPUs[] = {
        /* Type             Name                    EWBE/DPE    >=CXT   L2      Multiplier */
        { K6,               "AMD K6",               false,      false,  false,  false },
        { K6_2,             "AMD K6-2",             true,       false,  false,  false },
        { K6_2_CXT,         "AMD K6-2 CXT",         true,       true,   false,  false },
        { K6_III,           "AMD K6-III",           true,       true,   true,   false },
        { K6_PLUS,          "AMD K6-2+/III+",       true,       true,   true,   true  },
        { UNSUPPORTED_CPU,  "<UNSUPPORTED CPU>",    false,      false,  false,  false },
    };

    sys_CPUIDVersionInfo info   = sys_getCPUIDVersionInfo();
    u16 model                   = info.basic.model;
    u16 stepping                = info.basic.stepping;

    sys_getCPUIDString(s_sysInfo.cpuidString);
    s_sysInfo.cpuidInfo = info;   
    s_sysInfo.cpu = supportedCPUs[UNSUPPORTED_CPU];

    if (info.basic.family != 5)
        return; /* Not a K6 family chip */

    if (model == 6)                     s_sysInfo.cpu = supportedCPUs[K6];
    if (model == 7)                     s_sysInfo.cpu = supportedCPUs[K6];
    if (model == 8 && stepping < 0x0c)  s_sysInfo.cpu = supportedCPUs[K6_2];
    if (model == 8 && stepping == 0x0c) s_sysInfo.cpu = supportedCPUs[K6_2_CXT];
    if (model == 9)                     s_sysInfo.cpu = supportedCPUs[K6_III];
    if (model == 0x0d)                  s_sysInfo.cpu = supportedCPUs[K6_PLUS];
}

static void k6init_populateSysInfo(void) {
    memset (&s_sysInfo, 0, sizeof(s_sysInfo));

    k6init_populateCPUInfo();
    
    if (s_sysInfo.cpu.type != UNSUPPORTED_CPU) {
        s_sysInfo.criticalError |= !cpu_K6_getWriteAllocateRange(&s_sysInfo.whcr);
        s_sysInfo.L1CacheEnabled = cpu_K6_getL1CacheStatus();

        if (s_sysInfo.cpu.supportsCxtFeatures)
            s_sysInfo.criticalError |= !cpu_K6_getMemoryTypeRanges(&s_sysInfo.mtrrs);
        
        if (s_sysInfo.cpu.supportsL2)
            s_sysInfo.L2CacheEnabled = cpu_K6_getL2CacheStatus();
    }

    /* Get memory & VESA info*/
    s_sysInfo.memSize = sys_getMemorySize(&s_sysInfo.memHole);
    s_sysInfo.vesaBIOSPresent = vesa_getBiosInfo(&s_sysInfo.vesaBiosInfo);
}

static void k6init_printCompactMTRRConfigs(const char *optionalTag, bool newLine) {
    size_t i;

    if (s_params.quiet)
        return;

    cpu_K6_getMemoryTypeRanges(&s_sysInfo.mtrrs); /* Update known MTRRs */

    if (optionalTag != NULL)
        vgacon_print("%s", optionalTag);

    for (i = 0; i < 2; i++) {
        if (s_sysInfo.mtrrs.configs[i].isValid == true) {
            printf("<%u: %lu KB @ %08lx> ", i,
                s_sysInfo.mtrrs.configs[i].sizeKB,
                s_sysInfo.mtrrs.configs[i].offset);
        } else {
            printf("<%u: unconfigured> ", (u16) i);
        }
    }

    printf("%s", newLine ? "\n" : "");
}

static void k6init_printAppLogoSysInfo(u8 logoColor) {
#define LOGO_HEADER_WIDTH   12
#define LOGO_HEADER_HEIGHT  6
    static const u8 k6initLogoData[LOGO_HEADER_WIDTH * LOGO_HEADER_HEIGHT] = {
        0x20, 0xDC, 0xDC, 0xDC, 0xDC, 0xDC, 0xDC, 0xDC, 0xDC, 0xDC, 0x20, 0x20,
        0x20, 0x20, 0xDF, 0xDB, 0xDB, 0xDB, 0xDB, 0xDB, 0xDB, 0xDB, 0x20, 0x20,
        0x20, 0x20, 0x20, 0xDC, 0xDF, 0xDF, 0xDF, 0xDB, 0xDB, 0xDB, 0x20, 0x20,
        0x20, 0xDC, 0xDB, 0xDB, 0x20, 0x20, 0x20, 0xDB, 0xDB, 0xDB, 0x20, 0x20,
        0x20, 0xDB, 0xDB, 0xDB, 0xDC, 0xDC, 0xDC, 0xDB, 0xDB, 0xDB, 0x20, 0x20,
        0x20, 0xDB, 0xDB, 0xDB, 0xDB, 0xDF, 0x20, 0x20, 0xDF, 0xDB, 0x20, 0x20
    };
    util_ApplicationLogo logo = { (const char *)k6initLogoData, LOGO_HEADER_WIDTH, LOGO_HEADER_HEIGHT, 0, VGACON_COLOR_BLACK };

    /* In quiet mode, we don't print anything except for the header */
    if (s_params.quiet) {
        printf("%s\n", k6init_versionString);
        return;
    }

    /* Turbo C won't let me do this assignment in the initialization*/
    logo.fgColor = logoColor;

    /* Print the title / version header */
    util_printWithApplicationLogo(&logo, "%s\n", k6init_versionString);

    /* Print the line with the little twig going down after 5 characters */
    util_printWithApplicationLogo(&logo, "");
    vgacon_fillCharacter('\xC4', 5);
    vgacon_fillCharacter('\xC2', 1);
    vgacon_fillCharacter('\xC4', 60);
    putchar('\n');

    /*  If our CPU is unsupported, print info about it, else the clearname */
    if (s_sysInfo.cpu.type == UNSUPPORTED_CPU) {
        util_printWithApplicationLogo(&logo, "CPU  \xB3[%s] Type %u Family %u Model %u Stepping %u \n",
            s_sysInfo.cpuidString,
            s_sysInfo.cpuidInfo.basic.type,
            s_sysInfo.cpuidInfo.basic.family,
            s_sysInfo.cpuidInfo.basic.model,
            s_sysInfo.cpuidInfo.basic.stepping);
    } else {
        util_printWithApplicationLogo(&logo, "CPU  \xB3[");
        vgacon_printColorString(s_sysInfo.cpu.name, VGACON_COLOR_LGREN, VGACON_COLOR_BLACK, false);
        printf("] L1 Cache: %s", s_sysInfo.L1CacheEnabled ? "ON" : "OFF");

        if (s_sysInfo.cpu.supportsL2)
             printf(", L2 Cache: %s", s_sysInfo.L2CacheEnabled ? "ON" : "OFF");

        printf("\n");
    }

    /* Print RAM info, should be reliable across all platforms we run on... */
    util_printWithApplicationLogo(&logo,     "RAM  \xB3");
    if (s_sysInfo.memSize > 0) {
        printf("%lu KB, 15MB Hole: %s\n",
            s_sysInfo.memSize / 1024UL,
            s_sysInfo.memHole ? "Yes" : "No");
    } else {
        vgacon_printColorString("? (Detection failed!)", VGACON_COLOR_YELLO, VGACON_COLOR_BLACK, true);
        printf("\n");
    }

    /* If we found a valid VESA BIOS, print some info about it */
    if (vesa_isValidVesaBios(&s_sysInfo.vesaBiosInfo)) {
        util_printWithApplicationLogo(&logo,     "VBIOS\xB3[%Fs], VESA %x.%x, %u modes, %lu MB\n",
            s_sysInfo.vesaBiosInfo.oemStringPtr ? s_sysInfo.vesaBiosInfo.oemStringPtr : "Unknown",
            s_sysInfo.vesaBiosInfo.version.major,
            s_sysInfo.vesaBiosInfo.version.minor,
            (u16) vesa_getModeCount(&s_sysInfo.vesaBiosInfo),
            vesa_getVRAMSize(&s_sysInfo.vesaBiosInfo) >> 20UL); /* /1024/1024 -> size in megabytes */
    } else {
        util_printWithApplicationLogo(&logo,     "VBIOS\xB3<No VESA compatible VGA BIOS detected>\n");
    }

    /*  If the CPU is supported, print MTRR configs, else make clear that it isn't */
    if (s_sysInfo.cpu.supportsCxtFeatures) {
        util_printWithApplicationLogo(&logo,     "MTRR \xB3");
        k6init_printCompactMTRRConfigs(NULL, true);
    } else {
        util_printWithApplicationLogo(&logo,     "MTRR \xB3");
        vgacon_printColorString("< Not supported by CPU >", VGACON_COLOR_LRED, VGACON_COLOR_BLACK, true);
        putchar('\n');
    }

    putchar('\n');
}

typedef bool (*action)(void);   /* Function pointer to action to execute if condition is true */
static bool k6init_doIfSetupAndPrint(bool condition, action function, const char *fmt, ...) {
    if (condition) {
        va_list args;
        bool success = function();
        vgacon_LogLevel logLevel = success ? VGACON_LOG_LEVEL_OK : VGACON_LOG_LEVEL_ERROR;

        va_start(args, fmt);
        vgacon_vprintfLogLevel(logLevel, fmt, args, true);
        va_end(args);

        return success;
    }
    return true;
}

static bool k6init_doMTRRCfg(void) {
    bool success = true;

    retPrintErrorIf(!s_sysInfo.cpu.supportsCxtFeatures, "MTRRs only supported on K6-2 CXT or higher. Skipping...", 0);

    if (s_params.mtrr.lfb) success &= k6init_findAndAddLFBsToMTRRConfig();
    if (s_params.mtrr.pci) success &= k6init_findAndAddPCIFBsToMTRRConfig();
    success &= cpu_K6_setMemoryTypeRanges(&s_params.mtrr.toSet);
    k6init_printCompactMTRRConfigs("New MTRR setup: ", true);
    return success;
}

static bool k6init_doChipsetTweaks(void) {
    return chipset_autoConfig(&s_params, &s_sysInfo);
}

static bool k6init_doWriteAllocCfg(void) {
    return cpu_K6_setWriteAllocateRangeValues(s_params.wAlloc.size, s_params.wAlloc.hole);
}

static bool k6init_doWriteOrderCfg(void) {
    retPrintErrorIf(!s_sysInfo.cpu.supportsEFER, "Write ordering not supported on this CPU. Skipping...", 0);
    return cpu_K6_setWriteOrderMode((cpu_K6_WriteOrderMode) s_params.wOrder.mode);
}

static bool k6init_doMultiCfg(void) {
    cpu_K6_SetMulError errCode;
    retPrintErrorIf(!s_sysInfo.cpu.supportsMulti, "Multiplier configuration only supported on K6-2+/III+. Skipping...", 0);
    errCode = cpu_K6_setMultiplier(s_params.multi.integer, s_params.multi.decimal);
    retPrintErrorIf(errCode == SETMUL_BADMUL, "The given multiplier value is invalid and not supported!", 0);
    retPrintErrorIf(errCode == SETMUL_ERROR, "There was a system error while setting the multiplier!", 0);
    return true;
}

static bool k6init_doL1Cfg(void) {
    return cpu_K6_setL1Cache(s_params.l1Cache.enable);
}

static bool k6init_doL2Cfg(void) {
    retPrintErrorIf(!s_sysInfo.cpu.supportsL2, "This CPU does not have on-die L2 cache. Skipping...", 0);
    return cpu_K6_setL2Cache(s_params.l2Cache.enable);
}

static bool k6init_doPrefetchCfg(void) {
    retPrintErrorIf(!s_sysInfo.cpu.supportsEFER, "This CPU does not support data prefetch control. Skipping...", 0);
    return cpu_K6_setDataPrefetch(s_params.prefetch.enable);
}

bool k6init_doPrintBARs(void) {
    pci_Device     *curDevice = NULL;
    pci_DeviceInfo  curDeviceInfo;
    u32             i;

    retPrintErrorIf(pci_test() == false, "FATAL: Unable to access PCI bus!", 0);

    if (s_params.quiet) {
        vgacon_printWarning("/listbars used with /quiet, unmuting the program!\n");
        vgacon_setLogLevel(VGACON_LOG_LEVEL_INFO);
    }

    while (NULL != (curDevice = pci_getNextDevice(curDevice))) {
        if (pci_populateDeviceInfo(&curDeviceInfo, *curDevice) == false) {
            vgacon_printWarning("Failed to obtain PCI device info...\n");
            continue;
        }

        vgacon_printOK("[Device @ %u:%u:%u] ", curDevice->bus, curDevice->slot, curDevice->func);
        printf("Vendor 0x%04x Device 0x%04x Class %02x Subclass %02x:\n",
            curDeviceInfo.vendor, curDeviceInfo.device, curDeviceInfo.classCode, curDeviceInfo.subClass);
        for (i = 0; i < PCI_BARS_MAX; i++) {
            if (curDeviceInfo.bars[i].address > 0UL)
                vgacon_print("   --> [BAR %lu] @ 0x%08lx (%s) Size %lu KB\n",
                    i,
                    curDeviceInfo.bars[i].address,
                    (curDeviceInfo.bars[i].type == PCI_BAR_MEMORY) ? "Memory" : "I/O",
                    curDeviceInfo.bars[i].size / 1024UL);
        }
    }

    return true;
}

static const char k6init_appDescription[] =
    "http://github.com/oerg866/k6init\n"
    "\n"
    "K6INIT is a driver for MS-DOS that lets you configure special features of\n"
    "AMD K6/K6-2/2+/III/III+ processors, similar to FASTVID on Pentium systems.\n"
    "\n"
    "It works on any K6 family CPUs, but K6 and K6-2 (pre-CXT) lack many features.\n"
    "In contrast to other tools, K6INIT can be loaded from CONFIG.SYS, so it works\n"
    "even with an extended memory manager (such as EMM386) installed.\n"
    "\n"
    "If called with the /auto parameter, it does the following:\n"
    "- Finds linear frame buffer memory regions using PCI/AGP and VESA methods,\n"
    "  then sets up write combining for them\n"
    "- Enables Write Allocate for the entire system memory range\n"
    "- Enables Write Ordering except for uncacheable / write-combined regions\n"
    "\n"
    "/auto is equivalent to '/pci /lfb /wa:0 /wo:1 /l1:1 /l2:1 /prefetch:1'\n"
    "\n"
    "K6INIT was built with the LIB866D DOS Real-Mode Software Development Library\n"
    "http://github.com/oerg866/lib866d\n";

static const args_arg k6init_args[] = {
    ARGS_HEADER(k6init_versionString, k6init_appDescription),
    ARGS_USAGE("?", "Prints parameter list"),

    { "status",     NULL,               "Display current program status.",                      ARG_FLAG,               NULL,                       NULL,                       NULL },
    { "quiet",      NULL,               "Reduce text output, only print warnings/errors",       ARG_FLAG,               NULL,                       &s_params.quiet,            NULL },
                            ARGS_BLANK,
    { "auto",       NULL,               "Attempt fully automated setup (See above.)",           ARG_FLAG,               NULL,                       NULL,                       k6init_argAutoSetup },
                            ARGS_EXPLAIN("Parts of this procedure can be disabled with these"),
                            ARGS_EXPLAIN("four arguments (with '/auto' being the first):"),

    { "skippci",    NULL,               "Skip PCI/AGP Frame Buffer Detection & MTRR Setup",     ARG_NFLAG,              NULL,                       &s_params.mtrr.pci,         NULL },
    { "skiplfb",    NULL,               "Skip VESA Linear Frame Buffer Detection & MTRR Setup", ARG_NFLAG,              NULL,                       &s_params.mtrr.lfb,         NULL },
    { "skipcpu",    NULL,               "Skip CPU internals setup (Cache, Prefetch)",           ARG_FLAG,               NULL,                       NULL,                       k6init_argSkipCPUStuff },
    { "skipwawo",   NULL,               "Skip Write Allocate / Order setup",                    ARG_FLAG,               NULL,                       NULL,                       k6init_argSkipWAWO },
                            ARGS_BLANK,
    { "chipset",    NULL,               "Apply chipset-specific tweaks (EXPERIMENTAL!!)",       ARG_FLAG,               NULL,                       &s_params.chipsetTweaks,    NULL },
                            ARGS_EXPLAIN("WARNING: Highly experimental feature!"),
                            ARGS_EXPLAIN("Some chipsets support acceleration of Frame Buffer"),
                            ARGS_EXPLAIN("write cycles, which K6INIT can leverage."),
                            ARGS_EXPLAIN("Supported chipsets:"),
                            ARGS_EXPLAIN("  - ALi ALADDIN III, IV, V"),
                            ARGS_EXPLAIN("  - SiS 5571, 5581, 5591, 5597"),
                            ARGS_EXPLAIN("  - SiS 530, 540"),
                            ARGS_BLANK,
    { "mtrr",       "offset,size,wc,uc","Configure MTRR manually (e.g. to set write combine)",  ARG_ARRAY(ARG_U32, 4),  &s_params.mtrr.setup,       s_MTRRCfgQueue,             k6init_argAddMTRR },
                            ARGS_EXPLAIN("offset: linear offset (e.g. 0xE0000000)"),
                            ARGS_EXPLAIN("size:   length in KILOBYTES (e.g. 8192)"),
                            ARGS_EXPLAIN("wc:     '1': Region is write-combine"),
                            ARGS_EXPLAIN("uc:     '1': Region is uncacheable"),
                            ARGS_EXPLAIN("NOTE - /mtrr can be be used twice."),
                            ARGS_EXPLAIN("NOTE - Will discard any MTRRs configured before"),    /* This is a side effect of ignoring previous MTRR config when setting MTRR*/
                            ARGS_EXPLAIN("running this program."),

    { "mtrrclr",    NULL,               "Clear Memory Type Range Registers",                    ARG_FLAG,               &s_params.mtrr.setup,       NULL,                       k6init_argClearMTRRs },
                            ARGS_EXPLAIN("Clears any MTRRs, effectively disabling any"),
                            ARGS_EXPLAIN("Write-Combine and Uncacheable regions."),

    { "lfb",        NULL,               "Find and enable Write Combine for Linear Frame Buffer",ARG_FLAG,               &s_params.mtrr.setup,       &s_params.mtrr.lfb,         NULL },

    { "pci",        NULL,               "Find and enable Write Combine for Frame Buffers",      ARG_FLAG,               &s_params.mtrr.setup,       &s_params.mtrr.pci,         NULL },
                            ARGS_EXPLAIN("exposed by PCI/AGP cards (experimental)"),
                            ARGS_EXPLAIN("NOTE: Known to cause problems in Windows 9x with"),
                            ARGS_EXPLAIN("some cards."),

    { "vga",        NULL,               "Enables Write Combine for the VGA memory region",      ARG_FLAG,               &s_params.mtrr.setup,       NULL,                       k6init_argAddVGAMTRR },
                            ARGS_EXPLAIN("(A0000-BFFFF). WARNING: Potentially unsafe."),
                            ARGS_EXPLAIN("You MUST NOT use this memory region for UMBs."),
                            ARGS_EXPLAIN("This parameter is equivalent to /wc:0xA0000,128,1,0"),

    ARGS_BLANK,

    { "wa",         "size",             "Configure Write Allocate manually",                    ARG_U32,                &s_params.wAlloc.setup,     &s_params.wAlloc.size,      k6init_argWriteAllocate },
                            ARGS_EXPLAIN("size: Memory size in KB"),
                            ARGS_EXPLAIN("Set this to 0 to auto-detect size + 15-16M Hole."),

    { "wahole",     "1/0",              "Force 15-16M Memory Hole for Write Allocate",          ARG_BOOL,               NULL,                       &s_params.wAlloc.hole,      NULL },
                            ARGS_EXPLAIN("K6INIT usually detects the hole by itself,"),
                            ARGS_EXPLAIN("but you can use this parameter to force it on/off."),
                            ARGS_EXPLAIN("(needs /auto or /wa to be effective)"),

    ARGS_BLANK,

    { "wo",         "mode",             "Configure Write Order Mode",                           ARG_U8,                 &s_params.wOrder.setup,     &s_params.wOrder.mode,      k6init_argWriteOrder },
                            ARGS_EXPLAIN("mode: a single digit indicating the WO mode:"),
                            ARGS_EXPLAIN("0 - All Memory Regions (Slow)"),
                            ARGS_EXPLAIN("1 - All except Uncacheable/Write-Combined (Fast)"),
                            ARGS_EXPLAIN("2 - No Memory Regions (Fastest)"),

    ARGS_BLANK,

    { "multi",      "x.y",              "Configure CPU Frequency Multiplier",                   ARG_STRING(3),          &s_params.multi.setup,      s_multiToParse,             k6init_argSetMulti },
                            ARGS_EXPLAIN("x: integral part of multiplier"),
                            ARGS_EXPLAIN("y: fractional part of multiplier"),
                            ARGS_EXPLAIN("IMPORTANT: Requires K6-2+ or K6-III+ CPU!"),
                            ARGS_EXPLAIN("Example: /multi:5.5"),

    ARGS_BLANK,

    { "l1",         "1/0",              "Enable/Disable Level 1 cache",                         ARG_BOOL,               &s_params.l1Cache.setup,    &s_params.l1Cache.enable,   NULL },
    { "l2",         "1/0",              "Enable/Disable Level 2 cache",                         ARG_BOOL,               &s_params.l2Cache.setup,    &s_params.l2Cache.enable,   k6init_argSetL2 },
                            ARGS_EXPLAIN("NOTE: Only K6-2+ and K6-III+ have on-die L2 Cache!"),
    { "prefetch",   "1/0",              "Enable/Disable Data Prefetch",                         ARG_BOOL,               &s_params.prefetch.setup,   &s_params.prefetch.enable,  k6init_argSetPrefetch },

    ARGS_BLANK,

    { "listbars",   NULL,               "List all PCI/AGP device Base Address Regions (BARs)",  ARG_FLAG,               NULL,                       &s_params.printBARs,        NULL },
};

int main(int argc, char *argv[]) {
    const char *writeOrderModeStrings[] = { "0, All Memory Regions",
                                            "1, All except Uncacheable/Write-Combined",
                                            "2, No Memory Regions" };
    args_ParseError argErr;
    u8              logoColor = VGACON_COLOR_GREEN;
    bool            ok = true;

    /* V86 mode is a no-no! */
    if (sys_cpuIsInV86Mode()) {
        vgacon_printError("K6INIT can't run in V86 mode!\n");
        vgacon_print("Hint: Load it in CONFIG.SYS before memory managers!\n");
        vgacon_print("Example: DEVICE=K6INIT.EXE /auto\n");
        return -1;
    }

    /* Privileged instructions cause GPFs on WINDOWS, so we exit. */
    if (sys_getWindowsMode() != OS_PURE_DOS) {
         vgacon_printError("K6INIT cannot run on Windows.\n");
         return -1;
    }

    k6init_populateSysInfo();

    memset(&s_params, 0, sizeof(s_params));
    argErr = args_parseAllArgs(argc, (const char **) argv, k6init_args, ARRAY_SIZE(k6init_args));

    if (argErr == ARGS_USAGE_PRINTED)               { return 0; }

    if      (s_sysInfo.criticalError == true)       { logoColor = VGACON_COLOR_RED; }
    else if (s_sysInfo.cpu.type == UNSUPPORTED_CPU) { logoColor = VGACON_COLOR_LRED; }
    else if (argErr == ARGS_NO_ARGUMENTS)           { logoColor = VGACON_COLOR_YELLO; }
    else if (argErr != ARGS_SUCCESS)                { logoColor = VGACON_COLOR_BROWN; }

    if (s_params.quiet)
        vgacon_setLogLevel(VGACON_LOG_LEVEL_WARNING);

    k6init_printAppLogoSysInfo(logoColor);

    if (s_sysInfo.cpu.type == UNSUPPORTED_CPU) {
        putchar(' ');
        vgacon_printColorString("Please run this program on an AMD-K6/K6-2/K6-2+/K6-III/K6-III+!", VGACON_COLOR_LRED, VGACON_COLOR_BLACK, true);
        printf("\n");
        return -1;
    } else if (argErr == ARGS_NO_ARGUMENTS) {
        vgacon_printWarning("No arguments given. Use /? for more information.\n");
        return 1;
    } else if (argErr != ARGS_SUCCESS) {
        vgacon_printError("User input error, quitting...\n");
        return (int) argErr;
    }

    /* Do actual execution of requested actions */
    ok &= k6init_doIfSetupAndPrint(s_params.printBARs,      k6init_doPrintBARs,     "Print PCI/AGP device BARs");
    ok &= k6init_doIfSetupAndPrint(s_params.mtrr.setup,     k6init_doMTRRCfg,       "Set MTRR Config");
    ok &= k6init_doIfSetupAndPrint(s_params.chipsetTweaks,  k6init_doChipsetTweaks, "Set Chipset Tweaks");
    ok &= k6init_doIfSetupAndPrint(s_params.wAlloc.setup,   k6init_doWriteAllocCfg, "Set Write Allocate Config (%lu KB)",
                                                                                        s_params.wAlloc.size);
    ok &= k6init_doIfSetupAndPrint(s_params.wOrder.setup,   k6init_doWriteOrderCfg, "Set Write Order Mode (%s)",
                                                                                        writeOrderModeStrings[(u16)s_params.wOrder.mode]);
    ok &= k6init_doIfSetupAndPrint(s_params.multi.setup,    k6init_doMultiCfg,      "Set Frequency Multiplier (%sx)",
                                                                                        s_multiToParse);
    ok &= k6init_doIfSetupAndPrint(s_params.l1Cache.setup,  k6init_doL1Cfg,         "Set L1 Cache (%s)",
                                                                                        s_params.l1Cache.enable ? "On" : "Off");
    ok &= k6init_doIfSetupAndPrint(s_params.l2Cache.setup,  k6init_doL2Cfg,         "Set L2 Cache (%s)",
                                                                                        s_params.l2Cache.enable ? "On" : "Off");
    ok &= k6init_doIfSetupAndPrint(s_params.prefetch.setup, k6init_doPrefetchCfg,   "Set Data Prefetch (%s)",
                                                                                        s_params.prefetch.enable ? "On" : "Off");
    if (!ok)
        vgacon_printWarning("Summary: Some actions failed!\n");

    return (ok == true) ? 0 : -1;
}

