// SPDX-License-Identifier: GPL-2.0+
/*
 *  HID driver for obtaining VXE Dragonfly R1 Pro Max mouse battery status.
 *
 *  Copyright (c) 2025 Dominykas Svetikas <dominykas@svetikas.lt>
 */

#include "linux/hid.h"
#include "linux/printk.h"
#include <linux/module.h>
#include <linux/atm_eni.h>
#include <linux/init.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/hid.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/usb.h>
#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/power_supply.h>
#include <linux/device.h>


// Define the interface number we want to poll for battery status
#define TARGET_INTERFACE 1
// Define polling interval for battery status in milliseconds
#define BATTERY_POLL_INTERVAL_MS 5000
// Define the request packet for battery status
#define REPORT_ID 0x08

// Structure to hold device-specific data
struct vxe_mouse {
    struct hid_device *hdev;
    struct timer_list battery_poll_timer;
    struct work_struct battery_request_work;

    char psy_name[32];

    // Fields for power supply management
    struct power_supply *power_supply;
    int battery_capacity; // To store the last known capacity
    int battery_status;   // To store the last known status (e.g., charging/discharging)
    int battery_voltage;  // To store the last known voltage
};

// Declare supported power supply properties
static enum power_supply_property vxe_power_supply_props[] = {
    POWER_SUPPLY_PROP_STATUS,
    POWER_SUPPLY_PROP_CAPACITY,
    POWER_SUPPLY_PROP_CAPACITY_LEVEL,
    POWER_SUPPLY_PROP_VOLTAGE_NOW,

    POWER_SUPPLY_PROP_SCOPE,
    POWER_SUPPLY_PROP_MODEL_NAME,
    POWER_SUPPLY_PROP_MANUFACTURER,
    POWER_SUPPLY_PROP_SERIAL_NUMBER,
};

// Callback function to get properties
static int vxe_power_supply_get_property(
    struct power_supply *psy,
    enum power_supply_property psp,
    union power_supply_propval *val
) {
    struct vxe_mouse *vxe_dev = power_supply_get_drvdata(psy);
    int ret = 0;

    if (!vxe_dev)
        return -EINVAL;

    switch (psp) {
    case POWER_SUPPLY_PROP_STATUS:
        val->intval = vxe_dev->battery_status;
        break;
    case POWER_SUPPLY_PROP_CAPACITY:
        val->intval = vxe_dev->battery_capacity;
        break;
    case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
        val->intval = vxe_dev->battery_capacity;
        break;
    case POWER_SUPPLY_PROP_VOLTAGE_NOW:
        val->intval = vxe_dev->battery_voltage * 1000; // Convert mV to uV
        break;
    case POWER_SUPPLY_PROP_SCOPE:
        val->intval = POWER_SUPPLY_SCOPE_DEVICE;
        break;
    case POWER_SUPPLY_PROP_MODEL_NAME:
        val->strval = "VXE Dragonfly R1 Pro Max Mouse";
        break;
    case POWER_SUPPLY_PROP_MANUFACTURER:
        val->strval = "VXE";
        break;
    case POWER_SUPPLY_PROP_SERIAL_NUMBER:
        val->strval = vxe_dev->hdev->uniq;
        break;
    default:
        ret = -EINVAL;
        break;
    }
    return ret;
}

/*
 * Retrieves the output reports from the HID device and sends a request
 * to the mouse to get the battery status.
 * This function is scheduled to run in a process context via a workqueue.
 */
static void vxe_battery_work_handler(struct work_struct *work) {
    // Get the containing vxe_mouse structure from the work_struct and access the hid_device
    struct vxe_mouse *vxe_dev = container_of(work, struct vxe_mouse, battery_request_work);
    struct hid_device *hdev = vxe_dev->hdev;

    struct list_head *report_list = &hdev->report_enum[HID_OUTPUT_REPORT].report_list;
    if (list_empty(report_list)) {
        hid_err(hdev, "no output reports found\n");
        return;
    }
    
    struct hid_report *report;
    /*hid_info(hdev, "Output reports:\n");
    list_for_each_entry(report, report_list, list) {
        hid_info(
            hdev, "Report ID: %d, Max Fields: %d, Max Field Size: %d\n",
            report->id, report->maxfield, report->maxfield
        );
        for (int i = 0; i < report->maxfield; i++) {
            struct hid_field *field = report->field[i];
            hid_info(
                hdev, "Field %d: Size: %d, Offset: %d, Report Count: %d\n",
                i, field->report_size, field->report_offset, field->report_count
            );
            if (field->report_count > 0)
                hid_info(
                    hdev, "Field %d has %d report counts\n",
                    i, field->report_count
                );
        }
    }*/

    report = list_first_entry(report_list, struct hid_report, list);
    if (report->id != REPORT_ID) {
        hid_err(hdev, "invalid report id\n");
        return;
    }

    if (report->field[0]->report_count < 16) {
        hid_err(hdev, "not enough values in the field\n");
        return;
    }

    report->field[0]->value[0] = 0x04; // Command
    report->field[0]->value[15] = 0x49; // Checksum

    hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
    hid_info(hdev, "hw_request sent for battery status\n");
}

/*
 * Timer callback function to periodically request battery status.
 * This function is called by the kernel timer when it expires.
 */
static void vxe_request_battery_status(struct timer_list *t) {
    // Get the containing vxe_mouse structure from the timer_list
    struct vxe_mouse *vxe_dev = from_timer(vxe_dev, t, battery_poll_timer);
    // Schedule the work item to run the actual HID request in a process context
    schedule_work(&vxe_dev->battery_request_work);
    // Reschedule the timer to run again after BATTERY_POLL_INTERVAL_MS
    mod_timer(&vxe_dev->battery_poll_timer, jiffies + msecs_to_jiffies(BATTERY_POLL_INTERVAL_MS));
}

/**
 * Initializes the device, sets up the workqueue and timer for battery polling on interface 1.
 * This function is called when the HID device is probed.
 */
static int vxe_probe(struct hid_device *hdev, const struct hid_device_id *id) {
    // Get the current USB interface number
    struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
    int ifnum = intf->cur_altsetting->desc.bInterfaceNumber;

    // Parse the HID descriptor
    int ret = hid_parse(hdev);
    if (ret) {
        hid_err(hdev, "hid_parse failed with error %d\n", ret);
        return ret;
    }

    // Start the HID hardware for I/O operations
    ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
    if (ret) {
        hid_err(hdev, "hid_hw_start failed with error %d\n", ret);
        return ret;
    }

    // Only set up battery polling on the specific interface we care about
    if (ifnum == TARGET_INTERFACE) {
        hid_info(
            hdev,
            "HID device found with Vendor ID: 0x%04x, Product ID: 0x%04x on If=%d, starting battery polling\n",
            id->vendor, id->product, ifnum
        );

        // Allocate memory for custom device structure
        struct vxe_mouse *vxe_dev;
        vxe_dev = kzalloc(sizeof(*vxe_dev), GFP_KERNEL);
        if (!vxe_dev) {
            hid_err(hdev, "Failed to allocate vxe_mouse structure\n");
            hid_hw_stop(hdev); // Stop hardware if memory allocation fails
            return -ENOMEM;
        }
        // Store the hid_device pointer in the custom structure
        vxe_dev->hdev = hdev;
        // Associate custom data with the HID device, so it can be retrieved later
        hid_set_drvdata(hdev, vxe_dev);

        snprintf(vxe_dev->psy_name, sizeof(vxe_dev->psy_name),
                  "vxe_%04x_%04x_bat_%d", id->vendor, id->product, ifnum);

        // Initialize the power supply description structure dynamically
        struct power_supply_desc *desc = devm_kzalloc(&hdev->dev, sizeof(*desc), GFP_KERNEL);
        if (!desc) {
            hid_err(hdev, "Failed to allocate power_supply_desc\n");
            hid_hw_stop(hdev);
            return -ENOMEM;
        }

        desc->name = vxe_dev->psy_name;
        desc->type = POWER_SUPPLY_TYPE_BATTERY;
        desc->properties = vxe_power_supply_props;
        desc->num_properties = ARRAY_SIZE(vxe_power_supply_props);
        desc->get_property = vxe_power_supply_get_property;
        desc->set_property = NULL;

        struct power_supply_config psy_cfg = {
            .drv_data = vxe_dev,
        };

        vxe_dev->power_supply = devm_power_supply_register(&hdev->dev, desc, &psy_cfg);
        if (IS_ERR(vxe_dev->power_supply)) {
            ret = PTR_ERR(vxe_dev->power_supply);
            hid_err(hdev, "Failed to register power supply: %d\n", ret);
            kfree(vxe_dev);
            hid_hw_stop(hdev);
            return ret;
        }

        // Initialize battery status fields with an unknown state
        vxe_dev->battery_capacity = -1;
        vxe_dev->battery_status = POWER_SUPPLY_STATUS_UNKNOWN;
        vxe_dev->battery_voltage = 0;

        // Initialize the workqueue item with a handler function.
        INIT_WORK(&vxe_dev->battery_request_work, vxe_battery_work_handler);
        // Initialize the timer
        timer_setup(&vxe_dev->battery_poll_timer, vxe_request_battery_status, 0);

        // Start the timer immediately to get the first battery status
        // It reschedule itself periodically in the callback
        mod_timer(&vxe_dev->battery_poll_timer, jiffies + msecs_to_jiffies(100));
    }

    // Return 0 to indicate a successful probe
    return 0;
}

static int vxe_raw_event(
    struct hid_device *hdev,
    struct hid_report *report,
    u8 *data, int size
) {
    
    struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
    int ifnum = intf->cur_altsetting->desc.bInterfaceNumber;
    // Detect battery information packet
    if (size == 17 && data[0] == 0x08 && data[1] == 0x04) {
        struct vxe_mouse *vxe_dev = hid_get_drvdata(hdev);
        hid_info(hdev, "vxe_raw_event: === Detected a 17-byte battery data packet! ===\n");
        hid_info(hdev, "vxe_raw_event: Interface Number: %d\n", ifnum);

        int batteryLevel = data[6];
        int batteryCharge = data[7];
        int voltage = (data[8] << 8) | data[9];

        if (batteryCharge == 0x00) {
            hid_info(hdev, "Battery Status: Not Charging\n");
        } else if (batteryCharge == 0x01) {
            hid_info(hdev, "Battery Status: Charging\n");
        } else {
            hid_info(hdev, "Battery Status: Unknown (%d)\n", batteryCharge);
        }

        hid_info(hdev, "Battery Level: %d%%\n", batteryLevel);
        hid_info(hdev, "Battery Voltage: %d mV\n", voltage);

        vxe_dev->battery_capacity = data[6];
        vxe_dev->battery_status = (batteryCharge == 0x01) ? POWER_SUPPLY_STATUS_CHARGING : POWER_SUPPLY_STATUS_DISCHARGING;
        vxe_dev->battery_voltage = voltage;
    }
    return 0;
}

static void vxe_remove(struct hid_device *hdev) {
    pr_info("vxe_remove: Device being removed.\n");
    
    // Stop new work/timers and wait for existing work to finish
    struct vxe_mouse *vxe_dev = hid_get_drvdata(hdev);
    if (vxe_dev) {
        // Delete the timer if it was active
        del_timer_sync(&vxe_dev->battery_poll_timer);
        // Cancel any pending workqueue items and wait for them to complete
        cancel_work_sync(&vxe_dev->battery_request_work);
    }

    // Stop the HID hardware operations
    hid_hw_stop(hdev);

    // Free custom data and clear the driver data
    if (vxe_dev) {
        kfree(vxe_dev);
        hid_set_drvdata(hdev, NULL);
    }
}

static const struct hid_device_id vxe_devices[] = {
    { HID_USB_DEVICE(0x3554, 0xf58a) }, // wireless dongle
    { HID_USB_DEVICE(0x3554, 0xf58c) }, // wired
    { }
};
MODULE_DEVICE_TABLE(hid, vxe_devices);

static struct hid_driver vxe_driver = {
    .name = "vxe-dragonfly-r1-pro-max",
    .id_table = vxe_devices,
    .probe = vxe_probe,
    .raw_event = vxe_raw_event,
    .remove = vxe_remove,
};
module_hid_driver(vxe_driver);

MODULE_AUTHOR("Dominykas Svetikas <dominykas@svetikas.lt>");
MODULE_DESCRIPTION("HID driver for obtaining VXE Dragonfly R1 Pro Max mouse battery status.");
MODULE_LICENSE("GPL");
