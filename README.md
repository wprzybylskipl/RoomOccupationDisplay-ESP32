# RoomOccupationDisplay-ESP32
This project uses the Freenove ESP32-32E board with a 4-inch touch display (320x480) to provide a live status of room availability. Booking data is pulled directly from a public Google Calendar using the iCalendar (ICS) format.

# Features 
- LIVE Status: Presents the current room status (Free/Busy) and indicates when the room will be free ([ BUSY / FREE FROM HH:MM ]).
- Header: Displays the conference room name (e.g., A-101 – COMPANY NAME).
- Event List: Shows a scrollable list of reservations for the selected day.
- Navigation: Allows browsing events for the previous, current, and next days via touch buttons (PREV, TODAY, NEXT).
- Stability: Implements mechanisms for maintaining Wi-Fi connectivity and Network Time Protocol (NTP) synchronization for robust operation.

   
# Requirements and Setup
## Hardware Requirements
Component Specification:
- Board: Freenove ESP32-32E (or compatible ESP32 board)
- Display: 4-inch, 320x480, ST7796
- Driver: TouchXPT2046 touch controller

## Required Libraries (Arduino IDE)
Install the following libraries using the Arduino Library Manager:
- TFT_eSPI (Author: Bodmer)
- WiFi (Standard ESP32 library)
- HTTPClient (Standard ESP32 library)


## TFT_eSPI Configuration (CRITICAL STEP)
The TFT_eSPI library must be configured to match the specific Freenove hardware pins. Since this library is installed locally, you must provide instructions for the user to edit their library files.
1. Locate: Find the User_Setup.h file (or the file currently selected in User_Setup_Select.h) within your local TFT_eSPI library folder (e.g., Documents/Arduino/libraries/TFT_eSPI/).
2. Verify/Edit Settings: Ensure the following required lines are uncommented or correctly defined to match the ST7796 display and the Freenove ESP32-32E pinout:
   Setting        Required Value / Definition
   Driver          #define ST7796_DRIVER
   Dimensions      #define TFT_WIDTH 320, #define TFT_HEIGHT 480
   Color Order     #define TFT_RGB_ORDER TFT_BGR
   SPI Pins        e.g., #define TFT_MISO 12, #define TFT_MOSI 13, etc.
   Touch Pins      e.g., #define TOUCH_CS 33, #define TOUCH_IRQ 36

##Project Configuration (Code)
Open the main .ino file and modify the CONFIG section before compiling:
   Constant            DescriptionExample Value   
   WIFI_SSID1            Primary Wi-Fi Network Name"Your_Network_1"
   WIFI_PASS1            Primary Wi-Fi Password"Your_Password_1"
   ICS_URL               Public URL for Google Calendar ICS data."https://calendar.google.com/calendar/ical/..."
   ROOM_NAME             Name displayed in the header."A-101 – Your Company Name"

## Getting Started
Ensure you have completed the configuration steps for TFT_eSPI (Section 3) and updated the CONFIG variables (Section 4).
1. In the Arduino IDE, select the correct board (e.g., ESP32 Dev Module).
2. Compile and upload the sketch to the ESP32 board.
3. The Serial Monitor (set to 115200 baud) will display the connection, time sync, and data download progress.
   

# Additonal resources
You can refer to this sources for more details:
- tutorial: https://docs.freenove.com/projects/fnk0103/en/latest/fnk0103/codes/tutorial.html
- 3d printing enclousure: https://www.thingiverse.com/thing:7166648 
