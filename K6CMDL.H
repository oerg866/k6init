#ifndef __K6CMDL_H
#define __K6CMDL_H

static const char k6cmdl_usage_info[] = {
    "Usage (config.sys):   DEVICE=K6INIT.EXE <parameters>\n"
    "Usage (command line): K6INIT.EXE <parameters>\n"
    "\n"
    "Parameters are OPTIONAL. Use them at your own risk! Available options:\n"
    "  /help                                                               \n"
    "       Prints this list :-)                                           \n"
    "  /wc:<address>,<size>                                                \n"
    "       Enables Write Combining for a given range.                     \n"
    "  /wcdisable                                                          \n"
    "       Disables write combining completely.                           \n"
    "  /nolfbscan                                                          \n"
    "       Disables automatic scan for GPU Memory Size and LFB regions.   \n"
    "  /vga                                                                \n"
    "       Enables Wirte Combining for the VGA memory range (A0000-BFFFF).\n"
    "  /wa:n                                                               \n"
    "       Disables configuration of Write Allocate.                      \n"
    "  /wa:<size>,<hole>                                                   \n"
    "       Manual Setup of Write Allocate.                                \n"
    "  /wo:<mode>                                                          \n"
    "       Manual Setup of Write Ordering mode.                           \n"
    "  /multi:<x>.<y>                                                      \n"
    "       Sets the CPU multiplier (K6-2+/III+ ONLY!)                     \n"
    "  Refer to READNE.MD for more details.\n"
};

void toLowercase(char *str);
int stringStartsWith(char *string1, char*string2);
void printUsageInfo();
int getMtrrValues(char *str, unsigned long *address, unsigned long *size);
int getWriteAllocateValues(char *str, int *setupMode,
                           unsigned long *waMemorySize, int *waHasMemoryHole);
int getWriteOrderValues(char *str, int *setupMode);
int getMultiplierValues(char *str, int *setupMode, unsigned short *multiplierValueIndex);

#endif
