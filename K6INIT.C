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

static int findLFBs(mtrrConfigInfo* mtrrConfig) {

    vbeInfo *vbeInfoPtr;
    vbeModeInfo *vbeModeInfoPtr;
    unsigned short ret;
    int vbeVersionMajor, vbeVersionMinor;
    char oemVersionString[16+1];
    unsigned short i;
    unsigned short currentMode;

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

    } 

    free(vbeModeInfoPtr); 
    free(vbeInfoPtr); 
    return 1;

// TODO: Make this more elegant :/
error:
    free(vbeModeInfoPtr); 
    free(vbeInfoPtr); 
    return -1;
}

