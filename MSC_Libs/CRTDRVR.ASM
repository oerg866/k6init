
;   ************************************************************************
;   *				  crtdrvr.asm				   *
;   ************************************************************************

;   This module prepares the interface between device driver initialisation
;   and the C run-time.  The intention is to match the two sufficiently well
;   to allow driver initialisation to be handled in C through the main ()
;   function, as if writing a normal application program.

;   REVISIONS:

;	28/05/92	Original version - for release with "DOS Internals".

;	08/06/96	Bug fix.  When DrvrProcInit returns, the original
;			code preserved ds.  The new code makes sure that ds
;			still addresses the resident data, which may have
;			been moved if the C language driver code uses the
;			CRTKEEPC library (also from "DOS Internals").

;	08/06/96	New code to work around problem with the Windows 95
;			implementation of int 21h function 31h.  Also
;			modified code that fakes an FCB-SFT.

;	30/07/96	Revised new code that works around problem with
;			the Windows 95 implementation of DOS function 31h.

;	23/03/08	Changed optimistic test for exactly DOS 7.0 so that
;			it now test for 7.0 and higher.

;	09/09/26	Fixed ES/stack issue causing garbage pointer on IBM
;			PC-DOS 7.xx/2000.

.NOLIST
  INCLUDE		arena.inc
  INCLUDE		ascii.inc
  INCLUDE		driver.inc
  INCLUDE		psp.inc
  INCLUDE		sft.inc
  INCLUDE		sysvars.inc
.LIST

.MODEL			SMALL, FARSTACK

;   The Keep C segmentation model for resident C programming is adopted,
;   but the segment definitions provided in the KEEPSEGS.INC file must be
;   modified to accommodate paragraph-alignment of a simulated PSP and
;   memory arena header.

KEEP_BTEXT		SEGMENT PARA COMMON 'CODE'
KEEP_BTEXT		ENDS

KEEP_TEXT		SEGMENT PARA PUBLIC 'CODE'
KEEP_TEXT		ENDS

KEEP			SEGMENT WORD PUBLIC 'FAR_DATA'
KEEP			ENDS

KEEP_GROUP		GROUP	KEEP

;   Paragraph alignment is also needed for a simulated environment and
;   accompanying memory arena header, best handled by declaring a private
;   segment within DGROUP.  The environment cannot be in the BSS area, as
;   explained below.

_ENV			SEGMENT PARA PRIVATE 'DATA'
_ENV			ENDS

DGROUP			GROUP	_ENV

;   Some measure of the program's stack (whose size may be set by the
;   linker) is required.  Create a paragraph-aligned fence to follow the
;   default STACK segment.  Note that in the presence of DOSSEG ordering,
;   the fence must be in DGROUP (as must the default stack).  This method
;   is ignorant of changes made to the stack by the EXEHDR utility.

.STACK			0000h
.DOSSEG

ENDSTACK		SEGMENT PARA COMMON 'STACK'
ENDSTACK		ENDS

DGROUP			GROUP	ENDSTACK

;   ========================================================================

.CODE			KEEP_TEXT

;   The interface with the C run-time simulates the entry conditions of a
;   normal DOS executable.  Most important is a PSP, set up so that control
;   can be regained when the high-level component of the program terminates
;   by the usual means.

;   The driver is allowed to overwrite its initialisation code and data, but
;   the PSP must be valid at termination, so place the fake PSP in resident
;   memory (paragraph-aligned).  The code segment KEEP_TEXT is selected to
;   avoid conflict with the scheme for repositioning the resident data.
;   This situation is not entirely satisfactory and an alternative ought to
;   be implemented in a future version.

FakePSPArena		ARENA_HEADER	{ARENA_LAST, 0000h, 0000h,, "CRTDRVR"}
FakePSP 		PSP	<>

;   The following pieces of related data are needed before and after the
;   jump to the C run-time.  They too must be placed in a resident segment.
;   Access is easier if they lie in the same segment as the PSP, rather than
;   in a KEEP_GROUP segment.

lpDTA			dd	?
lpInt22h		dd	?
lpSysinitStack		dd	?
spSysinitPSP		dw	?

;   Similar considerations apply to the installation of a System File Table
;   for use with File Control Blocks.

;   UPDATE 08/06/96 START

;   The original version took the easy path of defining a fake FCB-SFT
;   statically in the resident code.  It is better, however, to create the
;   fake FCB-SFT dynamically in memory taken from the top (and hidden from
;   the C language driver code).  The fake FCB-SFT is not needed if the
;   driver loads under Windows 95 (DOS 7).

lpFakeFCBSFT		dd	00000000h
lpFCBSFTptr		dd	00000000h

;   A feature new to Windows 95 (DOS 7) is that int 21h function 4Bh records
;   information about the system state before the new process started - if
;   memory needed for this recording has been initialised.  Unfortunately,
;   int 21h function 31h simply assumes that the memory has been provided.
;   As it happens, SYSINIT doesn't initialise this recording until it has
;   finished loading device drivers.  This is a problem since the C language
;   driver code will usually terminate-and-stay-resident.

;   To get around this difficulty, we give DOS the address of some memory
;   that int 21h function 31h can use without causing harm.  The least
;   memory that seems to be required is 0141h bytes:

DRVRPTR 		TYPEDEF DWORD

TSRINFO 		STRUCT
  lpInt13h		dd	?
  pNextRecord		dw	?
			dd	?
			dd	?
  aRecord		db	14h * 0Ch DUP (?)
			dw	?
  lpHwInts		dd	10h DUP (?)
  nDrives		db	?
  aDrvrPtr		DRVRPTR 00h DUP (?)
TSRINFO 		ENDS

MIN_TSRINFO		STRUCT
			TSRINFO <>
			DRVRPTR ?
MIN_TSRINFO		ENDS

;   If the driver is loaded under DOS 7, the memory for this structure will
;   be taken off the top.

lpFakeTSRInfo		dd	00000000h
lpTSRInfoPtr		dd	00000000h

;   UPDATE 08/06/96 END

;   ========================================================================

_ENV			SEGMENT

;   It is not essential that a program have an environment, but one is
;   simulated to enable the C run-time to provide a pathname as argv [0].

;   Note that the environment cannot be declared as uninitialised data,
;   because the C run-time would clear it before extracting the pathname.

;   Both the PSP and environment are accompanied by fake memory arena
;   headers, but the illusion is not carried further (just far enough for
;   instance, to support routines that inspect the arena header to obtain
;   a practical limit on the environment size).

FakeEnvArena		ARENA_HEADER	{ARENA_LAST, 0000h, 0000h,, "CRTDRVR"}
FakeEnv 		db	0100h DUP (?)

_ENV			ENDS

;   ========================================================================

.DATA?

;   Segment addresses for the fake environment and PSP - these are needed
;   only before the transfer to the C run-time and may therefore be declared
;   as uninitialised data:

spFakeEnv		dw	?
spFakePSP		dw	?

;   ========================================================================

.DATA

;   Segment address giving an upper bound on memory available to the device
;   driver - must be initialised because access is needed before the
;   addressability of the uninitialised data area has been established:

spTop			dw	0000h

;   ========================================================================

.CONST

;   Fatal error messages:

FatalError		db	CR, LF,
				"CRTDRVR Fatal error:  ",
				"$"

BadVersion		db	"DOS version 5 or higher required",
				CR, LF,
				"$"

OutOfMemory		db	"Insufficient memory for driver",
				CR, LF,
				"$"

;   ========================================================================

.FARDATA		KEEP

;   A Boolean flag in a resident data segment - to allow the high-level
;   component to determine whether it is being executed for device driver
;   initialisation or as a normal program:

			PUBLIC	C bLoadedAsDriver
bLoadedAsDriver 	db	00h

;   ========================================================================

.CODE			KEEP_TEXT

DrvrProcInit		PROC	NEAR C PUBLIC lpReqHdr:DWORD

;   The DrvrProcInit function is the C language entry point required by
;   the DRIVER.OBJ module for the initialisation command.  Although this
;   entry point is needed only once, it lies in and is called from the
;   resident code segment KEEP_TEXT, for the sake of uniformity.

;   Transfer the initialisation request to non-resident code.

			ASSUME	ds:KEEP_GROUP

			push	word ptr [lpReqHdr + 02h]
			push	word ptr [lpReqHdr]
			call	far ptr NonResidentInit

;   BUGFIX 08/06/96 START

;   On entry, ds addresses the resident data.  However, the C language
;   driver code may use the scheme for repositioning resident data.  Make
;   sure that on exit, ds still addresses the resident data.  Just reload ds
;   from a relocatable reference to KEEP_GROUP:  if the resident data has
;   been moved, the code that did it will have updated all the relocatable
;   segment references.

			mov	bx,KEEP_GROUP
			mov	ds,bx

;   BUGFIX 08/06/96 END

			pop	bx
			pop	bx

			ret

DrvrProcInit		ENDP

;   ========================================================================

.CODE

NonResidentInit 	PROC	FAR C PRIVATE USES ds di si, lpReqHdr:DWORD

;   While ds still addresses the resident data group, set a flag to show that
;   the program has been loaded as a device driver.

			mov	[bLoadedAsDriver],0FFh

;   The device driver will have been loaded into memory as an overlay,
;   without regard to the program's memory requirements in excess of its
;   disk image.  The validity of addresses assigned to uninitialised data
;   must therefore be established before attempting access.

;   In a feature new to DOS 5, an upper limit to available memory is passed
;   as a far pointer in the request header - the same field in which the
;   driver is expected to return the address of the first byte of memory it
;   will not require.  The provision of this field depends on the real DOS
;   version reported by DOS function 3306h, not the usual function 30h.

			lds	si,[lpReqHdr]

			mov	ax,DGROUP
			mov	es,ax

			ASSUME	ds:NOTHING, es:DGROUP, si:PTR REQUEST_HEADER

			mov	ax,3306h
			xor	bx,bx
			int	21h
			mov	dx,OFFSET BadVersion
			cmp	bl,05h
			jb	error

			mov	ax,word ptr [si].lpEndOfMemory
			mov	cl,04h
			shr	ax,cl
			add	ax,word ptr [si + 02h].lpEndOfMemory
			mov	es:[spTop],ax

			cmp	ax,ENDSTACK
			jnb	@f

bad_memory:
			mov	dx,OFFSET OutOfMemory

error:
			push	ds
			push	es
			pop	ds
			push	dx
			mov	ah,09h
			mov	dx,OFFSET FatalError
			int	21h
			pop	dx
			mov	ah,09h
			int	21h
			pop	ds

			jmp	far ptr fail

@@:

;   UPDATE 08/06/96 START

;   If loading under DOS 7, take enough memory from the top to satisfy
;   int 21h function 31h.  Presumably, this silliness will not be needed for
;   the next DOS version (should there ever be another).

;   UPDATE 23/03/08 START (NESTED)

;   The lack of interest in DOS (including from me) as anything more than a
;   loader for Windows seems to have had the consequence that nobody (again,
;   including me) has cared until now that the silliness referred to above
;   did continue to be needed. Checking for exactly version 7.00 was much
;   too hopeful.

			xchg	bl,bh
			cmp	bx,0700h
			jnb	getmem_tsrinfo

;   UPDATE 23/03/08 END (NESTED)

getmem_fcbsft:

;   If loading under an earlier DOS, take enough memory from the top to
;   create a fake FCB-SFT.  This silliness is not needed for DOS 7.

			sub	ax,(SIZE SFT_HEADER + SFT + 0Fh) SHR 04h
			jb	bad_memory
			cmp	ax,ENDSTACK
			jb	bad_memory

			push	es

;   Note the address of the dword in which DOS keeps the address of the
;   FCB-SFT header.  Take the opportunity to check whether DOS already has a
;   set of FCB-SFTs.

			push	ax
			mov	ah,52h
			int	21h
			add	bx,SYSVARS.lpFCBSFTs
			pop	ax

			cmp	word ptr es:[bx],0000h
			jnz	getmem_restore
			cmp	word ptr es:[bx + 02h],0000h
			jnz	getmem_restore

			mov	dx,es

			mov	cx,KEEP_TEXT
			mov	es,cx
			ASSUME	es:KEEP_TEXT

			mov	word ptr es:[lpFCBSFTptr],bx
			mov	word ptr es:[lpFCBSFTptr + 02h],dx

;   Note the location of the fake FCB-SFT and header (for later).

			xor	di,di
			ASSUME	di:PTR SFT_HEADER
			mov	word ptr es:[lpFakeFCBSFT],di
			mov	word ptr es:[lpFakeFCBSFT + 02h],ax

;   Initialise the fake FCB-SFT and header.

			mov	es,ax
			ASSUME	es:NOTHING
			mov	word ptr es:[di].lpNext,0FFFFh
			mov	word ptr es:[di + 02h].lpNext,0FFFFh
			mov	word ptr es:[di].wCount,0001h

			lea	di,es:[di + SIZE SFT_HEADER]
			ASSUME	di:PTR SFT

			mov	cx,SIZE SFT
			push	di
			push	ax
			mov	al,'A'
			rep	stosb
			pop	ax
			pop	di

			mov	es:[di].wHandles,0000h
			mov	word ptr es:[di].dwFilePointer,0000h
			mov	word ptr es:[di + 02h].dwFilePointer,0000h

			pop	es
			ASSUME	es:DGROUP, di:NOTHING

			jmp	getmem_done

getmem_tsrinfo:
			sub	ax,(SIZE MIN_TSRINFO + 0Fh) SHR 04h
			jb	bad_memory
			cmp	ax,ENDSTACK
			jb	bad_memory

			push	es

			push	ax
			mov	ah,52h
			int	21h
			mov	bx,1328h
			pop	ax

			cmp	word ptr es:[bx],0000h
			jnz	getmem_restore
			cmp	word ptr es:[bx + 02h],0000h
			jnz	getmem_restore

			mov	dx,es

			mov	cx,KEEP_TEXT
			mov	es,cx
			ASSUME	es:KEEP_TEXT

			mov	word ptr es:[lpTSRInfoPtr],bx
			mov	word ptr es:[lpTSRInfoPtr + 02h],dx

;   Note the location of the TSRINFO structure (for later).

			xor	di,di
			ASSUME	di:PTR TSRINFO
			mov	word ptr es:[lpFakeTSRInfo],di
			mov	word ptr es:[lpFakeTSRInfo + 02h],ax

;   Initialise the TSRINFO structure - again, just enough to satisfy int 21h
;   function 31h.

			mov	es,ax
			ASSUME	es:NOTHING

			mov	cx,SIZE MIN_TSRINFO
			push	di
			push	ax
			xor	al,al
			rep	stosb
			pop	ax
			pop	di

			lea	bx,es:[di].aRecord
			mov	es:[di].pNextRecord,bx

			mov	es:[di].nDrives,01h

;   BUGFIX: the four conditional jumps above, which are taken when DOS is
;   found already to have the structure that would otherwise be faked here,
;   used to branch straight to getmem_done and thereby leave the pushed es
;   on the stack with es still addressing the DOS data segment as returned
;   by int 21h function 52h.  All subsequent es-relative writes then went
;   into the DOS kernel instead of DGROUP.  They now come here instead.

getmem_restore:
			pop	es
			ASSUME	es:DGROUP, di:NOTHING

getmem_done:

;   Adjust the driver's "top of memory" so that the hack structures sit
;   safely above.

			mov	es:[spTop],ax

			mov	word ptr [si].lpEndOfMemory,0000h
			mov	word ptr [si].lpEndOfMemory + 02h,ax

;   UPDATE 08/06/96 END

;   Prepare the simulated environment:

;	The device driver command line, whose address is passed in the
;	request header, starts with the first non-separator after the DEVICE
;	or DEVICEHIGH directive (including its optional SIZE parameter),
;	i.e., the first character of the device driver's pathname.

;	In a DEVICE statement, the end of the pathname is marked by any
;	character < 20h, space, comma, forward slash, semicolon or equals
;	sign, but by the time SYSINIT is ready to initialise the driver,
;	this separator will have been converted to a space.  DEVICEHIGH
;	directives are treated slightly differently in that the separator is
;	preserved, but note that the only control characters regarded as
;	separators are the null byte, tab and carriage-return.	In any case,
;	all lower case letters are capitalised and the line-feed is retained
;	as the terminator.

;	The C run-time (as may any program) expects the pathname to have
;	been appended to the environment by the DOS "load and execute"
;	function.  Recall that the environment consists of consecutive
;	ASCIIZ strings with an extra null to terminate the list.  DOS
;	follows this with the word 0001h and then the pathname as an ASCIIZ
;	string.

;	No attempt is made to canonicalise the pathname - either by DOS or
;	the following code.  Note however, that more than a few
;	professionally-written programs - among them WIN.COM - assume the
;	presence of at least one backslash in the pathname they take from
;	the environment.

			lds	si,[si].lpCommandLine
			ASSUME	si:NOTHING

			mov	di,OFFSET FakeEnv

			xor	ax,ax
			stosw
			mov	al,01h
			stosw

@@:
			lodsb
  FOR			separator, <NULL, TAB, LF, CR, " ", ",", "/", ";", "=">
			cmp	al,separator
			jz	@f
  ENDM
			stosb
			jmp	@b

@@:
			xor	al,al
			stosb

;   Save the pointer into the device driver command line - the remainder
;   must yet go into the PSP.

			push	ds
			push	si

;   The fake PSP and environment are both paragraph-aligned and have
;   equivalent segment addresses.  These are needed for the arena headers
;   and again later, when the fake PSP is activated.

			mov	cl,04h
			mov	bx,OFFSET FakeEnv
			shr	bx,cl
			mov	dx,es
			add	bx,dx
			mov	es:[spFakeEnv],bx

			mov	dx,KEEP_TEXT
			mov	ds,dx
			ASSUME	ds:KEEP_TEXT

			mov	ax,OFFSET FakePSP
			shr	ax,cl
			add	ax,dx
			mov	es:[spFakePSP],ax

;   Initialise the arena header for the environment.

			mov	di,OFFSET FakeEnvArena.spOwner
			stosw
			mov	word ptr es:[di],SIZEOF FakeEnv SHR 4

;   Fill in the uninitialised fields in the arena header for the PSP.

			mov	[FakePSPArena.spOwner],ax
			mov	dx,ax

			mov	si,es:[spTop]
			sub	ax,si
			neg	ax
			mov	[FakePSPArena.wParagraphs],ax

;   Clone the current PSP (set up by the SYSINIT module of IO.SYS, which
;   loaded the device driver) and activate the copy.  This is all done by
;   DOS function 55h - registers dx and si are expected to contain a segment
;   address at which to create the child process and another which delimits
;   the available memory.

			mov	ah,55h
			int	21h

;   Set the environment pointer in the fake PSP.

			mov	[FakePSP.spEnvironment],bx

;   Copy the remaining command line arguments to the PSP, taking care not
;   to exceed the available space.  The command line in the PSP must be
;   terminated by a carriage-return and preceded by a byte count.

			push	ds
			pop	es
			mov	di,OFFSET FakePSP.CommandLine

			pop	si
			pop	ds

			ASSUME	ds:NOTHING, es:KEEP_TEXT

@@:
			lodsb
			cmp	al,LF
			jz	@f
			cmp	di,OFFSET FakePSP + SIZEOF PSP - 1
			jnb	@f
			stosb
			jmp	@b

@@:
			push	es
			pop	ds
			ASSUME	ds:KEEP_TEXT

			mov	byte ptr [di],CR
			mov	ax,OFFSET FakePSP.CommandLine
			xchg	ax,di
			sub	ax,di
			dec	di
			mov	[di],al

;   No set of SFTs dedicated for use with FCBs will exist yet, but as part
;   of the normal clean-up operations on process termination, DOS will try
;   to close outstanding FCBs - without checking that a set of FCB-SFTs
;   actually exists (after all, DOS has no intention of using them before
;   it is ready to run applications, by which time a suitable set will have
;   been constructed).

;   UPDATE 08/06/96 START

;   This FCB-SFT hack is not needed in Windows 95, which has fixed the DOS
;   bug so that the existence of FCB-SFTs is checked before DOS tries to
;   close any FCB-SFTs during its cleanup of a process termination.

			mov	ax,word ptr [lpFakeFCBSFT]
			mov	dx,word ptr [lpFakeFCBSFT + 02h]

			or	dx,dx
			jz	@f

			les	bx,[lpFCBSFTptr]
			mov	es:[bx],ax
			mov	es:[bx + 02h],dx

@@:

;   If a TSRINFO structure has been prepared for Windows 95, tell DOS about
;   it now.  In DOS 7, the far pointer from the DOS kernel to the memory in
;   which int 21h functions 31h and 4Bh record changes that TSRs make to the
;   system state is at offset 1328h in the DOS data segment (currently
;   addressed by es).

			mov	ax,word ptr [lpFakeTSRInfo]
			mov	dx,word ptr [lpFakeTSRInfo + 02h]

			or	dx,dx
			jz	@f

			les	bx,[lpTSRInfoPtr]
			mov	word ptr es:[bx],ax
			mov	word ptr es:[bx + 02h],dx

@@:

;   UPDATE 08/06/96 END

;   Set the DTA to the command line area of the PSP, as would normally be
;   done by DOS function 4B00h.  Note, as more than a curiosity, that the
;   DTA address is not preserved by DOS function 4B00h.

			mov	ah,2Fh
			int	21h
			mov	word ptr [lpDTA],bx
			mov	word ptr [lpDTA + 02h],es

			mov	ah,1Ah
			mov	dx,di
			int	21h

;   When the child process terminates, the parent receives control at an
;   address taken from a field at offset 0Ah in the child's PSP.  DOS
;   function 55h (used to create the child PSP) will have set this
;   termination address to the current value of int 22h in the interrupt
;   vector table.  Ordinarily, DOS function 55h is called internally by
;   the "load and execute" function 4Bh, which then points both the PSP
;   termination field and the int 22h vector to the instruction immediately
;   after the "load and execute" call.  Simulate this by redirecting the
;   termination pointer and the int 22h vector to an instruction inside this
;   module (but in the resident code segment).

			xor	ax,ax
			mov	es,ax
			ASSUME	es:NOTHING
			mov	di,22h * 4

			mov	ax,es:[di]
			mov	word ptr [lpInt22h],ax
			mov	ax,es:[di + 02h]
			mov	word ptr [lpInt22h + 02h],ax

			mov	ax,OFFSET CRTreturn
			mov	dx,SEG CRTreturn

			mov	es:[di],ax
			mov	es:[di + 02h],dx

			mov	word ptr [FakePSP.lpInt22h],ax
			mov	word ptr [FakePSP.lpInt22h + 02h],dx

;   The stack is restored from an address which would have been saved in the
;   parent's own PSP as a normal consequence of the parent's issuing an
;   int 21h instruction (typically the one to load the child program).	The
;   address is saved by the DOS kernel only after the caller's CPU registers
;   have been pushed onto the stack for retrieval at termination.

			pushf
			push	dx
			push	ax

			push	es
			push	ds
			push	bp
			push	di
			push	si
			push	dx
			push	cx
			push	bx
			push	ax

			mov	ax,[FakePSP.spParentPSP]
			mov	[spSysinitPSP],ax
			mov	es,ax

			mov	ax,sp
			xchg	ax,word ptr es:[PSP.lpStack]
			mov	word ptr [lpSysinitStack],ax
			mov	ax,ss
			xchg	ax,word ptr es:[PSP.lpStack + 02h]
			mov	word ptr [lpSysinitStack + 02h],ax

;   Reproduce the normal .EXE entry conditions expected by the _astart entry
;   point to the C run-time:

;	Address the simulated PSP with both ds and es.

			mov	ax,DGROUP
			mov	ds,ax
			ASSUME	ds:DGROUP
			mov	ax,[spFakePSP]
			mov	ds,ax
			mov	es,ax
			ASSUME	ds:NOTHING, es:NOTHING

;	Point ss:sp to the top of the STACK segment.

			mov	ax,ENDSTACK
			mov	dx,STACK
			sub	ax,dx
			mov	cl,04h
			shl	ax,cl

			cli
			mov	ss,dx
			mov	sp,ax
			sti

;	Register ax should contain a value describing the validity of the
;	drive elements in the two FCBs contained in the PSP.  The fields
;	themselves are seldom used and the value in ax even less so.

			mov	ax,0FFFFh

;   Transfer to _astart, but if the CRTKEEPC library is linked, execute its
;   initialisation code first to rearrange segments as needed.	Control will
;   be regained at the label CRTreturn in the KEEP_TEXT segment below.

			EXTERN	C _astart:FAR
			EXTERN	C InitKeepC (_astart):FAR

			jmp	InitKeepC

NonResidentInit 	ENDP

;   ========================================================================

.CODE			KEEP_TEXT

CRTreturn		PROC	FAR C PRIVATE USES ds di si, lpReqHdr:DWORD

;   The prologue code for this procedure will have been set up by the
;   procedure NonResidentInit in the _TEXT segment - only the corresponding
;   epilogue sequence is required.

			OPTION	PROLOGUE:NONE

;   Restore the int 22h vector and the stack address field in SYSINIT's PSP
;   to the values they had when this device driver was loaded.	Note that
;   registers have been retrieved from the stack created earlier, so ds
;   still addresses KEEP_TEXT and es:di still points to the Interrupt
;   Vector Table location for int 22h.

			ASSUME	ds:KEEP_TEXT, es:NOTHING

			mov	si,OFFSET lpInt22h
			movsw
			movsw

			mov	es,[spSysinitPSP]
			mov	di,PSP.lpStack
			mov	si,OFFSET lpSysinitStack
			movsw
			movsw

;   Restore the DTA.

			push	ds
			pop	es
			lds	dx,[lpDTA]
			ASSUME	ds:NOTHING, es:KEEP_TEXT

			mov	ah,1Fh
			int	21h

;   UPDATE 08/06/96 START

;   If a TSRINFO structure was set up to satisfy the Windows 95 version of
;   int 21h function 31h, undo the hack.

			mov	ax,word ptr es:[lpFakeTSRInfo]
			mov	dx,word ptr es:[lpFakeTSRInfo + 02h]
			or	dx,dx
			jz	@f

			lds	bx,es:[lpTSRInfoPtr]

                        cmp     word ptr [bx],ax
                        jnz     @f
                        cmp     word ptr [bx + 02h],dx
                        jnz     @f

			xor	ax,ax
			mov	[bx],ax
			mov	[bx + 02h],ax

@@:

;   If an FCB-SFT was installed for the child, remove it.

			mov	ax,word ptr [lpFakeFCBSFT]
			mov	dx,word ptr [lpFakeFCBSFT + 02h]
			or	dx,dx
			jz	@f

			lds	bx,es:[lpFCBSFTptr]

			cmp	word ptr [bx],ax
			jnz	@f
			cmp	word ptr [bx + 02h],dx
			jnz	@f

			xor	ax,ax
			mov	[bx],ax
			mov	[bx + 02h],ax

@@:

;   UPDATE 08/06/96 END

;   Prepare to set values in the request header.  Use the same registers as
;   in the NonResidentInit procedure.

			lds	si,[lpReqHdr]
			ASSUME	si:PTR REQUEST_HEADER

;   Obtain the child process's exit codes.

			mov	ah,4Dh
			int	21h

;   If the program stayed resident, retrieve the break address from the
;   PSP - DOS function 31h adjusts the "top of memory" field in the PSP
;   if it is in fact able to resize the PSP's memory block.

			cmp	ah,03h
			jnz	@f

			mov	ax,es:[FakePSP.spCeiling]
			mov	word ptr [si].lpEndOfMemory,0000h
			mov	word ptr [si + 02h].lpEndOfMemory,ax

			mov	ax,D_DONE

			jmp	done

@@:

;   For the non-resident terminations, set the flag for the system error
;   message (introduced in DOS 4) if the cause of termination was a DOS
;   exception (Ctrl-C or a critical error) or if the child's return code
;   is >= 80h.

			or	ah,ah
			jnz	fail

			or	al,al
			jns	@f

fail::
			mov	[si].wMessageFlag,0001h

@@:

;   Abandon the device driver installation by indicating that none of the
;   program, from the very first byte onwards, is required.

			mov	word ptr [si].lpEndOfMemory,0000h
			mov	word ptr [si + 02h].lpEndOfMemory,KEEP_BTEXT

;   Block devices are also rejected if they return a count of no units to
;   support.  Note that for neither type of device driver does DOS consult
;   the status field for an error flag, although the published reference
;   states that the error bit should be set to indicate failure.

			mov	[si].cUnitsSupported,00h

			mov	ax,D_ERROR OR D_DONE OR D_GENERAL_FAILURE

done:
			ret

CRTreturn		ENDP

;   ************************************************************************

END

