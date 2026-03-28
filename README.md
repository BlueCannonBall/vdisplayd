# vdisplayd

A minimal userspace daemon that creates a virtual display using the [EVDI](https://github.com/DisplayLink/evdi) kernel module. Useful for headless setups, virtual monitors, VNC targets, or screen recording without a physical display.

## Requirements

- Linux with X11
- [EVDI](https://github.com/DisplayLink/evdi) kernel module (DKMS)
- `libevdi`, `libX11`, `libXrandr` (dev packages)

On Arch:
```bash
paru -S evdi-dkms
sudo pacman -S libxrandr
```

## Build & Install

```bash
make
sudo make install   # installs to /usr/local/bin
```

## Usage

```bash
vdisplayd
```

The daemon:
1. Opens the EVDI device and advertises a 1920×1080 display via EDID
2. Links the EVDI provider to the primary GPU via XRandR
3. Processes frame updates at 60 Hz

After starting, enable the virtual output with xrandr:
```bash
xrandr --output DVI-I-1-1 --mode 1920x1080 --right-of eDP
```

Replace `eDP` with your primary output name (check `xrandr` output).

To disable:
```bash
xrandr --output DVI-I-1-1 --off
```

## How it works

EVDI creates a virtual DRM device with a fake connector. When X11 tries to display content on it (atomic commit), the kernel needs a userspace process to consume the pixels. Otherwise, the DRM `flip_done` fence times out and the display hangs for 10 seconds.

This daemon:
- Registers a framebuffer with the EVDI kernel module
- Responds to `mode_changed` events by allocating a matching buffer
- Calls `evdi_grab_pixels()` to signal frame completion to the kernel
