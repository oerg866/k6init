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

static int isKnownLFB(unsigned long *lfbList, unsigned long lfbToCheck)
{
    int i;

    for (i = 0; i < k6_maximumMTRRCount; i++) {
        if (lfbList[i] == lfbToCheck) {
            return 1;
        }
    }
    return 0;
}

static int findLFBs(mtrrConfigInfo *mtrrConfig)
{
    vbeInfo *vbeInfoPtr;
    vbeModeInfo *vbeModeInfoPtr;
    unsigned short ret;
    int vbeVersionMajor, vbeVersionMinor;
    char oemVersionString[16+1];
    unsigned short i;
    unsigned short currentMode;

    unsigned long videoMemorySize = 0UL;

    int foundLFB = 0;

    // Allocate memory for VBE (Mode) Info structures

    vbeInfoPtr = malloc(sizeof(vbeInfo));
    vbeModeInfoPtr = malloc(sizeof(vbeModeInfo));

    // Do some sanity checks first.

    if ((vbeInfoPtr == NULL) || (vbeModeInfoPtr == NULL)) {
        printf("ERROR> could not allocate memory for VBE Info Structures.\n");
        goto error;
    }

    if (mtrrConfig == NULL) {
        printf("mtrrConfig is NULL! Aborting...\n");
        goto error;
    }

    if (mtrrConfig->mtrrCount == k6_maximumMTRRCount) {
        printf("No more Write Combine / MTRR slots available!");
        goto error;
    }

    printf("Attempting to find Linear Frame Buffer (LFB) region(s)...\n");
    printf("Probing VGA BIOS for VBE support...\n\n");

    // Init the memory

    memset(vbeInfoPtr, 0, sizeof(vbeInfo));
    memset(vbeModeInfoPtr, 0, sizeof(vbeModeInfo));
    memset(oemVersionString, 0, sizeof(oemVersionString));

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

    _fstrncpy((char _far*)oemVersionString,
              (char _far*) vbeInfoPtr->oemStringPtr,
              16);

    printf("VESA BIOS OEM Version String: %s\n", oemVersionString);

    // If we have VESA version before 2.xx, we have to stop because
    // VESA only introduced LFBs starting at 2.xx.

    if (vbeVersionMajor < 2) {
        printf("VBE Version before 2.00. No LFB detection possible.\n");
        goto error;
    }

    // Query all video modes to find those with LFBs.

    printf("Querying VESA modes to find LFB address...\n\n");

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

                mtrrConfigInfoAppend(mtrrConfig, vbeModeInfoPtr->framebuffer,
                    videoMemorySize);

                foundLFB++;
            }

            // If we exceed two LFBs, we stop processing the VESA modes.

            if (mtrrConfig->mtrrCount == k6_maximumMTRRCount) {
                break;
            }
        }
    }

    free(vbeModeInfoPtr);
    free(vbeInfoPtr);


    return foundLFB;

// TODO: Make this more elegant :/
error:
    free(vbeModeInfoPtr);
    free(vbeInfoPtr);
    return -1;
}

static unsigned long getMTRRRangeForSize(unsigned long size)
{
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

static int setupMTRRs(mtrrConfigInfo *mtrrConfig)
{
    unsigned short i = 0;
    unsigned long mtrrValue = 0UL;
    unsigned long mtrrMask = 0UL;
    unsigned long mtrrAddress = 0UL;


    if (mtrrConfig == NULL) {
        printf("mtrrConfig is NULL! Aborting...\n");
        return 0;
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

        printf("Configuring MTRR%u: %08lx, size %lu -> %08lx \n",
            i,
            mtrrConfig->mtrrs[i],
            mtrrConfig->mtrrSizes[i],
            mtrrValue);

        // Now configure actual MTRR register
        k6_setMTRR(i, mtrrValue);
    }

    // Newline for readability

    printf("\n");

    return 1;
}

void mtrrConfigInfoAppend(mtrrConfigInfo *dst,
                          unsigned long address,
                          unsigned long size)
// Adds a MTRR Write combine range to the mtrrConfigInfo struct.
{
    if (dst == NULL)
        return;

    if (dst->mtrrCount == k6_maximumMTRRCount) {
        printf("Attempting to add MTRR range but maximum MTRR count already reached!\n");
        return;
    }

    dst->mtrrs[dst->mtrrCount] = address;
    dst->mtrrSizes[dst->mtrrCount] = size;
    dst->mtrrCount++;
}

int configureWriteCombining(mtrrConfigInfo *mtrrsToConfigure,
                            int doLfbScan)
// Configures Write Combining with the given parameters in mtrrsToConfigure.
// if doLfbScan = 1 then the VESA BIOS will be probed for the LFB region.
// this is the default behavior.
// returns -1 or 0 on error.
{
    int vbeLFBCount = 0;

    if (mtrrsToConfigure == NULL) {
        printf("mtrrsToConfigure NULL pointer!!\n");
        return 0;
    }

    // If doLfbScan is set, we scan for LFBs via VBE

    if (doLfbScan) {
        vbeLFBCount = findLFBs(mtrrsToConfigure);

        if (vbeLFBCount >= 0) {
            printf("LFB MTRRs configured via VESA BIOS: %u\n", vbeLFBCount);
        } else {
            // Error condition
            printf("ERROR probing VESA BIOS! Cannot auto-detect LFB!\n");
        }
    }

    // Newline for readability's sake...

    printf("\n");

    // Now we setup the MTRRs in the CPU.

    return setupMTRRs(mtrrsToConfigure);

}

unsigned long getMemorySize(void)
{
    // Basically a wrapper function for k6_getMemorySize
    // which is cumbersome to use.
    // The reason for that is that asm PROCs cannot return
    // 32-bit values for some reason. Old compilers innit'

    unsigned long memSizeBelow16M = 0UL;
    unsigned long memSizeAbove16M = 0UL;
    unsigned long memSizeTotal = 0UL;
    unsigned short ret;

    ret = k6_getMemorySize((unsigned long _far*) &memSizeBelow16M,
            (unsigned long _far*) &memSizeAbove16M);

    if (!ret) {
        return 0;
    }


    if (memSizeAbove16M == 0) {
        // If we have <=16MB
        memSizeTotal = memSizeBelow16M + (1UL * 1024UL * 1024UL);
    } else {
        memSizeTotal = memSizeAbove16M + (16UL * 1024UL * 1024UL);
    }

    return memSizeTotal;
}

static int hasMemoryHole(void)
// This function finds out whether or not we have
// a 15-16M memory hole.
// -1 = error
{

    unsigned long memSizeBelow16M = 0UL;
    unsigned long memSizeAbove16M = 0UL;

    unsigned short ret;

    ret = k6_getMemorySize((unsigned long _far*) &memSizeBelow16M,
            (unsigned long _far*) &memSizeAbove16M);

    if (!ret) {
        return -1;
    }

    // If the 15M memory area from 1 - 16 MB is actually 14M or less,
    // we assume a memory hole.

    return (memSizeBelow16M <= (14UL * 1024UL * 1024UL));
}

void showMemoryInfo(void)
// Displays Memory Size and 15M-16M-Hole information.
{
    unsigned long memorySize = 0UL;
    int memoryHole = 0;

    memorySize = getMemorySize();

    printf("\n");

    if (memorySize == 0) {
        printf("ERROR OBTAINING SYSTEM MEMORY INFORMATION!!\n\n");
        return;
    }

    memorySize = memorySize >> 10UL;

    memoryHole = hasMemoryHole();

    printf("System memory information:\n");
    printf("        Installed system Memory: %lu KiB\n", memorySize);
    printf("        Has 15M-16M Memory Hole: ");

    if (memoryHole) {
        printf("YES\n");
    } else if (memoryHole == 0) {
        printf("NO\n");
    } else {
        printf("ERROR.\n");
    }

    printf("\n");
}

void setWriteAllocateManual(unsigned long writeAllocateMemorySize,
                            int enableForMemoryHole)
// Sets write allocation for the given parameters
// The size is in bytes.
{
    // 4 MiB mask: 0xFFC00000
    // Memory hole bit: 0x00010000

    // We need to increment this value by 1 or rather 0x00400000,
    // else the last 4MB won't be inside the range!

    unsigned long writeAllocateReg = (writeAllocateMemorySize + 0x00400000UL)
                                      & 0xFFC00000UL;

    printf("Enabling Write Allocate for 1 - %lu MiB, ",
        writeAllocateReg >> 20UL);

    if (enableForMemoryHole) {
        writeAllocateReg = writeAllocateReg | 0x00010000UL;
        printf("including area between 15-16M.\n");
    } else {
        printf("excluding 15-16M memory hole.\n");
    }

    printf("Setting Write Allocate WHCR Register: %08lx\n", writeAllocateReg);

    k6_setWriteAllocate(writeAllocateReg);

}

void setWriteAllocateForSystemRAM(void)
// Attempts to auto-detect the available sytem memory and sets up Write
// Allocate for it. This function is called by default
{
    unsigned long writeAllocateMemorySize;
    int systemHasMemoryHole;

    writeAllocateMemorySize = getMemorySize();
    systemHasMemoryHole = hasMemoryHole();

    // Leave if we have an error in detection

    if ((writeAllocateMemorySize == 0) || (systemHasMemoryHole < 0)) {
        printf("ERROR getting memory info! Not setting Write allocate.\n");
        return;
    }

    // Make sure to negate the memory hole var, since it needs to be 1 if
    // we DON'T have a memory hole to enable write allocate for the 15-16M
    // region!!

    setWriteAllocateManual(writeAllocateMemorySize, !systemHasMemoryHole);

}