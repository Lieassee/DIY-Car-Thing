// Rotary Encoder Integration for Volume Control and Song Liking
// Uses rotary encoder to control volume and button double-click to add songs to favorites

// Set to false if DELETE requests keep failing (will only add, not remove)
#define ENABLE_UNLIKE_FEATURE false

#include <TFT_eSPI.h>

#define ENCODER_CLK 4   // IO18
#define ENCODER_DT  16  // IO19
#define ENCODER_SW  17  // IO17 - Button

// Forward declaration of tft object (defined in cheapYellowLCD.h)
extern TFT_eSPI tft;

// Encoder variables
volatile int encoderPos = 0;
volatile bool encoderChanged = false;
volatile unsigned long lastInterruptTime = 0;
volatile int lastCLKState = 0;
volatile int lastDTState = 0;
volatile int encoderState = 0;  // State machine: bits 0=CLK, 1=DT

// Button variables
volatile bool buttonPressed = false;
volatile unsigned long lastButtonTime = 0;
volatile unsigned long buttonPressStartTime = 0;
volatile bool buttonCurrentlyHeld = false;
volatile int clickCount = 0;
volatile unsigned long firstClickTime = 0;
const unsigned long DOUBLE_CLICK_TIMEOUT = 400;  // ms between clicks to register as double-click

// Volume tracking
int currentVolume = 50;  // 0-100
const int VOLUME_MIN = 0;
const int VOLUME_MAX = 100;
const float VOLUME_STEP = 0.5;  // Adjust volume by 0.5% per encoder click for ultra-smooth control
const int VOLUME_ACCELERATION_THRESHOLD = 3;  // Clicks per 100ms to trigger acceleration (lower = earlier acceleration)

// Acceleration tracking
int lastEncoderPos = 0;
unsigned long lastEncoderTime = 0;
int encoderClicksPerInterval = 0;

// Track URI for liking
char currentTrackUri[200] = "";
bool trackLiked = false;  // Track if we've liked this song during this session
bool isCurrentlyPlaying = false;

// Forward declarations
extern SpotifyArduino spotify;
extern SpotifyDisplay *spotifyDisplay;
extern WiFiClientSecure client;
extern const char* spotify_server_cert;
extern bool pauseSpotifyPolling;
extern char storedAccessToken[400];
void refreshStoredAccessToken(); // Refresh access token for manual API calls
void onVolumeChanged(int volume);
void onButtonPressed();
void updateCurrentTrackUri(const char* trackUri);
void updatePlayingState(bool isPlaying);

// Interrupt service routine for encoder button
void IRAM_ATTR buttonISR() {
  unsigned long buttonTime = millis();
  int buttonState = digitalRead(ENCODER_SW);
  
  // Debounce - ignore if less than 50ms since last state change
  if (buttonTime - lastButtonTime > 50) {
    // Button pressed (LOW = pressed with pullup)
    if (buttonState == LOW && !buttonCurrentlyHeld) {
      buttonCurrentlyHeld = true;
      buttonPressStartTime = buttonTime;
      lastButtonTime = buttonTime;
    }
    // Button released
    else if (buttonState == HIGH && buttonCurrentlyHeld) {
      buttonCurrentlyHeld = false;
      buttonPressed = true; // Signal button release for handling
      lastButtonTime = buttonTime;
      
      // Track click count for double-click detection
      if (clickCount == 0) {
        firstClickTime = buttonTime;
        clickCount = 1;
      } else if (buttonTime - firstClickTime < DOUBLE_CLICK_TIMEOUT) {
        clickCount = 2;  // Double click detected
      } else {
        // Timeout exceeded, reset to single click
        firstClickTime = buttonTime;
        clickCount = 1;
      }
    }
  }
}

// Interrupt service routine for encoder - state machine approach
void IRAM_ATTR encoderISR() {
  unsigned long interruptTime = millis();
  
  // Debounce - ignore if less than 2ms since last interrupt
  if (interruptTime - lastInterruptTime > 2) {
    int clkState = digitalRead(ENCODER_CLK);
    int dtState = digitalRead(ENCODER_DT);
    
    // Build state: bits [CLK][DT] = 2*CLK + DT (0-3)
    int newState = (clkState << 1) | dtState;
    int oldState = encoderState;
    
    // Only process if state actually changed
    if (newState != oldState) {
      int oldPos = encoderPos;
      
      // Quadrature state machine decoding
      // Valid transitions in quadrature encoder:
      // 00 -> 01 -> 11 -> 10 -> 00 (clockwise)
      // 00 -> 10 -> 11 -> 01 -> 00 (counter-clockwise)
      
      if ((oldState == 0 && newState == 1) ||  // 00->01
          (oldState == 1 && newState == 3) ||   // 01->11
          (oldState == 3 && newState == 2) ||   // 11->10
          (oldState == 2 && newState == 0)) {   // 10->00
        encoderPos++;  // Clockwise - increase volume
      } else if ((oldState == 0 && newState == 2) ||  // 00->10
                 (oldState == 2 && newState == 3) ||   // 10->11
                 (oldState == 3 && newState == 1) ||   // 11->01
                 (oldState == 1 && newState == 0)) {   // 01->00
        encoderPos--;  // Counter-clockwise - decrease volume
      }
      
      encoderState = newState;
      lastCLKState = clkState;
      lastDTState = dtState;
      encoderChanged = true;
      lastInterruptTime = interruptTime;
    }
  }
}

// Setup rotary encoder pins and interrupts
void setupRotaryEncoder() {
  Serial.println("Setting up Rotary Encoder...");
  
  // Setup encoder pins BEFORE reading them
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);  // Button pin
  delay(50); // Let pullups stabilize
  
  // Read initial pin states
  int clkState = digitalRead(ENCODER_CLK);
  int dtState = digitalRead(ENCODER_DT);
  lastCLKState = clkState;
  lastDTState = dtState;
  // Initialize encoder state: bits [CLK][DT]
  encoderState = (clkState << 1) | dtState;
  
  Serial.print("Initial CLK state: ");
  Serial.println(clkState);
  Serial.print("Initial DT state: ");
  Serial.println(dtState);
  
  // Attach interrupt to both CLK and DT pins - use CHANGE to track all transitions
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_DT), encoderISR, CHANGE);
  // Attach interrupt to button - use CHANGE to detect both press and release for long-press
  attachInterrupt(digitalPinToInterrupt(ENCODER_SW), buttonISR, CHANGE);
  
  Serial.println("Rotary Encoder interrupts attached");
}

// Handle encoder volume changes with smooth acceleration
void handleEncoderVolumeChange() {
  if (encoderChanged) {
    encoderChanged = false;
    
    // Read volatile encoderPos safely
    noInterrupts();
    int currentPos = encoderPos;
    interrupts();
    
    // Calculate clicks since last update
    int clickDelta = currentPos - lastEncoderPos;
    unsigned long currentTime = millis();
    unsigned long timeDelta = currentTime - lastEncoderTime;
    
    // Reset acceleration tracking if more than 200ms has passed
    if (timeDelta > 200) {
      encoderClicksPerInterval = 0;
      lastEncoderTime = currentTime;
    } else {
      encoderClicksPerInterval += abs(clickDelta);
    }
    
    // Calculate acceleration multiplier (faster rotation = bigger jumps)
    // Base: 1x multiplier, accelerates up to 3x at high speed
    float accelerationMultiplier = 1.0;
    if (encoderClicksPerInterval > VOLUME_ACCELERATION_THRESHOLD) {
      // Smooth acceleration: 1x to 3x based on click speed
      int clicksAboveThreshold = min(encoderClicksPerInterval - VOLUME_ACCELERATION_THRESHOLD, 10);
      accelerationMultiplier = 1.0 + (clicksAboveThreshold * 0.2);
      if (accelerationMultiplier > 3.0) {
        accelerationMultiplier = 3.0;  // Cap at 3x
      }
    }
    
    // Calculate volume change with acceleration
    float volumeChange = clickDelta * VOLUME_STEP * accelerationMultiplier;
    int newVolume = (int)(currentVolume + volumeChange);
    
    // Clamp volume to valid range
    if (newVolume < VOLUME_MIN) {
      newVolume = VOLUME_MIN;
      encoderPos = 0;  // Reset encoder position
      encoderClicksPerInterval = 0;
    } else if (newVolume > VOLUME_MAX) {
      newVolume = VOLUME_MAX;
      encoderPos = (VOLUME_MAX - currentVolume) / VOLUME_STEP;
      encoderClicksPerInterval = 0;
    }
    
    // Only update if volume actually changed
    if (newVolume != currentVolume) {
      currentVolume = newVolume;
      lastEncoderPos = currentPos;
      
      // Pause polling to avoid SSL conflicts
      pauseSpotifyPolling = true;
      
      // Set device volume via Spotify API
      // This sets the volume for the currently active device
      int volumeStatus = spotify.setVolume(currentVolume);
      
      // Resume polling
      pauseSpotifyPolling = false;
      
      if (volumeStatus == 204) {
        Serial.println("Volume set successfully");
      } else {
        Serial.print("Failed to set volume. Status: ");
        Serial.println(volumeStatus);
      }
      
      // Notify display of volume change
      onVolumeChanged(currentVolume);
    }
  }
}

// Save track to liked songs via Spotify API
int saveTrackToLiked(const char* trackId) {
  if (strlen(trackId) == 0) {
    Serial.println("No track ID available");
    return -1;
  }
  
  Serial.println("=== SAVE TRACK DEBUG ===");
  Serial.print("Track ID: ");
  Serial.println(trackId);
  
  // Pause polling to avoid SSL conflicts
  pauseSpotifyPolling = true;
  Serial.println("Paused Spotify polling for write operation");
  
  // Refresh access token before use
  Serial.println("Refreshing access token for API call...");
  refreshStoredAccessToken();
  delay(100); // Give time for token refresh to complete
  
  // Check if we have a valid token
  if (strlen(storedAccessToken) == 0) {
    Serial.println("ERROR: Access token is empty!");
    pauseSpotifyPolling = false;
    return -1;
  }
  
  Serial.print("Using access token (first 20 chars): ");
  Serial.println(String(storedAccessToken).substring(0, 20));
  
  // Ensure any existing connection is closed
  if (client.connected()) {
    client.stop();
    Serial.println("Closed existing client connection");
  }
  
  // Wait for connection to fully close
  delay(500);
  
  Serial.println("Creating fresh HTTPS connection");
  
  // Create fresh connection for this request
  WiFiClientSecure freshClient;
  freshClient.setCACert(spotify_server_cert);
  
  if (!freshClient.connect("api.spotify.com", 443)) {
    Serial.println("Connection to api.spotify.com failed");
    pauseSpotifyPolling = false;
    return -1;
  }
  
  Serial.println("Connected to api.spotify.com");
  
  // Build the PUT request manually
  String url = "/v1/me/tracks?ids=" + String(trackId);
  String authHeader = "Bearer " + String(storedAccessToken);
  
  freshClient.print(String("PUT ") + url + " HTTP/1.1\r\n" +
                   "Host: api.spotify.com\r\n" +
                   "Authorization: " + authHeader + "\r\n" +
                   "Content-Length: 0\r\n" +
                   "Connection: close\r\n\r\n");
  
  Serial.println("Request sent, waiting for response...");
  
  // Wait for response
  unsigned long timeout = millis();
  while (freshClient.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println("Request timeout");
      freshClient.stop();
      pauseSpotifyPolling = false;
      return -1;
    }
    delay(10);
  }
  
  // Read status code
  String response = freshClient.readStringUntil('\n');
  Serial.print("Response: ");
  Serial.println(response);
  
  int statusCode = -1;
  if (response.indexOf("200") > 0 || response.indexOf("204") > 0) {
    statusCode = 200;
    Serial.println(">>> Track ADDED to favorites (via direct HTTP)");
  } else {
    Serial.println(">>> FAILED to add track");
  }
  
  freshClient.stop();
  
  // Resume polling
  pauseSpotifyPolling = false;
  Serial.println("Resumed Spotify polling");
  Serial.println("=======================");
  
  return statusCode;
}

// Remove track from liked songs via Spotify API
int removeTrackFromLiked(const char* trackId) {
  if (strlen(trackId) == 0) {
    Serial.println("No track ID available");
    return -1;
  }
  
  Serial.println("=== REMOVE TRACK DEBUG ===");
  Serial.print("Track ID: ");
  Serial.println(trackId);
  
  // Pause polling to avoid SSL conflicts
  pauseSpotifyPolling = true;
  Serial.println("Paused Spotify polling for write operation");
  
  // Refresh access token before use
  Serial.println("Refreshing access token for API call...");
  refreshStoredAccessToken();
  delay(100); // Give time for token refresh to complete
  
  // Check if we have a valid token
  if (strlen(storedAccessToken) == 0) {
    Serial.println("ERROR: Access token is empty!");
    pauseSpotifyPolling = false;
    return -1;
  }
  
  Serial.print("Using access token (first 20 chars): ");
  Serial.println(String(storedAccessToken).substring(0, 20));
  
  // Ensure any existing connection is closed
  if (client.connected()) {
    client.stop();
    Serial.println("Closed existing client connection");
  }
  
  // Wait for connection to fully close
  delay(500);
  
  Serial.println("Creating fresh HTTPS connection");
  
  // Create fresh connection for this request
  WiFiClientSecure freshClient;
  freshClient.setCACert(spotify_server_cert);
  
  if (!freshClient.connect("api.spotify.com", 443)) {
    Serial.println("Connection to api.spotify.com failed");
    pauseSpotifyPolling = false;
    return -1;
  }
  
  Serial.println("Connected to api.spotify.com");
  
  // Build the DELETE request manually
  String url = "/v1/me/tracks?ids=" + String(trackId);
  String authHeader = "Bearer " + String(storedAccessToken);
  
  freshClient.print(String("DELETE ") + url + " HTTP/1.1\r\n" +
                   "Host: api.spotify.com\r\n" +
                   "Authorization: " + authHeader + "\r\n" +
                   "Content-Length: 0\r\n" +
                   "Connection: close\r\n\r\n");
  
  Serial.println("Request sent, waiting for response...");
  
  // Wait for response
  unsigned long timeout = millis();
  while (freshClient.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println("Request timeout");
      freshClient.stop();
      pauseSpotifyPolling = false;
      return -1;
    }
    delay(10);
  }
  
  // Read status code
  String response = freshClient.readStringUntil('\n');
  Serial.print("Response: ");
  Serial.println(response);
  
  int statusCode = -1;
  if (response.indexOf("200") > 0 || response.indexOf("204") > 0) {
    statusCode = 200;
    Serial.println(">>> Track REMOVED from favorites (via direct HTTP)");
  } else {
    Serial.println(">>> FAILED to remove track");
  }
  
  freshClient.stop();
  
  // Resume polling
  pauseSpotifyPolling = false;
  Serial.println("Resumed Spotify polling");
  Serial.println("=========================");
  
  return statusCode;
}

// Extract track ID from Spotify URI (spotify:track:TRACK_ID)
String extractTrackId(const char* trackUri) {
  String uri = String(trackUri);
  int lastColon = uri.lastIndexOf(':');
  if (lastColon != -1) {
    return uri.substring(lastColon + 1);
  }
  return "";
}

// Handle button press for play/pause and adding songs to favorites
void handleEncoderButtonPress() {
  static unsigned long lastActionTime = 0;
  unsigned long currentTime = millis();
  
  // First check if we have a double-click ready to process
  if (clickCount == 2) {
    // DOUBLE CLICK - Add to Favorites
    Serial.println(">>> DOUBLE CLICK - Adding to Favorites");
    clickCount = 0;
    buttonPressed = false;  // Clear the button press flag
    
    if (strlen(currentTrackUri) == 0) {
      Serial.println("No track currently playing");
      onButtonPressed();
      return;
    }
    
    // Extract track ID from URI
    String trackId = extractTrackId(currentTrackUri);
    
    if (trackId.length() == 0) {
      Serial.println("Failed to extract track ID");
      onButtonPressed();
      return;
    }
    
    int statusCode;
    
#if ENABLE_UNLIKE_FEATURE
    // Toggle like/unlike based on session tracking
    Serial.print("Current liked status: ");
    Serial.println(trackLiked ? "LIKED" : "NOT LIKED");
    
    if (trackLiked) {
      // Try to remove from favorites
      Serial.println("Attempting to UNLIKE track...");
      statusCode = removeTrackFromLiked(trackId.c_str());
      if (statusCode == 200 || statusCode == 204) {
        trackLiked = false;
        Serial.println(">>> Track REMOVED from favorites");
      } else {
        Serial.print(">>> FAILED to remove track. Status: ");
        Serial.println(statusCode);
      }
    } else {
      // Try to add to favorites
      Serial.println("Attempting to LIKE track...");
      statusCode = saveTrackToLiked(trackId.c_str());
      if (statusCode == 200 || statusCode == 204) {
        trackLiked = true;
        Serial.println(">>> Track ADDED to favorites");
      } else {
        Serial.print(">>> FAILED to add track. Status: ");
        Serial.println(statusCode);
      }
    }
#else
    // Add-only mode (no toggle, just add to favorites)
    Serial.println("Attempting to ADD track to favorites...");
    statusCode = saveTrackToLiked(trackId.c_str());
    if (statusCode == 200 || statusCode == 204) {
      Serial.println(">>> Track ADDED to favorites");
    } else {
      Serial.print(">>> FAILED to add track. Status: ");
      Serial.println(statusCode);
    }
#endif
    
    lastActionTime = currentTime;
    onButtonPressed();
    return;
  }
  
  // Clear buttonPressed flag if it was set (for single clicks waiting for timeout)
  if (buttonPressed) {
    buttonPressed = false;
  }
  
  // Handle single click after timeout
  if (clickCount == 1 && currentTime - firstClickTime > DOUBLE_CLICK_TIMEOUT) {
    if (currentTime - lastActionTime > DOUBLE_CLICK_TIMEOUT) {
      // Process single click after timeout
      Serial.println(">>> SINGLE CLICK - Toggling Play/Pause");
      clickCount = 0;
      
      // Pause polling to avoid SSL conflicts
      pauseSpotifyPolling = true;
      
      // Add delay before PUT request to avoid SSL issues
      delay(100);
      
      // Toggle play/pause based on current state
      bool success = false;
      if (isCurrentlyPlaying) {
        Serial.println("Pausing...");
        success = spotify.pause();
      } else {
        Serial.println("Playing...");
        success = spotify.play();
      }
      
      // Resume polling
      pauseSpotifyPolling = false;
      
      if (success) {
        Serial.println("Play/pause toggled successfully");
        isCurrentlyPlaying = !isCurrentlyPlaying;
      } else {
        Serial.println("Failed to toggle play/pause");
      }
      
      lastActionTime = currentTime;
      onButtonPressed();
    } else {
      // Reset if we can't process due to cooldown
      clickCount = 0;
    }
  }
}

// Update current track URI (call this from spotifyLogic when track changes)
void updateCurrentTrackUri(const char* trackUri) {
  if (trackUri != NULL) {
    strcpy(currentTrackUri, trackUri);
    trackLiked = false;  // Reset for new track (session-based tracking)
    Serial.print("Updated current track URI: ");
    Serial.println(currentTrackUri);
    Serial.println("Reset liked status for new track");
  }
}

// Update playing state (call this from spotifyLogic when state changes)
void updatePlayingState(bool isPlaying) {
  isCurrentlyPlaying = isPlaying;
}

// Callback function to notify display of volume change
// This will be called when volume changes
void onVolumeChanged(int volume) {
  Serial.print("Volume changed to: ");
  Serial.print(volume);
  Serial.println("%");
  // Display handler can implement visual feedback
}

// Callback function to notify display of button press
// This will be called when button is pressed
void onButtonPressed() {
  Serial.println("Button pressed - track action completed");
  // Display handler can implement visual feedback if needed
}

// Main rotary encoder check function - call this from your main loop
void checkRotaryEncoderInput() {
  handleEncoderVolumeChange();
  handleEncoderButtonPress();
}
