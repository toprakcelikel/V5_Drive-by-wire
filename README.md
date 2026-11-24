# V5_Drive-by-wire
Updated code and PCB for light vehicle drive by wire.

This repository is for use with the **Drive by Wire** board created to be used for the ELCANO self-driving tricycle. It controls throttle (Ebike controller), brakes (solenoid) and steering (either linear servo or motor controller board with a linear actuator). All vehicle actions are logged to an SD card, which is the main data for debugging. Vehicle can be controlled by

1) 6-channel Radio Control (RC) unit

2) Operator controls (OP)

3) Vehicle automation computer via CAN. We have demonstrated a Jetson Nano using a Pixhawk to find GNSS points and move from waypoint to waypoint.

## Required Libraries

Tested versions are the ones the firmware is currently built with.

| Library | Header | Install from | Tested version |
|---|---|---|---|
| Arduino SAM Boards (Due core) | `SPI.h`, `Wire.h` | Boards Manager | 1.6.12 |
| due_can (Collin Kidder) | `due_can.h` | [GitHub ZIP](https://github.com/collin80/due_can) | 2.1.6 |
| can_common (Collin Kidder) | `can_common.h` (used by due_can) | [GitHub ZIP](https://github.com/collin80/can_common) | 0.4.0 |
| SD (Arduino) | `SD.h` | Library Manager | 1.3.0 |
| Servo (Arduino) | `Servo.h` | Library Manager | 1.2.2 |
| Time (Michael Margolis) | `TimeLib.h` | Library Manager | 1.6.1 |
| DS1307RTC (Michael Margolis) | `DS1307RTC.h` | Library Manager | 1.4.1 |
| RTClib (Adafruit) | `RTClib.h` | Library Manager (also installs Adafruit BusIO) | 2.1.4 |

Do not use Library Manager for due_can or can_common: it only has due_can 2.0.1 and does not list can_common. An old can_common causes errors like `'union BytesUnion' has no member named 'int16'`.

## To Install

1. In the Arduino IDE, go to Tools > Board > Boards Manager and install **Arduino SAM Boards (32-bits ARM Cortex-M3)**.
2. Go to Tools > Manage Libraries and install **SD**, **Servo**, **Time**, **DS1307RTC**, and **RTClib** (choose "Install all" when RTClib asks about dependencies).
3. Open your Arduino `libraries` folder (inside the Sketchbook location shown in File > Preferences) and delete any existing `can_common` or `due_can` folders.
4. Download the ZIPs (Code > Download ZIP) from [collin80/can_common](https://github.com/collin80/can_common) and [collin80/due_can](https://github.com/collin80/due_can), then add each one with Sketch > Include Library > Add .ZIP Library.
5. Clone this repository:
```
git clone https://github.com/elcano/V5_Drive-by-wire.git
```
6. Copy `Drive_By_Wire/SettingsTemplate.h` to `Drive_By_Wire/Settings.h` and edit it for your vehicle (name, steering method, speed and steering limits). `Settings.h` is vehicle-specific and ignored by git, so the sketch will not compile until you create it. Do not commit it.
7. Open `Drive_By_Wire/Drive_By_Wire.ino` in the Arduino IDE.

## Navigating Arduino IDE: 
* **Selecting Board:** Tools -> Board -> Arduino SAM Boards -> Arduino Due
* **Selecting Serial Port:** Tools -> Port -> COM (Arduino Due)
* **Accessing Serial Monitor:** Right-hand Corner -> Select Baud Rate (115200)

## Drive-by-Wire: 
* **Low-level board code with RC control support with existing code for brakes, steering, and throttle.** 
