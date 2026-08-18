/*
 * QEMouse - Win16/Win9x QEMU vmmouse driver entry points
 * Derived from VBMouse by Javier S. Pedro.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <windows.h>

#include "vmware.h"
#include "ps2.h"
#include "int2fwin.h"
#include "mousew16.h"

/** If 1, hook int2f to detect fullscreen DOS boxes and auto-disable absolute input. */
#define HOOK_INT2F 1

#define MOUSE_NUM_BUTTONS 2

/** The routine Windows gave us which we should use to report events. */
static LPFN_MOUSEEVENT eventproc;
/** Current status of the mouse driver (see MOUSEFLAGS_*). */
static unsigned char mouseflags;
enum {
	MOUSEFLAGS_ENABLED          = 1 << 0,
	MOUSEFLAGS_HAS_VMWARE       = 1 << 1,
	MOUSEFLAGS_VMWARE_ENABLED   = 1 << 2,
	MOUSEFLAGS_HAS_WIN386       = 1 << 3,
	MOUSEFLAGS_INT2F_HOOKED     = 1 << 4,
	MOUSEFLAGS_VMWARE_SUSPENDED = 1 << 5
};
/** Last received pressed button status (to compare and see which buttons have been pressed). */
static unsigned char mousebtnstatus;
/** Last absolute position, used to avoid reporting stationary button-only packets as movement. */
static uint16_t mousex, mousey;
static bool mouseposvalid;

#if HOOK_INT2F
enum {
	DISPLAY_SWITCH_NONE       = 0,
	DISPLAY_SWITCH_BACKGROUND = 1,
	DISPLAY_SWITCH_FOREGROUND = 2
};
/** INT 2Fh display switch recorded for deferred handling from the PS/2 callback. */
static volatile unsigned char display_switch_pending;
#endif

/** Existing interrupt2f handler. */
static LPFN prev_int2f_handler;

/* This is how events are delivered to Windows. */

static void send_event(unsigned short Status, short deltaX, short deltaY, short ButtonCount, short extra1, short extra2);
#pragma aux (MOUSEEVENTPROC) send_event = \
	"call dword ptr [eventproc]"

/* VMware/QEMU absolute mouse helpers. */

#pragma code_seg ( "CALLBACKS" )

static bool __far vmware_detect(void)
{
	uint32_t status;

	if (vmware_get_version() < 0) {
		return false;
	}

	/* GETVERSION only proves that the generic VMware backdoor exists.
	 * Probe the absolute-pointer command set as well so Inquire() does not
	 * advertise absolute coordinates when QEMU has vmport but no vmmouse. */
	vmware_abspointer_cmd(VMWARE_ABSPOINTER_CMD_ENABLE);
	status = vmware_abspointer_status();
	if ((status & VMWARE_ABSPOINTER_STATUS_MASK_ERROR) == VMWARE_ABSPOINTER_STATUS_MASK_ERROR) {
		vmware_abspointer_cmd(VMWARE_ABSPOINTER_CMD_DISABLE);
		return false;
	}

	vmware_abspointer_data_clear();
	vmware_abspointer_cmd(VMWARE_ABSPOINTER_CMD_REQUEST_RELATIVE);
	vmware_abspointer_cmd(VMWARE_ABSPOINTER_CMD_DISABLE);
	return true;
}

static bool __far vmware_enable_absolute(void)
{
	uint32_t status;

	vmware_abspointer_cmd(VMWARE_ABSPOINTER_CMD_ENABLE);

	status = vmware_abspointer_status();
	if ((status & VMWARE_ABSPOINTER_STATUS_MASK_ERROR) == VMWARE_ABSPOINTER_STATUS_MASK_ERROR) {
		vmware_abspointer_cmd(VMWARE_ABSPOINTER_CMD_DISABLE);
		return false;
	}

	vmware_abspointer_data_clear();
	vmware_abspointer_cmd(VMWARE_ABSPOINTER_CMD_REQUEST_ABSOLUTE);
	mouseposvalid = false;
	return true;
}

/* Retry the complete usable-absolute-path test at a later execution point.
 * GETVERSION is intentionally repeated even if an earlier probe succeeded,
 * because the mouse stack may have changed the backdoor state in the meantime. */
static bool __far vmware_try_enable_absolute(void)
{
	if (vmware_get_version() < 0) {
		return false;
	}

	if (!vmware_enable_absolute()) {
		return false;
	}

	mouseflags |= MOUSEFLAGS_HAS_VMWARE | MOUSEFLAGS_VMWARE_ENABLED;
	return true;
}

static void __far vmware_disable_absolute(void)
{
	vmware_abspointer_cmd(VMWARE_ABSPOINTER_CMD_REQUEST_RELATIVE);
	vmware_abspointer_cmd(VMWARE_ABSPOINTER_CMD_DISABLE);
	mouseposvalid = false;
}

static void report_vmware_event(const struct vmware_abspointer_data __far *vmw)
{
	int sstatus = 0;
	short sx, sy;
	unsigned char status = 0;

	/* Windows was told by Inquire() that this driver is absolute. Never pass a
	 * relative VMware packet to the Windows absolute-mouse callback. The PS/2
	 * handler normally catches this first and re-arms absolute mode; keep this
	 * guard here as a second line of defense. */
	if (vmw->status & VMWARE_ABSPOINTER_STATUS_RELATIVE) {
		mouseposvalid = false;
		return;
	}

	{
		uint16_t x = (uint16_t) vmw->x;
		uint16_t y = (uint16_t) vmw->y;

		sx = (short) x;
		sy = (short) y;
		sstatus |= SF_ABSOLUTE;

		if (!mouseposvalid || x != mousex || y != mousey) {
			sstatus |= SF_MOVEMENT;
			mousex = x;
			mousey = y;
			mouseposvalid = true;
		}
	}

	if (vmw->status & VMWARE_ABSPOINTER_STATUS_BUTTON_LEFT) {
		status |= PS2M_STATUS_BUTTON_1;
	}
	if (vmw->status & VMWARE_ABSPOINTER_STATUS_BUTTON_RIGHT) {
		status |= PS2M_STATUS_BUTTON_2;
	}

	if ((mousebtnstatus & PS2M_STATUS_BUTTON_1) && !(status & PS2M_STATUS_BUTTON_1)) {
		sstatus |= SF_B1_UP;
	} else if (!(mousebtnstatus & PS2M_STATUS_BUTTON_1) && (status & PS2M_STATUS_BUTTON_1)) {
		sstatus |= SF_B1_DOWN;
	}

	if ((mousebtnstatus & PS2M_STATUS_BUTTON_2) && !(status & PS2M_STATUS_BUTTON_2)) {
		sstatus |= SF_B2_UP;
	} else if (!(mousebtnstatus & PS2M_STATUS_BUTTON_2) && (status & PS2M_STATUS_BUTTON_2)) {
		sstatus |= SF_B2_DOWN;
	}

	mousebtnstatus = status;

	if (sstatus) {
		send_event(sstatus, sx, sy, MOUSE_NUM_BUTTONS, 0, 0);
	}
}

/* PS/2 BIOS mouse callback. QEMU uses a fake PS/2 event to tell the guest
 * that VMware absolute-pointer data is waiting on the backdoor queue. */

static void ps2_mouse_handler(uint16_t status, uint16_t x, uint16_t y, uint16_t z)
{
#pragma aux ps2_mouse_handler "*" parm caller [ax] [bx] [cx] [dx] modify [ax bx cx dx si di es]

	if (!(mouseflags & MOUSEFLAGS_ENABLED)) {
		return;
	}

#if HOOK_INT2F
	/* Win95 may deliver INT 2Fh display-switch notifications while its VM state
	 * is in transition. Do not perform VMPort I/O from that interrupt context.
	 * The hook only records the newest requested state; service it here, where
	 * QEMouse already performs VMware backdoor I/O for mouse events. */
	if (display_switch_pending == DISPLAY_SWITCH_BACKGROUND) {
		display_switch_pending = DISPLAY_SWITCH_NONE;

		if ((mouseflags & MOUSEFLAGS_HAS_VMWARE)
		        && !(mouseflags & MOUSEFLAGS_VMWARE_SUSPENDED)) {
			/* Mark the DOS VM first so the startup-recovery path below cannot turn
			 * absolute mode back on while DOS owns the mouse. If vmmouse is active,
			 * disable it so following host events use the relative PS/2 path. */
			mouseflags |= MOUSEFLAGS_VMWARE_SUSPENDED;
			if (mouseflags & MOUSEFLAGS_VMWARE_ENABLED) {
				vmware_disable_absolute();
			}
			return;
		}
	} else if (display_switch_pending == DISPLAY_SWITCH_FOREGROUND) {
		display_switch_pending = DISPLAY_SWITCH_NONE;

		if ((mouseflags & MOUSEFLAGS_HAS_VMWARE)
		        && ((mouseflags & MOUSEFLAGS_VMWARE_SUSPENDED)
		            || !(mouseflags & MOUSEFLAGS_VMWARE_ENABLED))) {
			/* Windows is absolute again. Clear the DOS state before retrying, but
			 * never let this transition packet fall through to the relative PS/2
			 * path if vmmouse is not ready yet. */
			mouseflags &= ~(MOUSEFLAGS_VMWARE_ENABLED | MOUSEFLAGS_VMWARE_SUSPENDED);
			vmware_try_enable_absolute();
			return;
		}
	}
#endif

	/* Inquire() advertises an absolute device as soon as the vmmouse interface
	 * has been detected. PS/2 initialization during Enable() can temporarily
	 * knock that interface offline on Win95. If the first enable attempt failed,
	 * retry from subsequent mouse callbacks and swallow the triggering relative
	 * packet until absolute mode is live. Never feed relative coordinates to
	 * Windows while it believes this driver is absolute. */
	if ((mouseflags & MOUSEFLAGS_HAS_VMWARE)
	        && !(mouseflags & MOUSEFLAGS_VMWARE_SUSPENDED)
	        && !(mouseflags & MOUSEFLAGS_VMWARE_ENABLED)) {
		vmware_try_enable_absolute();
		return;
	}

	if ((mouseflags & MOUSEFLAGS_VMWARE_ENABLED)
	        && !(mouseflags & MOUSEFLAGS_VMWARE_SUSPENDED)) {
		/* Drain all complete VMware packets. QEMU queues one 4-word packet per
		 * host input event, while the fake PS/2 notification can be lost. Reading
		 * the entire queue prevents a permanent stale-event backlog. */
		for (;;) {
			uint32_t vmwstatus = vmware_abspointer_status();
			uint16_t data_avail;
			struct vmware_abspointer_data vmw;

			if ((vmwstatus & VMWARE_ABSPOINTER_STATUS_MASK_ERROR) == VMWARE_ABSPOINTER_STATUS_MASK_ERROR) {
				return;
			}

			data_avail = (uint16_t) (vmwstatus & VMWARE_ABSPOINTER_STATUS_MASK_DATA);
			if (data_avail < VMWARE_ABSPOINTER_DATA_PACKET_SIZE) {
				return;
			}

			vmware_abspointer_data(VMWARE_ABSPOINTER_DATA_PACKET_SIZE, &vmw);

			/* QEMU marks a packet relative if the vmmouse device has unexpectedly
			 * left absolute mode. Re-arm the interface immediately and discard this
			 * packet rather than applying relative coordinates to an absolute Windows
			 * driver. If re-arming fails, the callback-level recovery path retries on
			 * following PS/2 events. */
			if (vmw.status & VMWARE_ABSPOINTER_STATUS_RELATIVE) {
				mouseflags &= ~MOUSEFLAGS_VMWARE_ENABLED;
				vmware_try_enable_absolute();
				return;
			}

			report_vmware_event(&vmw);
		}
	}

	/* VMware backdoor unavailable: retain vbmouse's plain PS/2 fallback. */
	{
		int sstatus = 0;
		short sx = status & PS2M_STATUS_X_NEG ? (short) (0xFF00 | x) : (short) x;
		short sy = -(status & PS2M_STATUS_Y_NEG ? (short) (0xFF00 | y) : (short) y);
		unsigned char buttons = (unsigned char) status;

		(void) z;

		if (sx || sy) {
			sstatus |= SF_MOVEMENT;
		}

		if ((mousebtnstatus & PS2M_STATUS_BUTTON_1) && !(buttons & PS2M_STATUS_BUTTON_1)) {
			sstatus |= SF_B1_UP;
		} else if (!(mousebtnstatus & PS2M_STATUS_BUTTON_1) && (buttons & PS2M_STATUS_BUTTON_1)) {
			sstatus |= SF_B1_DOWN;
		}

		if ((mousebtnstatus & PS2M_STATUS_BUTTON_2) && !(buttons & PS2M_STATUS_BUTTON_2)) {
			sstatus |= SF_B2_UP;
		} else if (!(mousebtnstatus & PS2M_STATUS_BUTTON_2) && (buttons & PS2M_STATUS_BUTTON_2)) {
			sstatus |= SF_B2_DOWN;
		}

		mousebtnstatus = buttons & (PS2M_STATUS_BUTTON_1 | PS2M_STATUS_BUTTON_2);

		if (sstatus) {
			send_event(sstatus, sx, sy, MOUSE_NUM_BUTTONS, 0, 0);
		}
	}
}

/**
 * Raw BIOS callback wrapper.
 *
 * Win9x and 32-bit display code may depend on the upper halves of the 386
 * registers surviving IRQ12. Save full 32-bit general registers explicitly,
 * plus all segment registers that the C handler may use.
 */
static void __declspec(naked) __far ps2_mouse_callback(void)
{
	_asm {
		pushad
		push ds
		push es
		push fs
		push gs

		; 32 bytes from pushad + 8 bytes of segment registers = 40 bytes.
		; The far return address is another 4 bytes. The BIOS callback's four
		; word parameters therefore start at BP+44 (Z, Y, X, status).
		mov bp, sp

		; Compact-model DLL: load this driver's data segment explicitly.
		mov ax, SEG mouseflags
		mov ds, ax

		mov ax, [bp+50] ; status
		mov bx, [bp+48] ; X
		mov cx, [bp+46] ; Y
		mov dx, [bp+44] ; Z
		call ps2_mouse_handler

		pop gs
		pop fs
		pop es
		pop ds
		popad
		retf
	}
}

#if HOOK_INT2F

/** Interrupt 2F handler, called on Windows 386-mode display switches.
 * Keep this path deliberately minimal: Win95 can freeze if VMware backdoor
 * traffic is issued while the VDM/display switch itself is in progress.
 * Record only the newest direction and let the PS/2 callback perform the
 * actual vmmouse transition later. */
static void __declspec(naked) __far int2f_handler(void)
{
	_asm {
		; Preserve data segment
		push ds

		; Load our data segment
		push ax
		mov ax, SEG prev_int2f_handler
		mov ds, ax
		pop ax

		; Check functions we are interested in hooking
		cmp ax, 0x4001  ; Notify Background Switch
		je going_background
		cmp ax, 0x4002  ; Notify Foreground Switch
		je going_foreground

		; Otherwise directly jump to next handler
		jmp next_handler

	going_background:
		mov byte ptr [display_switch_pending], 1
		jmp next_handler

	going_foreground:
		mov byte ptr [display_switch_pending], 2

	next_handler:
		; Store the address of the previous handler
		push dword ptr [prev_int2f_handler]

		; Restore original data segment without touching the stack,
		; since we want to keep the previous handler address at the top
		push bp
		mov bp, sp
		mov ds, [bp + 6]  ; Stack looks like 0: bp, 2: prev_int2f_handler, 6: ds
		pop bp

		retf 2
	}
}

#endif /* HOOK_INT2F */

#pragma code_seg ()

/* Driver exported functions. */

/** DLL entry point (or driver initialization routine).
 * The initialization routine should check whether a mouse exists.
 * @return nonzero value indicates a mouse exists.
 */
#pragma off (unreferenced);
BOOL FAR PASCAL LibMain(HINSTANCE hInstance, WORD wDataSegment,
                        WORD wHeapSize, LPSTR lpszCmdLine)
#pragma pop (unreferenced);
{
	/* We are not going to bother checking whether a PS/2 mouse exists and just assume it does. */

#if HOOK_INT2F
	if (windows_386_enhanced_mode()) {
		mouseflags |= MOUSEFLAGS_HAS_WIN386;
	}
#endif

	if (vmware_detect()) {
		mouseflags |= MOUSEFLAGS_HAS_VMWARE;
	}

	/* Tell VMD what physical interrupt path this 16-bit driver is using. */
	if (mouseflags & MOUSEFLAGS_HAS_WIN386) {
		LPFN vmd_entry = win_get_vxd_api_entry(VMD_DEVICE_ID);
		if (vmd_entry) {
			vmd_set_mouse_type(&vmd_entry, VMD_TYPE_PS2, PS2_MOUSE_INT_VECTOR, 0);
		}
	}

	return 1;
}

/** Called by Windows to retrieve information about the mouse hardware. */
WORD FAR PASCAL Inquire(LPMOUSEINFO lpMouseInfo)
{
	/* If the LibMain probe failed, retry from this later Windows entry point. */
	if (!(mouseflags & MOUSEFLAGS_HAS_VMWARE) && vmware_detect()) {
		mouseflags |= MOUSEFLAGS_HAS_VMWARE;
	}

	lpMouseInfo->msExist = 1;
	lpMouseInfo->msRelative = mouseflags & MOUSEFLAGS_HAS_VMWARE ? 0 : 1;
	lpMouseInfo->msNumButtons = MOUSE_NUM_BUTTONS;
	lpMouseInfo->msRate = 40;
	return sizeof(MOUSEINFO);
}

/** Called by Windows to enable the mouse driver.
  * @param lpEventProc Callback function to call when a mouse event happens. */
VOID FAR PASCAL Enable(LPFN_MOUSEEVENT lpEventProc)
{
	/* Store the Windows-given callback. */
	cli(); /* Write to far pointer may not be atomic, and we could be interrupted mid-write. */
	eventproc = lpEventProc;
	sti();

	if (!(mouseflags & MOUSEFLAGS_ENABLED)) {
		int err;

		/* Configure the PS/2 BIOS and reset the mouse. */
		if ((err = ps2m_init(PS2_MOUSE_PLAIN_PACKET_SIZE))) {
			return;
		}

		/* Use the same basic PS/2 defaults as the original vbmouse driver. */
		ps2m_set_resolution(3);     /* 3 = 200 dpi, 8 counts per millimeter */
		ps2m_set_sample_rate(2);    /* 2 = 40 reports per second */
		ps2m_set_scaling_factor(1); /* 1 = 1:1 scaling */

		if ((err = ps2m_set_callback(ps2_mouse_callback))) {
			return;
		}

		if ((err = ps2m_enable(true))) {
			ps2m_set_callback(NULL);
			return;
		}

		mousebtnstatus = 0;
		mouseposvalid = false;
		mouseflags &= ~MOUSEFLAGS_VMWARE_SUSPENDED;
		mouseflags |= MOUSEFLAGS_ENABLED;

#if HOOK_INT2F
		display_switch_pending = DISPLAY_SWITCH_NONE;
#endif

		/* PS/2 initialization/reset can disable the vmmouse interface. Retry
		 * GETVERSION here even if an earlier probe succeeded, then enable the
		 * VMware-compatible absolute protocol. */
		vmware_try_enable_absolute();

#if HOOK_INT2F
		/* Hook DOS/display switching once the absolute interface has been detected,
		 * even if the first post-PS/2 absolute enable attempt was temporarily lost.
		 * The hook itself only records state and performs no VMPort I/O. */
		if ((mouseflags & MOUSEFLAGS_HAS_WIN386) && (mouseflags & MOUSEFLAGS_HAS_VMWARE)) {
			cli();
			hook_int2f(&prev_int2f_handler, int2f_handler);
			sti();
			mouseflags |= MOUSEFLAGS_INT2F_HOOKED;
		}
#endif
	}
}

/** Called by Windows to disable the mouse driver. */
VOID FAR PASCAL Disable(VOID)
{
	if (mouseflags & MOUSEFLAGS_ENABLED) {
#if HOOK_INT2F
		if (mouseflags & MOUSEFLAGS_INT2F_HOOKED) {
			cli();
			unhook_int2f(prev_int2f_handler);
			sti();
			mouseflags &= ~MOUSEFLAGS_INT2F_HOOKED;
		}
		display_switch_pending = DISPLAY_SWITCH_NONE;
#endif

		if (mouseflags & MOUSEFLAGS_VMWARE_ENABLED) {
			if (!(mouseflags & MOUSEFLAGS_VMWARE_SUSPENDED)) {
				vmware_disable_absolute();
			}
			mouseflags &= ~MOUSEFLAGS_VMWARE_ENABLED;
		}
		mouseflags &= ~MOUSEFLAGS_VMWARE_SUSPENDED;

		ps2m_enable(false);
		ps2m_set_callback(NULL);
		mouseflags &= ~MOUSEFLAGS_ENABLED;
	}
}

/** Called by Windows to retrieve the interrupt vector number used by this driver, or -1. */
int FAR PASCAL MouseGetIntVect(VOID)
{
	return PS2_MOUSE_INT_VECTOR;
}
