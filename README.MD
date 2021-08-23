# K6INIT

## What is this?

This is a driver for MS-DOS to replace `k6dos.sys` which is a bit useless and unflexible. It does not support the CXT versions of the K6-II.

On the newer K6-2/2+/3+ models there are features that benefit system performance in select applications greatly, such as games.

This driver sets Write allocation and Write Combining for the LFB memory on these models.

In the future it will also let you set the multiplier on K6-3 models.

I'm doing this because I want my K6 machine to be fast.

## Why in C?

Because I hate / suck at x86 assembly and I want flexibility, such as more complex(ish) command line parsing which is hard to do in ASM...

Normally you should really do this in assembly. But YOLO.

# Project Status

## Does this work yet?

NO! I'm still learning.

## What does it (not) do so-far?

* Find and check for supported CPU types using `cpuid`
  * yes
* Find VESA BIOS and VESA modes
  * yes
* Find LFB address
  * yes
* Set MTRR to enable Write combining for LFB
  * yes
* Detect total available system memory and set WA mode
  * No
* Detect 15-16M Memory hole for WA
  * No
* Set K6-3 Multiplier
  * I don't actually own this CPU. ETA: When the cows come home


## Supported Processors

* TODO

# Building

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

# Running

`K6INIT.EXE` at a command prompt.

`DEVICE=K6INIT.EXE` in CONFIG.SYS.

# License

[CC-BY-NC 3.0, click here for info](https://creativecommons.org/licenses/by-nc/3.0)
