/*
 *  VESA BIOS Extensions support header
 */

#ifndef __VESA_VBE_H
#define __VESA_VBE_H

// Info courtesy of http://wiki.osdev.org/Getting_VBE_Mode_Info

#pragma pack(1)

typedef struct
{
    char vbeSignature[4];               // "VESA"
    unsigned short vbeVersion;          // 0x0300 for VBE 3.0
    unsigned long _far *                // FAR Pointer to OEM String
        oemStringPtr;
    unsigned char capabilities[4];      // ??
    unsigned short _far *
        videoModeListPtr;               // FAR Pointer to video mode list
    unsigned short totalMemory;         // # of 64KiB blocks

    unsigned char reserved[512 - 16];   // Padding for mode list

} vbeInfo;

#define vbeMaximumModeEntries ((512-16)/2)


typedef struct
{
    unsigned short supportedInHardware : 1;
    unsigned short reserved : 1; 
    unsigned short ttyBIOSSupport : 1;
    unsigned short isColorMode : 1;
    unsigned short isGraphicsMode : 1;
    unsigned short isVGACompatible : 1;
    unsigned short hasVGACompatibleWindowedMode : 1;
    unsigned short hasLFB : 1;
    unsigned short hasDoubleScan : 1;
    unsigned short hasInterlaced : 1;
    unsigned short hasHWTripleBuffer : 1;
    unsigned short hasHWStereoscopic : 1;
    unsigned short hasDualDisplayStartAddress : 1;
    unsigned short reserved2 : 3;
} vbeModeAttributes;

typedef struct
{
    vbeModeAttributes attributes;       // deprecated, only bit 7 relevant
                                        // indicates the mode supports a linear frame buffer.
    unsigned char window_a;             // deprecated
    unsigned char window_b;             // deprecated
    unsigned short granularity;         // deprecated; used while calculating bank numbers
    unsigned short window_size;
    unsigned short segment_a;
    unsigned short segment_b;
    unsigned long win_func_ptr;         // deprecated; used to switch banks from protected mode without returning to real mode
    unsigned short pitch;               // number of bytes per horizontal line
    unsigned short width;               // width in pixels
    unsigned short height;              // height in pixels
    unsigned char w_char;               // unused...
    unsigned char y_char;               // ...
    unsigned char planes;
    unsigned char bpp;                  // bits per pixel in this mode
    unsigned char banks;                // deprecated; total number of banks in this mode
    unsigned char memory_model;
    unsigned char bank_size;            // deprecated; size of a bank, almost always 64 KB but may be 16 KB...
    unsigned char image_pages;
    unsigned char reserved0;

    unsigned char red_mask;
    unsigned char red_position;
    unsigned char green_mask;
    unsigned char green_position;
    unsigned char blue_mask;
    unsigned char blue_position;
    unsigned char reserved_mask;
    unsigned char reserved_position;
    unsigned char direct_color_attributes;

    unsigned long framebuffer;          // physical address of the linear frame buffer; write here to draw to the screen
    unsigned long off_screen_mem_off;
    unsigned short off_screen_mem_size; // size of memory in the framebuffer but not being displayed on the screen

    unsigned char reserved1[206];
} vbeModeInfo;

#pragma pack()

#endif
