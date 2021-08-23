#include <string.h>
#include <malloc.h>
#include <dos.h>

#include "k6.h"

int checkAuthenticAMD(void)
{
    char cpuidString[12+1];  // 3 DWORDs + 1 null term byte 

    cpuidString[12] = 0x00;  // null terminator

    k6_getCPUIdentifier((char _far*)cpuidString);
    printf("CPU ID String: %s\n", cpuidString);

    return(memcmp(cpuidString, "AuthenticAMD", 12) == 0);

}

int checkSupportedCPU(void)
{
    cpuidProcessorType cpu;

    cpu = k6_getCPUProcessorType();

    printf("CPU Family %02lx model %02lx stepping %02lx\n", 
        cpu.family, 
        cpu.model, 
        cpu.stepping);

    // Supported CPUs
    // Family   Model     Stepping
    // 5        8         C         AMD K6-II(CXT)
    // 5        9         --        AMD K6-III
    // 5        D         --        AMD K6-II+ / K6-IIIE+

    if (cpu.family != 5) {
        printf("Unsupported CPU family!\n");
        return 0;
    }

    if (cpu.model == 0x08 && cpu.stepping == 0x0C)    {
        printf("AMD K6-II (CXT) detected!\n");
    } else if (cpu.model == 0x09) {
        printf("AMD K6-III detected!\n");
    } else if (cpu.model == 0x0D) {
        printf("AMD K6-II+ / K6-IIIE+ detected!\n");
    } else {
        printf("Unsupported CPU Model / Stepping!\n");
        return 0;
    }

    return 1;
}

static int isKnownLFB(unsigned long *lfbList, unsigned long lfbToCheck) {
    int i;

    for (i = 0; i < k6_maximumLFBCount; i++) {
        if (lfbList[i] == lfbToCheck) {
            return 1;
        }
    }
    return 0;
}

static int findLFBs(mtrrConfigInfo* mtrrConfig) {

    vbeInfo *vbeInfoPtr;
    vbeModeInfo *vbeModeInfoPtr;
    unsigned short ret;
    int vbeVersionMajor, vbeVersionMinor;
    char oemVersionString[16+1];
    unsigned short i;
    unsigned short currentMode;

    unsigned long videoMemorySize = 0L;

    int foundLFB = 0;

    if (mtrrConfig == NULL) {
        printf("mtrrConfig is NULL! Aborting...\n");
        goto error;
    }

    printf("Attempting to find Linear Frame Buffer (LFB) region(s)...\n");
    printf("Probing VGA BIOS for VBEs...\n");

    // Allocate memory for VBE (Mode) Info structures

    vbeInfoPtr = malloc(sizeof(vbeInfo));
    vbeModeInfoPtr = malloc(sizeof(vbeModeInfo));

    if ((vbeInfoPtr == NULL) || (vbeModeInfoPtr == NULL)) {
        printf("ERROR> could not allocate memory for VBE Info Structures.\n");
        goto error;
    }

    // Init the memory

    memset(vbeInfoPtr, 0, sizeof(vbeInfo));
    memset(vbeModeInfoPtr, 0, sizeof(vbeModeInfo));
    memset(oemVersionString, 0, sizeof(oemVersionString));
    memset(mtrrConfig, 0, sizeof(mtrrConfigInfo));

    // A VBE Info Block request must happen with the target block's 
    // Signature field set to "VBE2"

    memcpy(vbeInfoPtr->vbeSignature, "VBE2", 4);

    ret = k6_getVBEInfoBlock(vbeInfoPtr);

    if (ret != 0x004F) {
        printf("VESA call failed. Cannot automatically find LFBs.\n");
        goto error;
    }

    vbeVersionMajor = vbeInfoPtr->vbeVersion >> 8;
    vbeVersionMinor = vbeInfoPtr->vbeVersion & 0xFF;

    printf("VESA BIOS found, Signature: %.4s, VESA Version %.1x.%02x\n",
        vbeInfoPtr->vbeSignature,
        vbeVersionMajor,
        vbeVersionMinor);

    videoMemorySize = ((unsigned long) vbeInfoPtr->totalMemory) * 65536UL;

    printf("Total video memory: %lu Bytes\n", videoMemorySize);

    // Copy out OEM version string (Yay far pointers)

    _fstrncpy((char _far*)oemVersionString, vbeInfoPtr->oemStringPtr, 16);

    printf("VESA BIOS OEM Version String: %s\n", oemVersionString);

    // If we have VESA version before 2.xx, we have to stop because
    // VESA only introduced LFBs starting at 2.xx. 

    if (vbeVersionMajor < 2) {
        printf("VBE Version before 2.00. No LFB detection possible.\n");
        goto error;
    }

    // Query all video modes to find those with LFBs. 

    printf("Querying VESA modes to find LFB address...\n"); 

    for (i = 0; vbeInfoPtr->videoModeListPtr[i] != 0xFFFF; i++) {
        if (i > vbeMaximumModeEntries) {
            break;
        }

        currentMode = vbeInfoPtr->videoModeListPtr[i]; 

        ret = k6_getVBEModeInfo(currentMode, 
            (vbeModeInfo _far*) vbeModeInfoPtr);

        // ret = return code from query call

        /*

        TODO: Add loglevel based debug print function

        printf("ret: %04hx - Resolution: %ux%u - BPP: %u - Attr: %04x\n",
            ret,
            vbeModeInfoPtr->width,
            vbeModeInfoPtr->height,
            vbeModeInfoPtr->bpp,
            *(unsigned short*) &vbeModeInfoPtr->attributes);
        */

        // check if current mode has LFB capability

        if (vbeModeInfoPtr->attributes.hasLFB) {

            // we support a maximum of two LFB regions
            // (there are two MTRRs on the supported CPUs)

            // If this mode has not yet a known LFB address, save it.

            if (!isKnownLFB(mtrrConfig->mtrrs, vbeModeInfoPtr->framebuffer)) {
                printf("Found LFB%d at address %08lx.\n",
                    foundLFB,
                    vbeModeInfoPtr->framebuffer);

                mtrrConfig->mtrrs[foundLFB] = vbeModeInfoPtr->framebuffer;
                mtrrConfig->mtrrSizes[foundLFB] = videoMemorySize;
                foundLFB++;
            }

            // If we exceed two LFBs, we stop processing the VESA modes.

            if (foundLFB == k6_maximumMTRRCount) {
                break;
            }
        }
    }


    mtrrConfig->mtrrCount = foundLFB;

    free(vbeModeInfoPtr);
    free(vbeInfoPtr);


    return foundLFB;

// TODO: Make this more elegant :/
error:
    free(vbeModeInfoPtr); 
    free(vbeInfoPtr); 
    return -1;
}

static unsigned long getMTRRRangeForSize(unsigned long size) {
    // 15 bit mask

    /*
        111 1111 1111 1111 128K
        111 1111 1111 1110 256K
        111 1111 1111 1100 512K
        111 1111 1111 1000 1M
        ...
        100 0000 0000 0000 2G
        000 0000 0000 0000 4G
    */

    unsigned long mask = 0xFFFFFFFFUL;
    unsigned long shiftReference = 128UL * 1024UL;

    /*
        Shift the reference of 128K left (doubling it with each step
        and along with it the mask, until this reference is bigger
        than the requested size. The result & 7FFF is our mask.
    */

    while(shiftReference < size) {
        mask <<= 1UL;
        shiftReference <<= 1UL;
    }

    mask &= 0x00007FFFUL;
    return mask;
}

static int setupMTRRs(mtrrConfigInfo* mtrrConfig) {

    unsigned short i = 0;
    unsigned long mtrrValue = 0UL;
    unsigned long mtrrMask = 0UL;
    unsigned long mtrrAddress = 0UL;


    if (mtrrConfig == NULL) {
        printf("mtrrConfig is NULL! Aborting...\n");
        return -1;
    }

    for (i = 0; i < mtrrConfig->mtrrCount; i++) {

        mtrrValue = 0UL;

        // Set MTRR to Write Combining Memory Type (bit 1)
        mtrrValue |= 0x00000002UL;

        // Set Physical Base Address (bits 17 - 31)
        // Most significant 15 bits of the physical address.
        mtrrAddress = mtrrConfig->mtrrs[i];
        mtrrValue |= mtrrAddress & 0xFFFE0000UL;

        // Set Physical Address Mask (bits 2 - 16)
        mtrrMask = getMTRRRangeForSize(mtrrConfig->mtrrSizes[i]);
        mtrrMask <<= 2UL;
        mtrrValue |= mtrrMask;

        printf("Configuring MTRR0: %08lx, size %lu -> %08lx \n",
            mtrrConfig->mtrrs[i],
            mtrrConfig->mtrrSizes[i],
            mtrrValue);

        // Now configure actual MTRR register
        k6_setMTRR(i, mtrrValue);
    }

}

int enableWriteCombiningForLFBs(void) {

    mtrrConfigInfo mtrrConfig;
    int amountOfLFBs = 0;
    int result = 0;

    // Probe VESA BIOS first to find LFBs and VRAM size

    amountOfLFBs = findLFBs(&mtrrConfig);

    // Set up the processor MTRR registers for these LFBs.
    // TODO: If only 1 LFB is found, consider making an MTRR
    // config for legacy VGA memory region

    result = setupMTRRs(&mtrrConfig);

    return result;
}

