
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

#include <keepc.h>
#include <standard.h>

#include "k6.h"

/*  Hard error trap... I have no idea why this is here, but the
    driver hybrid ASM+C example from DOS internals had this in it,
    so I am keeping it :P */

static int _far HardErrorTrap (void)
{
    return (_HARDERR_FAIL);
}

static char k_program_name [] = "K6INIT";
static int k_version_major = 0;
static int k_version_minor = 2;
static char k_copyright_text [] = "(C) 2021 Eric \"oerg866\" Voirin";
static char k_contact_info [] = "Contact me on Discord: EricV#9999";

/*  Message output  */

static void PutError (char *str, ...);

/*  ========================================================================  */

int main (int argc, char **argv)
{

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

    if (!enableWriteCombiningForLFBs()) {
        printf("Failed to enable Write Combining.\n");
        goto cleanup;
    }

    setWriteAllocateForSystemRAM();

    /*  Parse the command line.  */

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

