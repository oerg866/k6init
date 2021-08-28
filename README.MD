# K6INIT

## What is this?

This is a driver for MS-DOS to replace `k6dos.sys` which is a bit useless and unflexible. It does not support the CXT versions of the K6-II.

On the newer K6-2/2+/3+ models there are features that benefit system performance in select applications greatly, such as games.

This driver sets Write allocation and Write Combining for the LFB memory on these models.

In the future it will also let you set the multiplier on K6-3 models.

I'm doing this because I want my K6 machine to be fast.

# Project Status
## What does it (not) do so-far?

- [x] Find and check for supported CPU types using `cpuid`
- [x] Find VESA BIOS and VESA modes
- [x] Find LFB address
- [x] Set MTRR to enable Write combining for LFB
- [x] Set Write Combine Mode
- [x] Detect total available system memory
- [x] Detect 15-16M Memory hole
- [x] Set write allocate for system memory
- [x] Set K6-2+/III+ Multiplier (untested, don't own this CPU)
- [ ] Set K6-2+/III+ voltage settings (not sure if there's a point)
- [ ] Enable/Disable CPU Cache


## Supported Processors

| CPU Type                | Family | Model | Stepping |
|-------------------------|--------|-------|----------|
| AMD K6-2 (CXT Core)     | 5      | 8     | C        |
| AMD K6-III              | 5      | 9     | *Any*    |
| AMD K6-2+ / AMD K6-III+ | 5      | D     | *Any*    |

# Running the Driver

Usage (config.sys):
`DEVICE=K6INIT.C <parameters>`

Usage (command line):
`K6INIT.EXE <parameters>`

All parameters are **OPTIONAL**. They should only be used by
knowledgeable users.

By default, the program sets the following CPU features:

- Write Ordering mode to 1 (All except UC/WC)
- Write Allocate for System memory (auto detected)
- Write Combining for Linear Frame Buffer(s) exposed by VESA BIOS
  (auto detected)

This configuration sequence can be changed using the command line parameters.

## Command Line Parameters
Valid parameters:
- `/help`

    Prints a (shortened) parameter list.

- `/wc:<address>,<size>`

    Enables Write Combining for a given range.

    - `address` Starting address for Write Combining.
                This is a 32-bit hexadecimal value.
                (e.g. `e0000000`)

    - `size`    Size of the write combining region in **kilobytes**.
                This is a decimal value. (e.g. `32768`)

    Example:  `/wc:e0000000,32768`

    **This parameter can be used twice.**

- `/wcdisable`

    Disables Write Combining completely. Overwrites any MTRR registers.

- `/nolfbscan`

    Disables automatic scan for GPU Memory Size and LFB regions.
    *Tip: Use this with `/wc` to set a manual region (or two)!*

- `/vga`

    Enables Wirte Combining for the VGA memory region (`A0000`-`BFFFF`).
    Not done by default because it is *potentially unsafe*.

    **WARNING**: If you enable this, you **MUST NOT** use this memory
    region for UMBs. Exclude it in your memory manager.

    This parameter is equivalent to `/wc:a0000,128`.

- `/wa:n`

    Disables configuration of Write Allocate.

- `/wa:<size>,<hole>`

    Manual Setup of Write Allocate.

    - `size` Write Allocate region limit (i.e. memory size.) in
             **kilobytes**.
             This is a decimal value (e.g. `131072`)

    - `hole` Can be `y` or `n` to indicate presence or absence
        of the 15-16M memory hole.

    Example: `/wa:131072:n`.

- `/wo:<mode>`

    Manual Setup of Write Ordering mode.

    - `mode` is a single digit to indicate the mode:

        - `0` for all memory regions (slow)
        - `1` for all except uncacheable / write combine
              memory regions (fast, **this is the default**)
        - `2` for none (fastest)
        - `n` skip configuration of Write Ordering completely.

    Example: `/wo:2`

- `/multi:<x>,<y>`

    Set the CPU bus frequency multiplier.

    - `x` integral part of the multiplier value, a digit from `2` to `6`

    - `y` fractional part of the multiplier value, either `0` or `5`

    Example: `/multi:5.5`

    **IMPRORTANT: This feature can only be used on K6-2+ and K6-III+ CPUs!**

    **WARNING: This feature is UNTESTED because I do not own a "+" CPU!
    Testers wanted!**


# Building

## Why in C?

Because I hate / suck at x86 assembly and I want flexibility, such as more complex(ish) command line parsing which is hard to do in ASM...

Normally you should really do this in assembly. But YOLO.
## Requirements

* MS-DOS 6.xx or 7.xx or Windows 9x
* Microsoft C / C++ Compiler Version 7.00
  * Also works with Microsoft Visual C++ 1.52, but not recommended because that's not really meant for development under pure DOS.
  * In this case you need a 32-Bit DOS Extender such as HX-DOS. The DOSXNT that is included doesn't play nice with my machine sadly.
* Microsoft Macro Assembler Version 6.11
* DPMI host (not necessary on Windows)
  * I use HXDPMI32 ([part of HXDOS by Japheth](https://www.japheth.de/HX.html)).

## Evironment setup

* Install Microsoft C/C++ 7.00
* Install all the patches for Microsoft C 7.00
* Install Microsoft Macro Assembler 6.11
* Install the LIB/INCLUDE directories from the book "DOS Internals" by Geoff Chapell
  * Obtainable at a certain blue background retro software library. Will make this easily accessible somewhere soon.
* Install the 2008 updates to CRTDRV.LIB by Geoff Chapell
  * Obtainable [here.](https://www.geoffchappell.com/notes/dos/internals/crtdrvr/update.htm)

## Building

```
cd <somewhere>\k6init
nmake
```

That's all.

## Build environment issues

I developed this on an AMD K6-II CXT 500MHz. DOS is not the most stable and easy to configure environment in the world and there are a few quirks to address.

### Weird errors when building

**USE HIMEM.SYS THAT COMES WITH MICROSOFT C/C++ 7.00!**

You'll get weird errors otherwise. It's something to do with XMS size. I can get this to compile with JemmEx when limiting its reported size to 32MiB.

QEMM '97 also works, but after a while I got really strange behavior. I cannot open K6init.h (and only that file) properly via network share anymore.

### Linker doesn't run when DPMI host is running

I couldn't really fix this, so I took the linker (`LINK.EXE` and `CVPACK.EXE`) from Microsoft Visual C++ 1.52. **NOTE:** The linker from VC1.52 requires a 32-Bit DOS Extender.
VC1.52 includes one in the form of `DOSXNT.EXE`

### CodeView doesn't run when DPMI host is running

No idea. I don't use the debugger (yet) so at the moment it is not a big deal for me but it would be cool to fix this...?

### Why don't you just use 386-MAX as suggested by Microsoft?`

Because when I load it, it reboots my computer. %-)

# License

[CC-BY-NC 3.0, click here for info](https://creativecommons.org/licenses/by-nc/3.0)
