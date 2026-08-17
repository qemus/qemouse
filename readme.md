<h1 align="center">QEMouse<br />
<div align="center">
<a href="https://github.com/qemus/qemouse"><img src="https://raw.githubusercontent.com/qemus/qemouse/master/.github/logo.png" title="Logo" style="max-width:100%;" width="128" /></a>
</div>
<div align="center">
  
[![Build]][build_url]
[![Version]][release_url]
[![Size]][release_url]

</div></h1>

QEMouse is a small Windows mouse driver for QEMU's `vmmouse` implementation.

It uses QEMU's VMware-compatible backdoor protocol to obtain absolute pointer coordinates while retaining the PS/2 interrupt path as the notification source.

## Features ✨

- QEMU absolute pointer coordinates
- Supports Windows 3.11, 95, 98 and ME
- Windows 386/VMD mouse type registration
- Fullscreen/background switching through the existing INT 2F notification hook
- Falls back to normal relative PS/2 input when the VMware-compatible interface is unavailable
- Uses Windows' existing enhanced-mode mouse stack; no additional VxD is required

## Design 🧩

QEMU's vmmouse implementation queues each host input event as four VMware backdoor words and generates a fake PS/2 mouse event to notify the guest. The PS/2 BIOS callback is therefore used only as a wakeup mechanism when VMware absolute mode is active.

On each notification QEMouse:

1. Queries VMware absolute-pointer status.
2. Drains every complete 4-word packet currently queued.
3. Reports absolute coordinates directly to Windows using `SF_ABSOLUTE`.
4. Converts VMware's current left/right button state into Windows button transition events.

Draining the complete queue is intentional. QEMU can retain stale vmmouse packets if a fake PS/2 notification is missed; consuming all complete packets prevents the guest pointer from developing a permanent event backlog.

## Installation 📦

### Windows 95 / 98 / ME

For an existing Windows installation, place `qemouse.inf` and `qemouse.drv` in the same directory.

Right-click `qemouse.inf` and choose **Install**, then reboot Windows.

### Windows 3.x

Copy `qemouse.drv` to the Windows `SYSTEM` directory and set:

    [boot]
    mouse.drv=qemouse.drv

in `SYSTEM.INI`, then restart Windows.

## QEMU configuration 🖥️

QEMU's VMware-compatible VMPort interface must be available to the guest.

For machine types where VMPort is not enabled automatically, enable it with:

    -machine vmport=on

QEMouse detects the VMware-compatible backdoor and absolute-pointer interface when Windows loads the driver.

If the absolute-pointer interface is unavailable, QEMouse falls back to conventional relative PS/2 mouse input.

## Building 🛠️

QEMouse uses Open Watcom and the Windows headers supplied with it.

After loading an Open Watcom environment, run:

    wmake qemouse.drv

To build a 1.44 MB floppy image containing `qemouse.drv` and `qemouse.inf`:

    wmake flp

## Acknowledgements 🙏

Special thanks to [Javier S. Pedro](https://javispedro.com/), this project would not exist without his invaluable work.

## Stars 🌟

[![Stargazers](https://raw.githubusercontent.com/star-stats/stars/refs/heads/data/charts/qemus-qemouse.svg)](https://github.com/qemus/qemouse/stargazers)

[build_url]: https://github.com/qemus/qemouse/
[release_url]: https://github.com/qemus/qemouse/releases/

[Build]: https://github.com/qemus/qemouse/actions/workflows/build.yml/badge.svg
[Size]: https://img.shields.io/badge/size-3_KB-steelblue?style=flat&color=066da5
[Version]: https://img.shields.io/github/v/tag/qemus/qemouse?label=version&sort=semver&color=066da5
