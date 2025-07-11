# battery

This repository contains a kernel module driver for VXE Dragonfly R1 Mouse to retrieve battery information.

- `/module` folder contains the actual driver code.
- `/investigation` folder contains files for investigation purposes.
- `/testing` folder contains working reimplementations of the protocol in higher level abstractions such as libusb or hidraw in userspace.

Article documenting the initial experience:
https://svetikas.lt/en/posts/2025/06/the-mouse-that-made-me-write-a-kernel-module/

## Special thanks

Shout out to [`hid-dr.c`](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/hid/hid-dr.c?h=v6.16-rc1)
and [`hid-logitech-hidpp.c`](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/hid/hid-logitech-hidpp.c?h=v6.16-rc1)

## Future work

- Figure out what to do when device is both plugged in via wire and via dongle as 2 batteries show up
- Shift udev rule to be resolved at driver level, not sure how
