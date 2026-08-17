# This is an Open Watcom wmake makefile, not GNU make.
# Assuming you have sourced `owsetenv` beforehand.

.BEFORE:
	# We need DOS and Windows headers, not host platform's
	set include=$(%watcom)/h/win;$(%watcom)/h

# The main driver file
qemouse.drv: mousew16.c mousew16.h vmware.h ps2.h int2fwin.h
	# -bd to build DLL
	# -mc to use compact memory model (far data pointers, since ss != ds)
	# -zu for DLL calling convention (ss != ds)
	# -zc put constants on the code segment (cs)
	# -s to disable stack checks, since the runtime uses MessageBox() to abort (which we can't call from mouse.drv)
	wcl -6 -mc -bd -zu -zc -s -bt=windows -l=windows_dll @qemouse.lnk -fe=$^@ mousew16.c

clean: .SYMBOLIC
	rm -f qemouse.drv qemouse.flp *.o

qemouse.flp:
	mformat -C -f 1440 -v QEMOUSE -i $^@ ::

# Build a floppy image containing the driver
flp: qemouse.flp qemouse.drv oemsetup.inf .SYMBOLIC
	mcopy -i qemouse.flp -o oemsetup.inf qemouse.drv ::
