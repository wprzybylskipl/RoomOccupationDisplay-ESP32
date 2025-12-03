# RoomOccupationDisplay-ESP32
This project uses a Freenove ESP32 touch display 4inch to present room bookings pulled from Google calendar

# Features 
1. Present Room name
2. Present busy/free
3. List events for given day
4. Show events for next days

   
# Setup
1. Pull project fro github
2. Change in the code 
   1. Set up wifi credentials: WIFI_SSID and WIFI_PASS
   2. Google ICS URL: ICS_URL. Note the url must be public. You may also try to use secret iCAL url.
   3. Setup room name
3. Add to sketch two libraries from ZIP:
   1. TFT_eSPI
   2. TFT_eSPI_Setups
5. Using Arduino compile sketch and upload to board
6. Before uploading make sure that the files: User_Setup.h and User_Setup_Select.h have set drivers coresponing to your board.
   

# Additonal resources
You can refer to this sources for more details:
- tutorial: https://docs.freenove.com/projects/fnk0103/en/latest/fnk0103/codes/tutorial.html
- 3d printing enclousure: https://www.thingiverse.com/thing:7166648 
