Features

- Real-time Display: Shows currently playing track, artist, album, and album artwork
- Rotary Encoder Control:

  - Rotate to adjust volume (0-100%) with smooth acceleration
  - Single click to play/pause
  - Double click to add/remove songs from favorites


- Smooth UI Animations: Fade transitions between tracks for a polished experience
- Enhanced Album Art: Saturation-boosted images with rounded corners
- Progress Bar: Real-time song progress with time display
- WiFi Configuration: Built-in captive portal for easy setup
- Touch Screen Support: Ready for future touch interactions

Hardware Required
Main Components

CYD - 2.8" Cheap Yellow Display with a Built-in ESP32 module
Rotary encoder - I used something similar to EC11


Rotary Encoder Setup
Connect to ESP32:

For This Step You will have to desolder the LED on the back of the board to use the connections.

- Out A → GPIO 4
- Out B → GPIO 16
- Switch (Button) → GPIO 17
- GND → GND

* Optional * 
USB-C Charging port

<img width="1084" height="563" alt="image" src="https://github.com/user-attachments/assets/48ba4e32-8ac8-4060-b4b1-839a0d4e4a10" />

Rotary Encoder Pinout
<img width="956" height="608" alt="image" src="https://github.com/user-attachments/assets/66938fee-a3ff-4e40-93a0-ee25032a78b0" />

(LED Pins Circled in Black)
<img width="1080" height="1839" alt="image" src="https://github.com/user-attachments/assets/c04b08ee-ae4e-42e8-b6ed-dcee861dcd0e" />


Spotify Setup
1. Create Spotify App

- Go to Spotify Developer Dashboard
- Create a new app
- Note your Client ID and Client Secret
- Add redirect URI: http://127.0.0.1:80/callback/

2. First Time Setup
- WiFi Configuration

- Power on the device
- Connect to WiFi AP: SpotifyDIY (Password: thing123)
- Enter your WiFi credentials and Spotify Client ID/Secret
- Device will connect and restart

3. Spotify Authorization

- After WiFi setup, visit the device's IP address (shown on display)
- Click the authorization link
- Log in to Spotify and authorize the device
- Now you will be directed to something like 127.0.0.1:80/callback/XXXXXXXXXXXXXXXXXXXX
- Chnage it to the ip and port showed on the device like this 192.168.X.XX:80/XXXXXXXXXXXXXXXXXXXX

4. You are done!
- Play a song 
- If you dont see the album image but see everything else and the screen is turning off and on - It means that the device is trying to load the image and you just have to wait/change the song
- Why is the screen black for so long when changing song? It needs to download the image.

5. USAGE
- Rotary encoder controlls

  - Action                      →   Function
  - Rotate Clockwise            →   Increase volume
  - Rotate Counter-clockwise    →   Decrease volume
  - Single Click                →   Play/Pause current track
  - Double Click                →   Add/Remove track from favorites

6. 3D Printed Case
- Everything is in 3D_Files Folder.
- I HIGHLY RECOMMEND:
  - Sanding the box and covering it in spray paint.

- MASSIVE THANKS
  - This is a heavly edited code from https://github.com/witnessmenow/Spotify-Diy-Thing
  - Original 3D printed Case Creator is https://gitlab.com/makeitforless/music-controller

