/*
 *  K6INIT Helper Header
 */

#include <stdio.h>
#include <dos.h>

#include "vesa_vbe.h"

#ifndef __K6_H
#define __K6_H

// Constants

#define k6_processorTypeNONE            0   // Unsupported CPU
#define k6_processorTypeCXT             1   // AMD K6-2 Chomper Extended
#define k6_processorTypeK6_3            2   // AMD K6-3                    ^
#define k6_processorTypePLUS            3   // AMD K6-2+/III+ -- This supports chanigng

#define k6_maximumMTRRCount             2   // Amount of MTRR registers.

static const char k6_writeOrderModeStrings[][256] = {
    "All (Slow)",                                       // 0
    "All except Uncacheable/Write-Combined (Fast)",     // 1
    "None (Fastest)"                                    // 2
};

typedef struct {
    unsigned long stepping : 4, model : 4, family : 4, type : 2;
} cpuidProcessorType;

typedef struct {
    unsigned short mtrrCount;
    unsigned long mtrrs[k6_maximumMTRRCount];
    unsigned long mtrrSizes[k6_maximumMTRRCount];
} mtrrConfigInfo;

// k6.asm exports

extern unsigned short k6_getMemorySize(unsigned long _far *memorySizeBelow16M, unsigned long _far *memorySizeAbove16M);

extern void k6_setWriteAllocate(unsigned long value);

extern void k6_getCPUIdentifier(char _far *cpuidString);
extern cpuidProcessorType k6_getCPUProcessorType(void);

extern void k6_setWriteOrderMode(unsigned short writeOrderMode);
extern void k6_setMTRR(unsigned short index, unsigned long value);

extern unsigned short k6_getVBEInfoBlock(vbeInfo _far *vbeInfoBlockPtr);
extern unsigned short k6_getVBEModeInfo(unsigned short videoMode,
                                        vbeModeInfo _far *vbeModeInfoPtr);

// k6init functionality

unsigned long getMemorySize(void);
int checkAuthenticAMD(void);
int getSupportedCPUType(void);
int configureWriteCombining(mtrrConfigInfo *mtrrsToConfigure, int doLfbScan);
void mtrrConfigInfoAppend(mtrrConfigInfo *dst,
                          unsigned long address,
                          unsigned long size);
void disableWriteCombining(void);

// Memory / Write Allocate stuff

void showMemoryInfo(void);

void setWriteAllocateManual(unsigned long writeAllocateMemorySize,
                            int enableForMemoryHole);

void setWriteAllocateForSystemRAM(void);

void setWriteOrderMode(int writeOrderMode);


#endif
