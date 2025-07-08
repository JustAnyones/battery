#include <stdio.h>
#include <libusb.h>
#include <unistd.h>

#define VENDOR_ID  0x3554
#define PRODUCT_ID 0xf58a
#define INTERFACE  1
#define ENDPOINT_IN  0x82  // Interrupt IN endpoint for interface 1
#define REPORT_ID  0x08
#define REPORT_TYPE_OUTPUT 0x02

int main(void) {
    libusb_device_handle *handle;
    libusb_context *ctx = NULL;
    int r;

    r = libusb_init(&ctx);
    if (r < 0) {
        fprintf(stderr, "libusb init error\n");
        return 1;
    }

    handle = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if (!handle) {
        fprintf(stderr, "Cannot open device\n");
        libusb_exit(ctx);
        return 1;
    }

    // A part of my suffering
    if (libusb_kernel_driver_active(handle, INTERFACE) == 1) {
        libusb_detach_kernel_driver(handle, INTERFACE);
    }

    if ((r = libusb_claim_interface(handle, INTERFACE)) != 0) {
        fprintf(stderr, "Failed to claim interface: %s\n", libusb_error_name(r));
        libusb_close(handle);
        libusb_exit(ctx);
        return 1;
    }

    unsigned char data[17] = {
        0x08, 0x04, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x49
    };

    // SET_REPORT (HID) via control transfer
    uint8_t bmRequestType = 0x21;  // Host to device | Class | Interface
    uint8_t bRequest = 0x09;       // SET_REPORT
    uint16_t wValue = (REPORT_TYPE_OUTPUT << 8) | REPORT_ID;
    uint16_t wIndex = INTERFACE;

    r = libusb_control_transfer(handle, bmRequestType, bRequest,
                                wValue, wIndex, data, sizeof(data), 1000);
    if (r < 0) {
        fprintf(stderr, "Control transfer failed: %s\n", libusb_error_name(r));
    } else {
        printf("SET_REPORT sent (%d bytes)\n", r);
    }

    usleep(10000);  // 10ms

    // Read interrupt IN data
    unsigned char in_data[17];
    int transferred = 0;
    r = libusb_interrupt_transfer(handle, ENDPOINT_IN, in_data, sizeof(in_data), &transferred, 1000);
    if (r == 0 && transferred > 0) {
        printf("Received %d bytes from device:\n", transferred);
        for (int i = 0; i < transferred; i++) {
            printf("%02x ", in_data[i]);
        }
        printf("\n");
    } else {
        fprintf(stderr, "Interrupt read failed: %s\n", libusb_error_name(r));
        usleep(10000); // 10 ms
    }

    libusb_release_interface(handle, INTERFACE);
    libusb_close(handle);
    libusb_exit(ctx);
    return 0;
}
