#include <dirent.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h> 

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define TARGET_VENDOR  0x3554
#define TARGET_PRODUCT 0xfffff58a

// Utility for parsing HID report descriptors
#define ITEM_TYPE_MAIN     0x00
#define ITEM_TYPE_GLOBAL   0x01
#define ITEM_TYPE_LOCAL    0x02

#define TAG_USAGE_PAGE     0x00
#define TAG_USAGE          0x00
#define TAG_COLLECTION     0x0A
#define TAG_END_COLLECTION 0x0C

// Utility function that parses and prints the collection info from a HID report descriptor
int parse_hid_descriptor(const uint8_t *desc, size_t size) {
    uint16_t usage_page = 0;
    uint16_t usage = 0;
    int collection_depth = 0;
    int top_level_collections = 0;

    size_t i = 0;
    while (i < size) {
        uint8_t prefix = desc[i];
        uint8_t size_code = prefix & 0x03;
        uint8_t type = (prefix >> 2) & 0x03;
        uint8_t tag  = (prefix >> 4) & 0x0F;

        size_t data_len;
        switch (size_code) {
            case 0: data_len = 0; break;
            case 1: data_len = 1; break;
            case 2: data_len = 2; break;
            case 3: data_len = 4; break; // Long format (rare)
            default: data_len = 0;
        }

        if (prefix == 0xFE) { // Long item - skip
            i += 2 + desc[i+1];
            continue;
        }

        if (i + 1 + data_len > size) break;

        uint32_t value = 0;
        for (size_t j = 0; j < data_len; ++j) {
            value |= desc[i + 1 + j] << (j * 8);
        }

        // Handle items
        if (type == ITEM_TYPE_GLOBAL && tag == TAG_USAGE_PAGE) {
            usage_page = value;
        } else if (type == ITEM_TYPE_LOCAL && tag == TAG_USAGE) {
            usage = value;
        } else if (type == ITEM_TYPE_MAIN && tag == TAG_COLLECTION) {
            if (collection_depth == 0) {
                ++top_level_collections;
                printf("Top-Level Collection #%d\n", top_level_collections);
                printf("  Usage Page: 0x%04X\n", usage_page);
                printf("  Usage:      0x%04X\n", usage);
                printf("  Type:       0x%02X\n", (uint8_t)value);
            }
            ++collection_depth;
        } else if (type == ITEM_TYPE_MAIN && tag == TAG_END_COLLECTION) {
            --collection_depth;
        }

        i += 1 + data_len;
    }

    printf("Total Top-Level Collections: %d\n", top_level_collections);

    return top_level_collections;
}

// Checks if the report descriptor of the device matches the expected format
// Specifically, it checks if the number of top-level collections is 6.
bool report_descriptor_matches(int fd) {
    int desc_size;
    if (ioctl(fd, HIDIOCGRDESCSIZE, &desc_size) < 0) {
        perror("HIDIOCGRDESCSIZE");
        return false;
    }

    struct hidraw_report_descriptor rpt_desc;
    rpt_desc.size = desc_size;
    if (ioctl(fd, HIDIOCGRDESC, &rpt_desc) < 0) {
        perror("HIDIOCGRDESC");
        return false;
    }

    return parse_hid_descriptor(rpt_desc.value, rpt_desc.size) == 6; // Check if collections.length === 6
}

// Finds a hidraw device that matches the target vendor and product IDs
// and has a report descriptor with 6 collections.
void find_hidraw_device(char *found_device_path) {
    DIR *dir;
    struct dirent *entry;

    dir = opendir("/dev");
    if (!dir) {
        perror("opendir");
        exit(EXIT_FAILURE);
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "hidraw", 6) == 0) {
            char path[64];
            snprintf(path, sizeof(path), "/dev/%s", entry->d_name);

            int fd = open(path, O_RDWR | O_NONBLOCK);
            if (fd < 0) continue;

            struct hidraw_devinfo info;
            memset(&info, 0, sizeof(info));

            if (ioctl(fd, HIDIOCGRAWINFO, &info) == 0) {
                if (info.vendor == TARGET_VENDOR && info.product == TARGET_PRODUCT && report_descriptor_matches(fd)) {
                    strcpy(found_device_path, path);
                    close(fd);
                    break;
                }
            }
            close(fd);
        }
    }

    closedir(dir);
}

int main() {
    char device_path[64] = {0};
    find_hidraw_device(device_path);

    if (strlen(device_path) == 0) {
        fprintf(stderr, "Target HID device not found\n");
        return 1;
    }
    printf("Found HID device at: %s\n", device_path);

    int fd = open(device_path, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    unsigned char report[] = {
        0x08, // Report ID
        0x04, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x49
    };
    ssize_t res = write(fd, report, sizeof(report));
    if (res < 0) {
        perror("write");
        close(fd);
        return 1;
    }
    printf("Report sent successfully (%ld bytes)\n", res);


    unsigned char buf[64];
    while (1) {
        res = read(fd, buf, sizeof(buf));
        if (res > 0) {
            printf("Received input report (%ld bytes):\n", res);
            for (int i = 0; i < res; ++i)
                printf("%02hhx ", buf[i]);
            printf("\n");
            break; // Exit after first successful read
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                perror("read");
            usleep(100000); // 100ms delay
        }
    }

    close(fd);
    return 0;
}
