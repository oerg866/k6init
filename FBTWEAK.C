#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include "chipset.h"

#include "vesabios.h"
#include "vgacon.h"
#include "util.h"
#include "args.h"
#include "sys.h"
#include "pci.h"

#define __LIB866D_TAG__ "FBTWEAK"
#include "debug.h"

#define retPrintErrorIf(condition, message, value) if (condition) { vgacon_printError(message "\n", value); return false; }

static vesa_BiosInfo vesaBiosInfo;

static bool s_skipPci = false;
static bool s_skipVesa = false;
static bool s_doVga = false;
static bool s_noPrefetchOk = true;

static const char versionString[] = "FBTweak Version 0.1 - (C) 2026 Eric Voirin (oerg866)";

static const char appDescription[] =
    "http://github.com/oerg866/k6init\n"
    "\n"
    "FBTWEAK is a tool from the K6INIT universe that can enable Frame Buffer\n"
    "accelerations on some Socket 5 / 7 chipsets, even without a K6 family CPU.\n"
    "\n"
    "FBTWEAK was built with the LIB866D DOS Real-Mode Software Development Library\n"
    "http://github.com/oerg866/lib866d\n";


static const args_arg fbtweak_args[] = {
    ARGS_HEADER(versionString, appDescription),
    ARGS_USAGE("?", "Prints parameter list"),

    { "nopci",      NULL,               "Skip PCI Framebuffer Detection.",                      ARG_FLAG,               NULL,                       &s_skipPci,                 NULL },
    { "novesa",     NULL,               "Skip VESA Framebuffer Detection.",                     ARG_FLAG,               NULL,                       &s_skipVesa,                NULL },
    { "vga",        NULL,               "Enable VGA region acceleration.",                      ARG_FLAG,               NULL,                       &s_doVga,                   NULL },
                            ARGS_EXPLAIN("NOTE: Not supported by all chipsets."),
};


/* Finds VESA LFB address and enters it into the GFX Tweak Confic. Returns true on success. */
bool getVesaLfb(chipset_GfxTweakConfig *cfg) {
    vesa_ModeInfo   currentMode;
    bool            vesaBiosValid = vesa_getBiosInfo(&vesaBiosInfo) && vesa_isValidVesaBios(&vesaBiosInfo);
    u32             vramSizeKB = vesa_getVRAMSize(&vesaBiosInfo) / 1024UL;
    size_t          i;

    retPrintErrorIf(vesaBiosValid == false, "No VESA BIOS found, cannot scan for LFBs!", 0);

    vgacon_print("Scanning %u VESA modes for Linear Frame Buffers...\n", (u16) vesa_getModeCount(&vesaBiosInfo));

    for (i = 0; i < vesa_getModeCount(&vesaBiosInfo); i++) {
        retPrintErrorIf(false == vesa_getModeInfoByIndex(&vesaBiosInfo, &currentMode, i),
            "Failed to get info for VESA mode 0x%x", i);

        /* Ignore if this has no LFB */
        if (!currentMode.attributes.hasLFB) {
            continue;
        }

        vgacon_printOK("Found Linear Frame Buffer at: 0x%08lx\n", currentMode.lfbAddress);
        cfg->setLfb = true;
        cfg->offset = currentMode.lfbAddress;
        cfg->sizeKB = vramSizeKB;
        return true;
    }
 
    vgacon_printWarning("No VESA Linear Frame Buffer found.\n");
 
    return false;
}

/* Finds VESA LFB address and enters it into the GFX Tweak Confic. Returns true on success. */
bool getPciAgpLfb(chipset_GfxTweakConfig *cfg, bool noPrefetchOk) {
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

        vgacon_printOK("Found Graphics Card, Vendor 0x%04x, Device 0x%04x\n", curDeviceInfo.vendor, curDeviceInfo.device);

        for (i = 0; i < PCI_BARS_MAX; i++) {
            if (curDeviceInfo.bars[i].type != PCI_BAR_MEMORY)               continue; /* Must be memory BAR */
            if (!curDeviceInfo.bars[i].prefetchable && !noPrefetchOk)       continue; /* Must be prefetchable */
            if (curDeviceInfo.bars[i].size < 1048576UL)                     continue; /* Must be at least 1MB */

            vgacon_printOK("Found PCI/AGP frame buffer at: 0x%08lx\n", curDeviceInfo.bars[i].address);

            cfg->setLfb = true;
            cfg->offset = curDeviceInfo.bars[i].address;
            cfg->sizeKB = curDeviceInfo.bars[i].size / 1024UL;

            free(curDevice);
            return true;
        }
    }

    if (curDevice != NULL)
        free(curDevice);

    vgacon_printWarning("No PCI/AGP LFBs found\n");
    return false;
}


int main(int argc, char *argv[]) {
    chipset_GfxTweakConfig  tweak;
    args_ParseError         argErr;
    bool                    ok = true;

    /* Privileged instructions cause GPFs on WINDOWS, so we exit. */
    if (sys_getWindowsMode() != OS_PURE_DOS) {
         vgacon_printError("FBTWEAK cannot run on Windows.\n");
         return -1;
    }

    memset(&tweak, 0, sizeof(tweak));
    argErr = args_parseAllArgs(argc, (const char **) argv, fbtweak_args, ARRAY_SIZE(fbtweak_args));

    if (argErr == ARGS_USAGE_PRINTED)               { return 0; }

    vgacon_print("%s\n", versionString);

    if (argErr != ARGS_SUCCESS && argErr != ARGS_NO_ARGUMENTS) {
        vgacon_printError("User input error, quitting...\n");
        return (int) argErr;
    }

    if (!tweak.setLfb && !s_skipVesa)   getVesaLfb(&tweak);
    if (!tweak.setLfb && !s_skipPci)    getPciAgpLfb(&tweak, s_noPrefetchOk);
    
    tweak.setVgaFb = s_doVga;

    return (int) chipset_doFramebufferTweaks(&tweak);
}

