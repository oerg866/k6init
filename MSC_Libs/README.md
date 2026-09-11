# Libraries for writing DOS drivers in C.

These files were originally published as part of the book "DOS Internals", released in 1994 by the late Geoff Chappell (R.I.P 😢)

Due to his recent passing, I am including these here in case his website goes down.

These are *not* the unmodified libraries, see below.

# CRTDRVR fix for IBM PC DOS 7.XX

## Symptom

Loading K6INIT from `CONFIG.SYS` on PC DOS 7.01 (PC DOS 2000) fails with

```
Keep C fatal error:  Unable to fixup segment references
```

Running the same EXE from the command prompt works. (MS-DOS 5/6 and Windows 9x/DOS 7.x are unaffected)

## Cause

A bug in Geoff Chappell's `CRTDRVR.ASM`, in `NonResidentInit`:

```asm
getmem_tsrinfo:
            push    es                      ; es = DGROUP
            push    ax
            mov     ah,52h
            int     21h                     ; es = DOS data segment
            mov     bx,1328h
            pop     ax
            cmp     word ptr es:[bx],0000h
            jnz     getmem_done             ; <-- leaves es = DOS DS, es still on stack
            cmp     word ptr es:[bx + 02h],0000h
            jnz     getmem_done             ; <-- ditto
            ...
            pop     es                      ; only the fall-through path restores es
getmem_done:
            mov     es:[spTop],ax
```

The two `jnz getmem_done` branches skip the `pop es`, so execution continues with `es` pointing at the **DOS kernel data segment** instead of `DGROUP` (+ a stray word left on the stack). The same thing happens in the pre-DOS-7 `getmem_fcbsft` block.

Under Windows 9x/DOS 7.x the dword at 1328h is the TSRINFO pointer and is NULL while SYSINIT is loading drivers, so the branch is never taken.

The PC-DOS Kernel is a different codebase. Offset 1328h is ordinary kernel data that is non-NULL, so the branch is always taken.

Everything `NonResidentInit` writes afterwards then lands in the DOS kernel:

* `mov es:[spTop],ax`
* `mov es:[spFakeEnv],bx`
* `mov es:[spFakePSP],ax`
* the fake environment's arena header

When the routine later does this:

```
  mov ax,DGROUP
  mov ds,ax
  mov ax,[spFakePSP]
```

it reads the UNTOUCHED word, which is NULL, so it enters `InitKeepC` with `ds = es = 0000`.

Keep C's `SwapReferences` must re-read the relocation table (the loader has already consumed it). It attempts to get the executable's name through `GetProgramName`, which reads `PSP:2Ch` to get the environment segment and validates the environment's MCB owner against the current PSP.

Since the PSP segment is still NULL, that lookup reads an interrupt vector instead of a PSP. The owner check fails, `SwapReferences` returns error, and `InitKeepC` prints the message: `Unable to fixup segment reference`.

## Fix

Route the early exit branches through a label that restores `es` first (`getmem_restore`).

The `CRTDRVR.ASM` file contains the fixed source (based on Chappell's 16 June 2008 revision), and the LIB folder contains the assembled library file.

There might be other DOS variants that are broken, but that will be the subject of another bug report :P
