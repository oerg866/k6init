
/*  ************************************************************************  *
 *                                   K6INIT                                   *
 *     AMD K6-II(CXT) / K6-II+ / K6-III+ initialization driver for MS-DOS     *
 *                            (C) 2021 Eric Voirin                            *
 *  ************************************************************************  */

#include <dos.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <keepc.h>
#include <standard.h>

#include "k6.h"
#include "k6cmdl.h"

/*  Hard error trap... I have no idea why this is here, but the
    driver hybrid ASM+C example from DOS internals had this in it,
    so I am keeping it :P */

static int _far HardErrorTrap (void)
{
    return (_HARDERR_FAIL);
}

static const char k_program_name [] = "K6INIT";
static const int k_version_major = 0;
static const int k_version_minor = 3;
static const char k_copyright_text [] = "(C) 2021 Eric \"oerg866\" Voirin";
static const char k_contact_info [] = "Contact me on Discord: EricV#9999";

/*  Message output  */

static void PutError (char *str, ...);

/*  ========================================================================  */

int main (int argc, char **argv)
{
    mtrrConfigInfo mtrrSetup;       // The MTRR regions to rwrite. By
                                    // default, we don't write anything.

    int writeOrderSetup = 1 ;       // Write order setup,
                                    // For value see k6init.h, except:
                                    // -1 = don't config

    int writeAllocateSetup = 0;     // Indicates user wants manual
                                    // Write Allocate setup
                                    // 1  = manual
                                    // 0  = autodetect
                                    // -1 = don't config

    int doLfbScan = 1;              // By default, we scan for LFB in VBE.

    int i;
    int ret = 1;

    unsigned long parsedMtrrAddr = 0UL;
    unsigned long parsedMtrrSize = 0UL;
    unsigned long parsedWASize = 0UL;
    int parsedWAMemoryHole = 0;

    memset(&mtrrSetup, 0, sizeof(mtrrConfigInfo));

    printf("%s Version %d.%02d\n", k_program_name, k_version_major, k_version_minor);
    printf("%s\n", k_copyright_text);
    printf("%s\n", k_contact_info);
    printf("===============================================================================\n");

    showMemoryInfo();

    if (!checkAuthenticAMD()) {
        printf("You don't have an AMD CPU. Aborting.\n");
        goto cleanup;
    }

    if (!checkSupportedCPU()) {
        printf("Aborting\n");
        goto cleanup;
    }

    printf("\n");

    for (i = 1; i < argc; i++) {
        // Parse an argument.
        // Let's make it lowercase first so we support both cases.

        toLowercase(argv[i]);

        if        (stringStartsWith(argv[i], "/wc:")) {

            // Write Combine Parameter
            ret = getMtrrValues(argv[i], &parsedMtrrAddr, &parsedMtrrSize);

            if (ret) {
                mtrrConfigInfoAppend(&mtrrSetup, parsedMtrrAddr,
                                     parsedMtrrSize);
            }

        } else if (stringStartsWith(argv[i], "/wcdisable")) {

            // Disable Write Combining compeltely in the CPU.

            disableWriteCombining();

        } else if (stringStartsWith(argv[i], "/nolfbscan")) {

            // Disable LFB Scan
            doLfbScan = 0;

        } else if (stringStartsWith(argv[i], "/vga")) {

            // Add Write Combining for VGA region.
            printf("Setting up Write Combine for VGA region.\n");
            mtrrConfigInfoAppend(&mtrrSetup, 0x000A0000UL, 131072UL);

        } else if (stringStartsWith(argv[i], "/wa:")) {

            // Set Write Allocate (disables automatic system scan)
            ret = getWriteAllocateValues(argv[i], &writeAllocateSetup,
                                         &parsedWASize, &parsedWAMemoryHole);

        } else if (stringStartsWith(argv[i], "/wo:")) {

            // Set Write Ordering Mode
            ret = getWriteOrderValues(argv[i], &writeOrderSetup);

        } else if (stringStartsWith(argv[i], "/help")) {

            // Print usage info. We exit after this.
            printUsageInfo();
            goto cleanup;

        } else {

            // Unknown parameter
            printf("Unknown command line parameter: %s\n", argv[i]);
            printf("Aborting.\n");
            goto cleanup;
        }

        if (!ret) {
            printf("Command line parsing error! Aborting.\n");
            goto cleanup;
        }

    }

    printf("\n");

    if (!doLfbScan) {
        printf("Disabling automatic VBE LFB scan.\n");
    }

    // Setup Write Allocate

    if (writeAllocateSetup == 1) {
        // Manual Setup, need to invert the presence of the memory hole
        // to ENABLE WA for that region.
        setWriteAllocateManual(parsedWASize, !parsedWAMemoryHole);
    } else if (writeAllocateSetup == 0) {
        // Autodetect memory and set WA
        setWriteAllocateForSystemRAM();
    } else {
        printf("Skipping setup of Write Allocate.\n");
    }

    // Setup Write Combining

    if (!configureWriteCombining(&mtrrSetup, doLfbScan)) {
        printf("Error configuring Write Combining. Aborting.\n");
        goto cleanup;
    }

    // Setup Write Ordering

    if (writeOrderSetup >= 0) {
        setWriteOrderMode(writeOrderSetup);
    } else {
        printf("Skipping setup of Write Ordering.\n");
    }

    goto cleanup;


    signal(SIGINT, SIG_IGN);
    _harderr(HardErrorTrap);

    /*  Tidy up.  */

cleanup:
    CloseAllFiles ();
    FreeEnvironment ();
    ExitKeepC (0, 0);
}

static void PutError (char *str, ...)
{
    va_list va_start (argptr, str);
    printf("\n%s error:  ", k_program_name);
    vprintf(str, argptr);
    printf("\n");
    va_end(argptr);
}

/*  ************************************************************************  */

