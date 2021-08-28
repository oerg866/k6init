
/* Commandline processing helper code */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "k6cmdl.h"

static int printParseError(char *str, char *errorLocation)
// Prints a parsing error and returns -1.
// Parameter 1 is the errorneous string
// Parameter 2 is a pointer to where the problem is.
//        (NULL if this additional help is unwanted.)
{
    unsigned short errorLocIndex;
    unsigned short i;
    int printErrorLoc;

    printErrorLoc = ((errorLocation - str) > 0) || (errorLocation != NULL);

    errorLocIndex = (unsigned short) errorLocation - (unsigned short) str;

    if (str != NULL) {
        printf("ERROR Parsing string: '%s'\n", str);

        if (printErrorLoc) {
            printf("                       ");
            for (i = 0; i < errorLocIndex; i++) {
                putchar(' ');
            }
            printf("^ here\n");
        }
    } else {
        printf("ERROR - NULL string.\n");
    }
    return 0;
}

static int nullCheck(void *addressToCheck)
// Checks if a pointer is NULL.
// returns: 1  if pointer is not NULL
//          0  if pointer is NULL
{
    if (addressToCheck == NULL) {
        printf("NULL pointer error!\n");
    }
    return (addressToCheck != NULL);
}

void toLowercase(char *str)
// Converts a string to lowercase.
{
    if (!nullCheck(str))
        return;

    while (*str != '\0') {
        *str = tolower(*str);
        str++;
    }
}

int stringStartsWith(char *string1, char *string2)
// checks if string1 starts with string2.
//
// returns: 1  if string 1 starts with string2.
//          0  if string 1 doesn't start with string2
//          -1 if there's a null pointer or string 1 is too short.
{

    if (!nullCheck(string1) || !nullCheck(string2)) {
        return 0;
    }

    // Check if string is too short

    if (strlen(string1) < strlen(string2)) {
        return 0;
    }

    // Return result
    return (memcmp(string1, string2, strlen(string2)) == 0);
}

int stringLongerThan(char *string1, char *string2)
// checks if string1 is longer than string2.
// returns: 1  if string 1 is longer than string 2.
//          0  if it isn't (or there's an error)
{
    if (!nullCheck(string1) || !nullCheck(string2)) {
        return 0;
    }

    return (strlen(string1) > strlen(string2));
}

void printUsageInfo()
// Prints the usage info.
{
    printf("%s", k6cmdl_usage_info);
}

int getMtrrValues(char *str, unsigned long *address, unsigned long *size)
// Populates address and size variables from a string of format:
// /wc:lx,h
// where    lx = address, unsigned long hexadecimal
//          lu = size in KILOBYTES!!, unsigned long integer
// returns: 1 on success
//          0 if there was a problem parsing the string
{
    char *param = str;
    char *paramEnd = NULL;

    // If the string is too short

    if (!stringLongerThan(param, "/wc:")) {
        return printParseError(str, str + strlen(str));
    }

    // advance param pointer to after the initial token

    param += strlen("/wc:");

    // get first param, unsigned long base 16

    *address = strtoul(param, &paramEnd, 16);

    // now there should be a comma and there should be more after that.

    if ((paramEnd[0] != ',') || (strlen(paramEnd) < 1)) {
        return printParseError(str, paramEnd);
    }

    param = paramEnd + 1;

    // get second param, unsigned long base 10, convert from KiB to B

    *size = strtoul(param, &paramEnd, 10) << 10;

    // If there was an error during the last conversion or
    // there's garbage at the end of the line, we still return an error.

    if (paramEnd[0] == '\0') {
        return 1;
    } else {
        return printParseError(str, paramEnd);
    }

}

int getWriteOrderValues(char *str, int *setupMode)
// Populates the write order bitfield value from a string of format:
// /wo:u
//
// where    u = a digit from 0 to 2 for the mode
//              (setupMode becomes the digit in this case)
//              or 'n' to skip this configuration.
//              (setupMode becomes -1 in this case)
//
// returns: 1 on success
//          0 if there was a problem parsing the string
{
    char *param = str;
    char *paramEnd = NULL;

    // If the string is too short

    if (!stringLongerThan(param, "/wo:")) {
        return printParseError(str, str);
    }

    // advance param pointer to after the initial token

    param += strlen("/wo:");

    // There should only one character afterwards

    if (strlen(param) != 1) {
        return printParseError(str, param);
    }

    // check if character is valid

    if ((param[0] >= '0') && (param[0] <= '2')) {
        *setupMode = atoi(param);
    } else if (param[0] == 'n') {
        *setupMode = -1;
    } else {
        return printParseError(str, param);
    }

    return 1;
}

int getWriteAllocateValues(char *str, int *setupMode,
                           unsigned long *waMemorySize, int *waHasMemoryHole)
// Populates address and size variables from a string of format:
// /wa:lu,h
// where    lu = size in KILOBYTES!!, unsigned long integer
//          h  = char which indicates presence of 15-16M hole,
//               either 'y' or 'n'
//
// In this case setupMode will become 1 (manual setup)
//
// OR
// /wa:n    skips setup of getWriteAllocate entirely
// returns: 1 on success
//          0 if there was a problem parsing the string
//
// In this case setupMode will become -1 (don't config).
{
    char *param = str;
    char *paramEnd = NULL;

    // If the string is too short, we abort.

    if (!stringLongerThan(param, "/wa:"))
    {
        return printParseError(str, str);
    }

    // advance param pointer to after the initial token

    param += strlen("/wa:");

    // if we just have a 'n' following, we get out and set it to skip
    // Write Allocate config entirely.

    if ((strlen(param) == 1) && (param[0] == 'n')) {
        *setupMode = -1;
        return 1;
    }

    // get first param, unsigned long base 16, convert KiB to B

    *waMemorySize = strtoul(param, &paramEnd, 10) << 10;

    // now there should be a comma and one more character after that.

    if ((paramEnd[0] != ',') || (strlen(paramEnd) != 2)) {
        return printParseError(str, paramEnd);
    }

    param = paramEnd + 1;

    // now check the memory hole parameter

    if (param[0] == 'y') {
        *waHasMemoryHole = 1;
    } else if (param[0] == 'n') {
        *waHasMemoryHole = 0;
    } else {
        // some random character is here, quit.
        return printParseError(str, param);
    }

    // Everything went fine, setupMode indicates manual setup, return true.

    *setupMode = 1;
    return 1;
}

int getMultiplierValues(char *str, int *setupMode, unsigned short *multiplierValueIndex)
// Populates multiplier value index based on an input string of the format
// /multi:x.y
// where    x = multiplier integral digit, between '2' and '6'
//          y = multiplier fractional digit, either '0' or '5'
{
    char *param = str;
    char *paramEnd = NULL;

    unsigned short integral = 0;
    unsigned short fractional = 0;

    // If the string is too short, we abort.

    if (!stringLongerThan(param, "/multi:")) {
        return printParseError(str, str);
    }

    // advance param pointer to after the initial token

    param += strlen("/multi:");

    // The remainder must be 3 digits long and have a dot in the middle.

    if (strlen(param) != 3) {
        return printParseError(str, param);
    } else if (param[1] != '.') {
        return printParseError(str, param);
    }

    integral = (unsigned short)(param[0] - '0');
    fractional = (unsigned short)(param[2] - '0');

    // Check for valid values and report errors if invalid

    if ((integral < 2) || (integral > 6)) {
        return printParseError(str, param + 0);
    } else if ((fractional != 0) && (fractional != 5)) {
        return printParseError(str, param + 2);
    }

    // Calculate actual index. Multiply integral by 2, and offset by 1
    // if it's a ".5" value. This value is used as an index to
    // k6_multiplierValues in K6.H

    *multiplierValueIndex = (integral << 1) + (fractional == 5);

    *setupMode = 1;
    return 1;
}
