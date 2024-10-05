#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include "vesabios.h"
#include "vgacon.h"
#include "util.h"
#include "args.h"
#include "sys.h"
#include "pci.h"
#include "cpu_k6.h"

#define __LIB866D_TAG__ "K6INIT"
#include "debug.h"

/* This structure holds all the arguments passed to the program. */
static struct {
    bool        autoSetup;
    /* MTRR Config */
    struct {    bool setup;
                bool clear;
                size_t count;
                cpu_K6_MemoryTypeRangeRegs toSet;   } mtrr;
    /* Write Ordering Config */
    struct {    bool setup;
                u8 mode;                            } wOrder;
    /* Write Allocate Config */
    struct {    bool setup;
                bool hole;
                u32 size;                           } wAlloc;
    /* Multiplier Config */
    struct {    bool setup;
                u8 integer;
                u8 decimal;                         } multi;
    /* L1 Cache Config */
    struct {    bool setup;
                bool enable;                        } l1Cache;
    /* L2 Cache Config */
    struct {    bool setup;
                bool enable;                        } l2Cache;
} s_params;

typedef enum {
    K6_2_CXT = 0,
    K6_III,
    K6_PLUS,
    UNSUPPORTED_CPU
} k6init_SupportedCPU;

/* This structure holds all the detected system info we may need for this program. */
static struct {
    sys_CPUIDVersionInfo        cpuidInfo;
    sys_CPUManufacturer         cpuManufacturer;
    char                        cpuidString[13];
    k6init_SupportedCPU         thisCPU;
    cpu_K6_MemoryTypeRangeRegs  mtrrs;
    cpu_K6_WriteAllocateConfig  whcr;
    u32                         memSize;
    bool                        memHole;
    bool                        vesaBIOSPresent;
    vesa_BiosInfo               vesaBiosInfo;
    bool                        L1CacheEnabled;
    bool                        L2CacheEnabled;
    bool                        criticalError;
} s_sysInfo;

static char         s_multiToParse[4] = {0,};
static u32          s_MTRRCfgQueue[4];
static const char   k6init_versionString[] = "K6INIT Version 1.06 - (C) 2021-2024 Eric Voirin (oerg866)";

#define retPrintErrorIf(condition, message, value) if (condition) { vgacon_printError(message "\n", value); return false; }

static bool k6init_addMTRRToConfig(u32 offset, u32 sizeKB, bool writeCombine, bool uncacheable) {
    retPrintErrorIf(s_params.mtrr.clear == true,    "Cannot clear MTRRs and set them up at the same time!",         0);
    retPrintErrorIf(s_params.mtrr.count >= 2,       "You are trying to configure too many MTRRs, maximum is 2!",    0);
    retPrintErrorIf(sizeKB > 0x400000UL,            "Requested MTRR size of %lu KB too big!",                       sizeKB);
    retPrintErrorIf((offset % 131072UL) != 0UL,     "MTRR offset %lu isn't aligned on a 128KB boundary!",           offset);
    retPrintErrorIf(sizeKB < 128UL,                 "Requested MTRR size of %lu KB is too small (< 128KB)!",        sizeKB);

    s_params.mtrr.toSet.configs[s_params.mtrr.count].offset         = offset;
    s_params.mtrr.toSet.configs[s_params.mtrr.count].sizeKB         = sizeKB;
    s_params.mtrr.toSet.configs[s_params.mtrr.count].writeCombine   = writeCombine;
    s_params.mtrr.toSet.configs[s_params.mtrr.count].uncacheable    = uncacheable;
    s_params.mtrr.toSet.configs[s_params.mtrr.count].isValid        = true;

    DBG("k6init_addMTRRToConfig: 0x%08lx | %lu KB | %s | %s\n", offset, sizeKB, writeCombine ? "WC" : "  ", uncacheable ? "UC" : "  ");

    s_params.mtrr.count++;
    s_params.mtrr.setup = true;

    return true;
}

static bool k6init_argClearMTRRs(const void *arg) {
    UNUSED_ARG(arg);
    retPrintErrorIf(s_params.mtrr.setup == true, "Cannot clear MTRRs and set them up at the same time!", 0);

    memset(&s_params.mtrr.toSet, 0, sizeof(cpu_K6_MemoryTypeRangeRegs));
    s_params.mtrr.count = 2;
    s_params.mtrr.setup = true;
    return true;
}

static bool k6init_argAddMTRR(const void *arg) {
    u32    *toParse     = (u32 *)arg;
    bool    formatOK    = (toParse[2] <= 1 && toParse[3] <= 1); /* WC/UC flags must be valid bools */

    retPrintErrorIf(formatOK == false, "MTRR Config Argument Format error.", 0);
    return k6init_addMTRRToConfig(toParse[0], toParse[1], (bool) toParse[2], (bool) toParse[3]);
}

static bool k6init_isKnownMTRRAddress(u32 address) {
    if (s_params.mtrr.setup == false)                       return false;
    if (address == s_params.mtrr.toSet.configs[0].offset)   return true;
    if (address == s_params.mtrr.toSet.configs[1].offset)   return true;
    return false;
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
            "No VESA BIOS found, cannot scan for LFBs!", 0);

        if (currentMode.attributes.hasLFB) {
            /* Check if the address is already known */
            if (k6init_isKnownMTRRAddress(currentMode.lfbAddress)) {
                lfbsFound++;
                continue;
            }

            vgacon_print("Found Linear Frame Buffer at: 0x%08lx\n", currentMode.lfbAddress);
            L866_ASSERTM(lfbsFound < 2, "More than two unique LFB addresses found! Please configure manually!");

            if (false == k6init_addMTRRToConfig(currentMode.lfbAddress, vramSizeKB, true, false)) {
                return false;
            }

            lfbsFound++;
        }
    }

    retPrintErrorIf(lfbsFound == 0, "There is a VESA BIOS but no LFB modes were found!", 0);
    return true;
}

bool k6init_findAndAddPCIFBsToMTRRConfig(void) {
    pci_Device     *curDevice = NULL;
    pci_DeviceInfo  curDeviceInfo;
    size_t          pciFbsFound = 0;
    u32             i;

    while (NULL != (curDevice = pci_getNextDevice(curDevice))) {
        /* If this isn't a VGA card, continue searching */
        if (pci_getClass(*curDevice) != CLASS_DISPLAY || pci_getSubClass(*curDevice) != 0x00)
            continue;

        retPrintErrorIf(pci_populateDeviceInfo(&curDeviceInfo, *curDevice) == false, "Failed to read PCI device info!", 0);

        vgacon_print("Found Graphics Card, Vendor 0x%04x, Device 0x%04x\n", curDeviceInfo.vendor, curDeviceInfo.device);

        for (i = 0; i < PCI_BARS_MAX; i++) {
            if (curDeviceInfo.bars[i].type != PCI_BAR_MEMORY)   continue; /* Must be memory BAR */
            if (curDeviceInfo.bars[i].size < 1048576UL)         continue; /* Must be at least 1MB */

            /* Check if address is already known */
            if (k6init_isKnownMTRRAddress(curDeviceInfo.bars[i].address)) {
                pciFbsFound++;
                continue;
            }

            vgacon_print("Found PCI/AGP frame buffer at: 0x%08lx\n", curDeviceInfo.bars[i].address);

            if (false == k6init_addMTRRToConfig(curDeviceInfo.bars[i].address, curDeviceInfo.bars[i].size / 1024UL, true, false)) {
                return false;
            }

            pciFbsFound++;
        }
    }

    retPrintErrorIf(pciFbsFound == 0, "No PCI/AGP Frame Buffers were found!", 0);

    if (curDevice != NULL)
        free(curDevice);

    return true;
}

static bool k6init_argAddLFBMTRR(const void *arg) {
    UNUSED_ARG(arg);
    return k6init_findAndAddLFBsToMTRRConfig();
}

static bool k6init_argAddPCIMTRR(const void *arg) {
    UNUSED_ARG(arg);
    return k6init_findAndAddPCIFBsToMTRRConfig();
}

static bool k6init_argAddVGAMTRR(const void *arg) {
    UNUSED_ARG(arg);
    return k6init_addMTRRToConfig(0xA0000, 128, true, false);
}

static bool k6init_argForceWAHole(const void *arg) {
    UNUSED_ARG(arg);
    retPrintErrorIf(s_params.wAlloc.setup == false, "Can't force 15M Hole without setting Write Allocate!", 0);
    return true;
}

static bool k6init_argWriteOrder(const void *arg) {
    retPrintErrorIf(s_params.wOrder.mode >= (u8) __CPU_K6_WRITEORDER_MODE_COUNT__,
        "Value %u for Write Order Mode out of range!\n", s_params.wOrder.mode);
    UNUSED_ARG(arg);
    return s_params.wOrder.setup = true;
}

static bool k6init_argWriteAllocate(const void *arg) {
    UNUSED_ARG(arg);
    return s_params.wAlloc.setup = true;
}

static bool k6init_argAddMulti(const void *arg) {
    bool formatOK =     (isdigit(s_multiToParse[0]))
                    &&  (s_multiToParse[1] == '.' && s_multiToParse[3] == 0x00)
                    &&  (s_multiToParse[2] == '5' || s_multiToParse[2] == '0');

    retPrintErrorIf(formatOK == false, "Multiplier argument ('%s') format error!", s_multiToParse);
    UNUSED_ARG(arg);

    s_params.multi.integer = (u8) (s_multiToParse[0] - '0');
    s_params.multi.decimal = (u8) (s_multiToParse[2] - '0');
    s_params.multi.setup   = true;

    return true;
}

static bool k6init_argAddL1(const void *arg) {
    UNUSED_ARG(arg);
    return s_params.l1Cache.setup = true;
}

static bool k6init_argAddL2(const void *arg) {
    UNUSED_ARG(arg);
    return s_params.l2Cache.setup = true;
}

k6init_SupportedCPU k6init_getSupportedCPUFromCPUID(sys_CPUIDVersionInfo info) {
    if (info.basic.family == 5 && info.basic.model == 8 && info.basic.stepping == 0x0c)
        return K6_2_CXT;
    if (info.basic.family == 5 && info.basic.model == 9)
        return K6_III;
    if (info.basic.family == 5 && info.basic.model == 0x0d)
        return K6_PLUS;

    return UNSUPPORTED_CPU;
};

static const char *k6init_getK6CPUString(k6init_SupportedCPU cpu) {
    if (cpu == K6_2_CXT)    return "AMD K6-2 CXT";
    if (cpu == K6_III)      return "AMD K6-III";
    if (cpu == K6_PLUS)     return "AMD K6-2+/III+";
    return "<UNSUPPORTED CPU>";
}

static void k6init_populateSysInfo(void) {
    memset (&s_sysInfo, 0, sizeof(s_sysInfo));

    /* Get CPU info */
    sys_getCPUIDString(s_sysInfo.cpuidString);
    s_sysInfo.cpuidInfo = sys_getCPUIDVersionInfo();
    s_sysInfo.thisCPU = k6init_getSupportedCPUFromCPUID(s_sysInfo.cpuidInfo);
    s_sysInfo.cpuManufacturer = sys_getCPUManufacturer(NULL);

    if (s_sysInfo.thisCPU != UNSUPPORTED_CPU) {
        s_sysInfo.criticalError |= !cpu_K6_getMemoryTypeRanges(&s_sysInfo.mtrrs);
        s_sysInfo.criticalError |= !cpu_K6_getWriteAllocateRange(&s_sysInfo.whcr);
        s_sysInfo.L1CacheEnabled = cpu_K6_getL1CacheStatus();
        s_sysInfo.L2CacheEnabled = cpu_K6_getL2CacheStatus();
    }

    /* Get memory info */
    s_sysInfo.memSize = sys_getMemorySize(&s_sysInfo.memHole);

    /* Get VGA BIOS info */
    s_sysInfo.vesaBIOSPresent = vesa_getBiosInfo(&s_sysInfo.vesaBiosInfo);
}

static void k6init_printCompactMTRRConfigs(const char *optionalTag, bool newLine) {
    size_t i;
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
    if (s_sysInfo.thisCPU == UNSUPPORTED_CPU) {
        util_printWithApplicationLogo(&logo, "CPU  \xB3[%s] Type %u Family %u Model %u Stepping %u \n",
            s_sysInfo.cpuidString,
            s_sysInfo.cpuidInfo.basic.type,
            s_sysInfo.cpuidInfo.basic.family,
            s_sysInfo.cpuidInfo.basic.model,
            s_sysInfo.cpuidInfo.basic.stepping);
    } else {
        util_printWithApplicationLogo(&logo, "CPU  \xB3[");
        vgacon_printColorString(k6init_getK6CPUString(s_sysInfo.thisCPU), VGACON_COLOR_LGREN, VGACON_COLOR_BLACK, false);
        printf("] L1 Cache: %s", s_sysInfo.L1CacheEnabled ? "ON" : "OFF");

        if (s_sysInfo.thisCPU == K6_III || s_sysInfo.thisCPU == K6_PLUS)
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
    if (s_sysInfo.thisCPU != UNSUPPORTED_CPU) {
        util_printWithApplicationLogo(&logo,     "MTRR \xB3");
        k6init_printCompactMTRRConfigs(NULL, true);
    } else {
        util_printWithApplicationLogo(&logo,     "MTRR \xB3");
        vgacon_printColorString("< Unsupported CPU detected! >", VGACON_COLOR_LRED, VGACON_COLOR_BLACK, true);
        putchar('\n');
    }

    putchar('\n');
}

typedef bool (*action)(void);   /* Function pointer to action to execute if condition is true */
static bool k6init_doIfSetupAndPrint(bool condition, action function, const char *fmt, ...) {
    if (condition) {
        va_list args;
        bool success = function();

        if (success) vgacon_printOK(""); /* To get the header of the line printed */
        else         vgacon_printError("");

        va_start(args, fmt);
        vprintf(fmt, args);
        printf("\n");
        va_end(args);

        return success;
    }
    return true;
}

static bool k6init_doMTRRCfg(void) {
    bool success = cpu_K6_setMemoryTypeRanges(&s_params.mtrr.toSet);
    k6init_printCompactMTRRConfigs("New MTRR setup: ", true);
    return success;
}

static bool k6init_doWriteAllocCfg(void) {
    return cpu_K6_setWriteAllocateRangeValues(s_params.wAlloc.size, s_params.wAlloc.hole);
}

static bool k6init_doWriteOrderCfg(void) {
    return cpu_K6_setWriteOrderMode((cpu_K6_WriteOrderMode) s_params.wOrder.mode);
}

static bool k6init_doMultiCfg(void) {
    return cpu_K6_setMultiplier(s_params.multi.integer, s_params.multi.decimal);
}

static bool k6init_doL1Cfg(void) {
    return cpu_K6_setL1Cache(s_params.l1Cache.enable);
}

static bool k6init_doL2Cfg(void) {
    bool cpuHasL2 = (s_sysInfo.thisCPU == K6_III || s_sysInfo.thisCPU == K6_PLUS);
    retPrintErrorIf(cpuHasL2 == false, "This CPU does not have on-die L2 cache. Skipping...", 0);
    return cpu_K6_setL2Cache(s_params.l2Cache.enable);
}

static bool k6init_autoSetup(void) {
    s_params.wAlloc.setup      = (s_sysInfo.memSize > 0);
    s_params.wAlloc.size       = s_sysInfo.memSize / 1024UL;
    s_params.wAlloc.hole       = s_sysInfo.memHole;

    s_params.wOrder.setup      = true;
    s_params.wOrder.mode       = CPU_K6_WRITEORDER_ALL_EXCEPT_UC_WC;

    s_params.l1Cache.setup     = true;
    s_params.l1Cache.enable    = true;

    s_params.l2Cache.setup     = (s_sysInfo.thisCPU == K6_III || s_sysInfo.thisCPU == K6_PLUS);
    s_params.l2Cache.enable    = true;

    retPrintErrorIf(k6init_findAndAddPCIFBsToMTRRConfig() == false, "PCI/AGP FB detection failed! Skipping...", 0);
    retPrintErrorIf(k6init_findAndAddLFBsToMTRRConfig() == false,   "LFB detection failed! Skipping...", 0);
    retPrintErrorIf(s_params.wAlloc.setup == false,                 "Memory detection failed! Skipping Write Allocate.", 0);
    return true;
}

static const char k6init_appDescription[] =
    "http://github.com/oerg866/k6init\n"
    "\n"
    "K6INIT is a driver for MS-DOS that lets you configure special features of\n"
    "AMD K6-2/2+/III/III+ processors, similar to FASTVID on Pentium systems.\n"
    "\n"
    "It works on Chomper Extended (CXT) K6-2 chips or later.\n"
    "In contrast to other tools, K6INIT can be loaded from CONFIG.SYS, so it works\n"
    "even with an extended memory manager (such as EMM386) installed.\n"
    "\n"
    "If called with the /auto parameter, it does the following:\n"
    "- Finds linear frame buffer memory and sets up write combining for it\n"
    "- Enables Write Allocate for the entire system memory range\n"
    "- Enables Write Ordering except for uncacheable / write-combined regions\n"
    "\n"
    "This can be altered and overridden with many command line parameters.\n"
    "\n"
    "K6INIT was built using:\n"
    "LIB866D DOS Real-Mode Software Development Library\n"
    "http://github.com/oerg866/lib866d\n";

static const args_arg k6init_args[] = {
    ARGS_HEADER(k6init_versionString, k6init_appDescription),
    ARGS_USAGE("help", "Prints parameter list"),

    { "status",     NULL,               "Display current program status.",                      ARG_FLAG,               NULL,                       NULL },

    { "auto",       NULL,               "Fully automated setup (See above.)",                   ARG_FLAG,               &s_params.autoSetup,        NULL },

    { "mtrr",       "offset,size,wc,uc","Enable Write Combining for given range",               ARG_ARRAY(ARG_U32, 4),  s_MTRRCfgQueue,             k6init_argAddMTRR },
                            ARGS_EXPLAIN("offset: linear offset (e.g. 0xE0000000)"),
                            ARGS_EXPLAIN("size:   length in KILOBYTES (e.g. 8192)"),
                            ARGS_EXPLAIN("wc:     '1': Region is write-combine"),
                            ARGS_EXPLAIN("uc:     '1': Region is uncacheable"),
                            ARGS_EXPLAIN("NOTE - /mtrr can be be used twice."),
                            ARGS_EXPLAIN("NOTE - Will discard any MTRRs configured before"),
                            ARGS_EXPLAIN("running this program."),

    { "lfb",        NULL,               "Find and enable Write Combine for Linear Frame Buffer",ARG_FLAG,               NULL,                       k6init_argAddLFBMTRR },

    { "pci",        NULL,               "Find and enable Write Combine for Frame Buffers",      ARG_FLAG,               NULL,                       k6init_argAddPCIMTRR },
                            ARGS_EXPLAIN("exposed by PCI/AGP cards (experimental)"),

    { "vga",        NULL,               "Enables Write Combine for the VGA memory region",      ARG_FLAG,               NULL,                       k6init_argAddVGAMTRR },
                            ARGS_EXPLAIN("(A0000-BFFFF). WARNING: Potentially unsafe."),
                            ARGS_EXPLAIN("You MUST NOT use this memory region for UMBs."),
                            ARGS_EXPLAIN("This parameter is equivalent to /wc:0xA0000,128,1,0"),

    { "nomtrr",     NULL,               "Disables Memory Type Range Registers completely",      ARG_FLAG,               NULL,                       k6init_argClearMTRRs },
                            ARGS_EXPLAIN("Clears any MTRRs, including Write-Combine and"),
                            ARGS_EXPLAIN("Uncacheable regions."),

    { "wa",         "size",             "Configure Write Allocate",                             ARG_U32,                &s_params.wAlloc.size,      k6init_argWriteAllocate },
                            ARGS_EXPLAIN("size: Memory size in KB"),
                            ARGS_EXPLAIN("Set this to 0 to disable Write Allocate completely."),

    { "wahole",     NULL,               "Force 15-16M Memory Hole Skipping for Write Allocate", ARG_FLAG,               &s_params.wAlloc.hole,      k6init_argForceWAHole },
                            ARGS_EXPLAIN("K6INIT detects the memory hole automatically,"),
                            ARGS_EXPLAIN("but you can use this parameter to force it."),

    { "wo",         "mode",             "Configure Write Order Mode",                           ARG_U8,                 &s_params.wOrder.mode,      k6init_argWriteOrder },
                            ARGS_EXPLAIN("mode is a single digit to indicate the mode:"),
                            ARGS_EXPLAIN("0 - All Memory Regions (Slow)"),
                            ARGS_EXPLAIN("1 - All except Uncacheable/Write-Combined (Fast)"),
                            ARGS_EXPLAIN("2 - No Memory Regions (Fastest)"),

    { "multi",      "x.y",              "Configure CPU Frequency Multiplier",                   ARG_STRING(3),          s_multiToParse,             k6init_argAddMulti },
                            ARGS_EXPLAIN("x: integral part of multiplier"),
                            ARGS_EXPLAIN("y: fractional part of multiplier"),
                            ARGS_EXPLAIN("IMPORTANT: Requires K6-2+ or K6-III+ CPU!"),
                            ARGS_EXPLAIN("Example: /multi:5.5"),

    { "l1",         "1/0",              "Enable/Disable Level 1 cache",                         ARG_BOOL,               &s_params.l1Cache.enable,   k6init_argAddL1 },
    { "l2",         "1/0",              "Enable/Disable Level 2 cache",                         ARG_BOOL,               &s_params.l2Cache.enable,   k6init_argAddL2 },
                            ARGS_EXPLAIN("NOTE: Only K6-2+ and K6-III+ have on-die L2 Cache!"),
};

int main(int argc, char *argv[]) {
    const char *writeOrderModeStrings[] = { "0, All Memory Regions",
                                            "1, All except Uncacheable/Write-Combined",
                                            "2, No Memory Regions" };
    args_ParseError argErr;
    u8              logoColor = VGACON_COLOR_GREEN;
    bool            ok = true;

    k6init_populateSysInfo();

    memset(&s_params, 0, sizeof(s_params));
    argErr = args_parseAllArgs(argc, (const char **) argv, k6init_args, ARRAY_SIZE(k6init_args));

    if (argErr == ARGS_USAGE_PRINTED)               { return 0; }

    if      (s_sysInfo.criticalError == true)       { logoColor = VGACON_COLOR_RED; }
    else if (s_sysInfo.thisCPU == UNSUPPORTED_CPU)  { logoColor = VGACON_COLOR_LRED; }
    else if (argErr == ARGS_NO_ARGUMENTS)           { logoColor = VGACON_COLOR_YELLO; }
    else if (argErr != ARGS_SUCCESS)                { logoColor = VGACON_COLOR_BROWN; }

    k6init_printAppLogoSysInfo(logoColor);

    if (s_sysInfo.thisCPU == UNSUPPORTED_CPU) {
        putchar(' ');
        vgacon_printColorString("Please run this program on an AMD-K6-2 CXT/K6-2+/K6-III/K6-III+!", VGACON_COLOR_LRED, VGACON_COLOR_BLACK, true);
        printf("\n");
        return -1;
    } else if (argErr == ARGS_NO_ARGUMENTS) {
        vgacon_printWarning("No arguments given. Use /help for more information.\n");
        return 1;
    } else if (argErr != ARGS_SUCCESS) {
        vgacon_printError("User input error, quitting...\n");
        return (int) argErr;
    }

    /* Do actual execution of requested actions */
    ok &= k6init_doIfSetupAndPrint(s_params.autoSetup,      k6init_autoSetup,       "Preparing automatic configuration");
    ok &= k6init_doIfSetupAndPrint(s_params.mtrr.setup,     k6init_doMTRRCfg,       "Set MTRR Config");
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
    if (!ok)
        vgacon_printWarning("Summary: Some actions failed!\n");

    return (ok == true) ? 0 : -1;
}
