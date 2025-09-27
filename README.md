
# ESP32 PCA9685 Servo & Continuous Rotation Controller

A WiFi-enabled ESP32 project for controlling up to 16 servos or continuous-rotation motors via a PCA9685 PWM driver, with a browser-based UI and REST API. Supports smooth position moves, continuous spin (like a DC motor), and calibration for center/range.

## Features
- ESP32 + PCA9685 (I2C) — up to 16 channels
- Web UI (XY pad, spin left/right, stop, calibration)
- REST API for all controls
- WiFi AP + STA (connects to your network, or use AP mode)
- Credentials in `.env` (never publish your WiFi password!)
- SPIFFS filesystem for web assets

## Wiring
- ESP32 SDA (GPIO21) → PCA9685 SDA
- ESP32 SCL (GPIO22) → PCA9685 SCL
- PCA9685 VCC → ESP32 3.3V
- PCA9685 GND → ESP32 GND
- Servo/ESC power: PCA9685 V+ to 5V (external supply recommended)
- Common ground between ESP32, PCA9685, and servo/ESC supply

## Setup
1. Clone this repo
2. Create `.env` in the project root:
	```
	STA_SSID=YourWiFiSSID
	STA_PASS=YourWiFiPassword
	```
3. Build and upload firmware & filesystem:
	```powershell
	# Build filesystem image
	platformio run --target buildfs --environment esp32dev
	# Upload filesystem image
	platformio run --target uploadfs --environment esp32dev
	# Upload firmware
	platformio run --target upload --environment esp32dev
	```
4. Open Serial Monitor (115200 baud) to see AP/STA IPs
5. Connect to AP or your WiFi, open browser to the IP shown

## Web UI
- Drag XY pad for spin control
- Spin Left/Right buttons for continuous rotation
- Stop/Center buttons
- Calibration controls (API)

## REST API
- `/api/spin?ch=0&speed=100` — set spin speed (-100..100 or -180..180)
- `/api/spinleft?ch=0` — spin left (full speed)
- `/api/spinright?ch=0` — spin right (full speed)
- `/api/stop?ch=0` — stop (center pulse)
- `/api/setcenter?ch=0&center=1500` — set center pulse (µs)
- `/api/setrange?ch=0&range=1000` — set range (µs)
- `/api/status?ch=0` — get channel status
- `/api/ip` — get AP/STA IPs


## How to Improve This Project

Want to make this project even better? Here are some ideas:
- Add authentication for the web UI and API
- Support WiFi configuration via browser (captive portal)
- Add mDNS/Bonjour for easy device discovery
- Improve the UI (mobile-friendly, more controls, calibration wizard)
- Add support for other PWM drivers (e.g., TLC5940)
- Add OTA firmware updates
- Add logging and diagnostics endpoints
- Support MQTT or other IoT protocols
- Add unit tests and CI integration

## Contributing

Pull requests are welcome! For major changes, please open an issue first to discuss what you would like to change.

Please make sure to update tests as appropriate and follow the code style used in this repo.

## Issues & Feedback

If you find a bug or have a feature request, please open an issue on GitHub. You can also use the Discussions tab for questions and ideas.
