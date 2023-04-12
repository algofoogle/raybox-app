# Raybox: App version (compiled code)

This is a simple ray casting game demo, i.e. a game resembling Wolf3D. It is written
in C++. I'm doing this to familiarise myself with how this works, so I can then try
to implement it in hardware using an FPGA, and possibly later an ASIC.

## Prerequisites

```bash
sudo apt install libsdl2-2.0-0 libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev
```

## Building

Makefile actions:
```bash
make clean
make raybox
make run
```
