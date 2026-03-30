# vdisplayd

A minimal userspace daemon that creates a virtual display using the [EVDI](https://github.com/DisplayLink/evdi) kernel module. Useful for headless setups, virtual monitors, VNC targets, or screen recording without a physical display.

## Requirements

- Linux with X11
- [EVDI](https://github.com/DisplayLink/evdi) kernel module (DKMS)
- `libevdi`

On Arch:
```bash
paru -S evdi-dkms
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

After starting, enable the virtual output with xrandr:
```bash
xrandr --output DVI-I-1-1 --mode 1920x1080 --right-of eDP
```

Replace `eDP` with your primary output name (check `xrandr` output).

To disable:
```bash
xrandr --output DVI-I-1-1 --off
```
