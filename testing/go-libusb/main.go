package main

import (
	"fmt"
	"log"
	"time"

	"github.com/karalabe/hid"
)

const (
	// Mouse vendorID
	vendorID uint16 = 0x3554
	// Mouse productID
	productID uint16 = 0xf58a
	// The name that will appear in UPower
	deviceName = "VXE Dragonfly R1 Pro Max"
	// How often to check the battery
	pollInterval = 120 * time.Second
)

const batteryQueryReportId = 0x8
const batteryQueryResponseId = 0x8

const offset = 5
const packetSize = 17

var batteryRequestReport = []byte{
	batteryQueryReportId, // Report ID
	0x04,                 // Most likely the Command ID
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x49,
}

var sampleBatteryResponse = []byte{
	0x08, // Report ID
	0x04, // Command ID
	0x00, // Command Status
	0x00, // EEPROM Address (high byte)
	0x00, // EEPROM Address (low byte)
	0x02,
	0x41, // Battery Level
	0x00, // Battery Charge
	0x0f, // Battery Voltage (high byte)
	0x83, // Battery Voltage (low byte)
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x74, // Checksum
}

// getBatteryLevel sends the request and parses the battery level from the response.
func getBatteryLevel(device *hid.Device) (level int, status string, err error) {
	// Send the battery query request
	_, err = device.Write(batteryRequestReport)
	if err != nil {
		return 0, "Unknown", fmt.Errorf("failed to send feature report: %w", err)
	}

	// Give the device a moment to process
	time.Sleep(50 * time.Millisecond)

	// Buffer to read the response
	response := make([]byte, packetSize)
	_, err = device.Read(response)
	if err != nil {
		return 0, "Unknown", fmt.Errorf("failed to read response: %w", err)
	}

	if len(response) == packetSize && response[0] == batteryQueryResponseId {
		batteryLevel := response[offset+1]
		batteryCharge := response[offset+2]
		batteryVoltage := (int(response[offset+3]) << 8) | int(response[offset+4])

		fmt.Println(len(response), response)
		log.Printf("Battery Level: %d%%\n", batteryLevel)
		log.Printf("Battery Charge: %d\n", batteryCharge)
		log.Printf("Battery Voltage: %d mV\n", batteryVoltage)
	}
	return 0, "Unknown", fmt.Errorf("unexpected response: %x", response)
}

func main() {

	devices := hid.Enumerate(vendorID, productID)
	if len(devices) == 0 {
		log.Fatalf("Error: No HID devices found with VID=%#x PID=%#x.", vendorID, productID)
	}
	log.Printf("Found %d HID devices named %s %s.", len(devices), devices[0].Manufacturer, devices[0].Product)

	// TODO: figure out how to get the specific device that works without resorting to specifying the path
	for _, device := range devices {
		if device.Interface != 1 {
			continue
		}
		log.Println("Opening device at", device.Path)

		openDev, err := device.Open()
		if err != nil {
			log.Fatalf("Error opening device: %v", err)
		}

		// Query the battery level
		level, status, err := getBatteryLevel(openDev)
		if err != nil {
			log.Printf("Error getting battery level: %v", err)
		} else {
			log.Printf("Battery Level: %d%%, Status: %s", level, status)
		}

		openDev.Close()
		break
	}
}
