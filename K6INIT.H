#include "sys.h"
#include "vesabios.h"
#include "cpu_k6.h"

/* This structure holds all the arguments passed to the program. */
typedef struct {
    bool        quiet;
    bool        verbose;
    bool        printBARs;
    bool        chipsetTweaks;
    /* MTRR Config */
    struct {    bool setup;
                bool clear;
                bool pci;
                bool lfb;
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
    /* Data Prefetch Config */
    struct {    bool setup;
                bool enable;                        } prefetch;
} k6init_Parameters;

typedef enum {
    K6 = 0,
    K6_2,
    K6_2_CXT,
    K6_III,
    K6_PLUS,
    UNSUPPORTED_CPU
} k6init_CPUType;

typedef struct {
    k6init_CPUType type;
    const char *name;
    bool supportsEFER;
    bool supportsCxtFeatures;
    bool supportsL2;
    bool supportsMulti;
} k6init_CPUCaps;

/* This structure holds all the detected system info we may need for this program. */
typedef struct {
    sys_CPUIDVersionInfo        cpuidInfo;
    char                        cpuidString[13];
    k6init_CPUCaps              cpu;
    cpu_K6_MemoryTypeRangeRegs  mtrrs;
    cpu_K6_WriteAllocateConfig  whcr;
    u32                         memSize;
    bool                        memHole;
    bool                        vesaBIOSPresent;
    vesa_BiosInfo               vesaBiosInfo;
    bool                        L1CacheEnabled;
    bool                        L2CacheEnabled;
    bool                        criticalError;
} k6init_SysInfo;


#define retPrintErrorIf(condition, message, value) if (condition) { vgacon_printError(message "\n", value); return false; }

bool chipset_autoConfig(const k6init_Parameters *params, const k6init_SysInfo *sysInfo);