# Open Watcom wmake makefile.
# The Open Watcom environment must already be configured for the Windows target.

!ifndef VERSION
VERSION = dev
!endif

DRIVER  = qemouse.drv
SOURCE  = src/mousew16.c
HEADERS = src/mousew16.h src/vmware.h src/ps2.h src/int2fwin.h

$(DRIVER): $(SOURCE) $(HEADERS) qemouse.lnk
	@echo Building QEMouse $(VERSION)
	# -bd builds a DLL/driver
	# -mc uses the compact memory model (far data pointers, since SS != DS)
	# -zu uses the DLL calling convention (SS != DS)
	# -zc places constants in the code segment (CS)
	# -s disables stack checks; the runtime abort path is unsuitable for mouse.drv
	wcl -6 -mc -bd -zu -zc -s -bt=windows -l=windows_dll @qemouse.lnk -fe=$^@ $(SOURCE)

clean: .SYMBOLIC
	rm -f qemouse.drv qemouse.flp *.o *.obj src/*.o src/*.obj

qemouse.flp:
	mformat -C -f 1440 -v QEMOUSE -i $^@ ::

# Build a floppy image containing the driver and OEM setup file.
flp: qemouse.flp qemouse.drv oemsetup.inf .SYMBOLIC
	mcopy -i qemouse.flp -o oemsetup.inf qemouse.drv ::
