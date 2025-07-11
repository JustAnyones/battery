# battery

`/investigation` folder contains files for investigation purposes.

`/testing` folder contains working reimplementations of the protocol in higher level abstractions such as libusb or hidraw in userspace.

Shout out to [`hid-dr.c`](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/hid/hid-dr.c?h=v6.16-rc1)
and [`hid-logitech-hidpp.c`](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/hid/hid-logitech-hidpp.c?h=v6.16-rc1)


TODO:
- figure out some power display issues
- figure out what to do when device is both plugged in via wire and via dongle as 2 batteries show up
