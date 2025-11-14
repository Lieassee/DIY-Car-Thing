SpotifyDisplay *sp_Display;

SpotifyArduino spotify(client, NULL, NULL);

// Store access token for manual HTTP requests (like track liking)
char storedAccessToken[400] = "";

// Store client credentials and refresh token for on-demand token refresh
const char *storedClientId = NULL;
const char *storedClientSecret = NULL;
char storedRefreshToken[400] = "";

bool albumArtChanged = false;
bool textNeedsUpdate = false;
CurrentlyPlaying lastCurrentlyPlaying;

long songStartMillis;
long songDuration;

char lastTrackUri[200];
char lastTrackContextUri[200];

// You might want to make this much smaller, so it will update responsively

unsigned long delayBetweenRequests = 5000; // Time between requests (5 seconds)
unsigned long requestDueTime;              // time when request due

unsigned long delayBetweenProgressUpdates = 500; // Time between requests (0.5 seconds)
unsigned long progressDueTime;                   // time when request due

bool pauseSpotifyPolling = false;          // Pause polling during write operations to avoid SSL conflicts

void spotifySetup(SpotifyDisplay *theDisplay, const char *clientId, const char *clientSecret)
{
  sp_Display = theDisplay;
  client.setCACert(spotify_server_cert);
  spotify.lateInit(clientId, clientSecret);

  // Store credentials for later use in manual token refresh
  storedClientId = clientId;
  storedClientSecret = clientSecret;

  lastTrackUri[0] = '\0';
  lastTrackContextUri[0] = '\0';
}

bool isSameTrack(const char *trackUri)
{

  return strcmp(lastTrackUri, trackUri) == 0;
}

void setTrackUri(const char *trackUri)
{
  strcpy(lastTrackUri, trackUri);
}

void setTrackContextUri(const char *trackContext)
{
  if (trackContext == NULL)
  {
    lastTrackContextUri[0] = '\0';
  }
  else
  {
    strcpy(lastTrackContextUri, trackContext);
  }
}

// Helper function to manually extract access token from Spotify API
void extractAndStoreAccessToken(const char *clientId, const char *clientSecret, const char *refreshToken)
{
  WiFiClientSecure tokenClient;
  tokenClient.setCACert(spotify_server_cert);
  
  if (!tokenClient.connect("accounts.spotify.com", 443))
  {
    Serial.println("Failed to connect to accounts.spotify.com for token extraction");
    return;
  }
  
  // Build the POST body
  String postBody = "grant_type=refresh_token&refresh_token=" + String(refreshToken) +
                   "&client_id=" + String(clientId) +
                   "&client_secret=" + String(clientSecret);
  
  // Make the request
  tokenClient.print(String("POST /api/token HTTP/1.1\r\n") +
                   "Host: accounts.spotify.com\r\n" +
                   "Content-Type: application/x-www-form-urlencoded\r\n" +
                   "Content-Length: " + postBody.length() + "\r\n" +
                   "Connection: close\r\n\r\n" +
                   postBody);
  
  // Wait for response
  unsigned long timeout = millis();
  while (tokenClient.available() == 0)
  {
    if (millis() - timeout > 5000)
    {
      Serial.println("Token request timeout");
      tokenClient.stop();
      return;
    }
    delay(10);
  }
  
  // Skip headers
  while (tokenClient.available())
  {
    String line = tokenClient.readStringUntil('\n');
    if (line == "\r")
    {
      break; // Headers end
    }
  }
  
  // Read JSON response
  String jsonResponse = tokenClient.readString();
  tokenClient.stop();
  
  // Parse JSON to extract access_token
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, jsonResponse);
  
  if (!error && doc.containsKey("access_token"))
  {
    const char *accessToken = doc["access_token"];
    strcpy(storedAccessToken, accessToken);
    Serial.println("Successfully extracted and stored access token");
  }
  else
  {
    Serial.println("Failed to parse access token from response");
    storedAccessToken[0] = '\0';
  }
}

void spotifyRefreshToken(const char *refreshToken)
{
  spotify.setRefreshToken(refreshToken);
  
  // Store refresh token for later use
  strcpy(storedRefreshToken, refreshToken);

  // If you want to enable some extra debugging
  // uncomment the "#define SPOTIFY_DEBUG" in SpotifyArduino.h

  Serial.println("Refreshing Access Tokens");
  if (!spotify.refreshAccessToken())
  {
    Serial.println("Failed to get access tokens");
  }
  else
  {
    // Also extract and store the access token for manual HTTP requests
    Serial.println("Extracting access token for manual API calls...");
    extractAndStoreAccessToken(storedClientId, storedClientSecret, refreshToken);
  }
}

// Function to refresh stored access token on demand (for manual API calls)
void refreshStoredAccessToken()
{
  if (storedRefreshToken[0] != '\0' && storedClientId != NULL && storedClientSecret != NULL)
  {
    Serial.println("Refreshing stored access token...");
    extractAndStoreAccessToken(storedClientId, storedClientSecret, storedRefreshToken);
  }
  else
  {
    Serial.println("Cannot refresh token: credentials not initialized");
  }
}

void handleCurrentlyPlaying(CurrentlyPlaying currentlyPlaying)
{
  if (currentlyPlaying.trackUri != NULL)
  {
    if (!isSameTrack(currentlyPlaying.trackUri))
    {
      setTrackUri(currentlyPlaying.trackUri);
      setTrackContextUri(currentlyPlaying.contextUri);

      // Update rotary encoder with current track URI for liking
      updateCurrentTrackUri(currentlyPlaying.trackUri);

      // Mark that text needs update - will happen together with album art
      textNeedsUpdate = true;
    }

    // Store the current playing info for later use
    lastCurrentlyPlaying = currentlyPlaying;

    // Update playing state for rotary encoder
    updatePlayingState(currentlyPlaying.isPlaying);

    albumArtChanged = sp_Display->processImageInfo(currentlyPlaying);

    sp_Display->displayTrackProgress(currentlyPlaying.progressMs, currentlyPlaying.durationMs);

    if (currentlyPlaying.isPlaying)
    {
      // If we know at what millis the song started at, we can make a good guess
      // at updating the progress bar more often than checking the API
      songStartMillis = millis() - currentlyPlaying.progressMs;
      songDuration = currentlyPlaying.durationMs;
    }
    else
    {
      // Song doesn't seem to be playing, do not update the progress
      songStartMillis = 0;
    }
  }
}

void updateProgressBar()
{
  if (songStartMillis != 0 && millis() > progressDueTime)
  {
    long songProgress = millis() - songStartMillis;
    if (songProgress > songDuration)
    {
      songProgress = songDuration;
    }
    sp_Display->displayTrackProgress(songProgress, songDuration);
    progressDueTime = millis() + delayBetweenProgressUpdates;
  }
}

void updateCurrentlyPlaying(boolean forceUpdate)
{
  // Skip polling if paused (e.g., during write operations)
  if (pauseSpotifyPolling && !forceUpdate)
  {
    return;
  }
  
  if (forceUpdate || millis() > requestDueTime)
  {
    if (forceUpdate)
    {
      Serial.println("forcing an update");
    }
    // Serial.print("Free Heap: ");
    // Serial.println(ESP.getFreeHeap());

    Serial.println("getting currently playing song:");
    // Check if music is playing currently on the account.
    int status = spotify.getCurrentlyPlaying(handleCurrentlyPlaying, SPOTIFY_MARKET);
    if (status == 200)
    {
      Serial.println("Successfully got currently playing");
      if (albumArtChanged || forceUpdate || textNeedsUpdate)
      {
        // Smooth fade animation for synchronized text and image update
        // Fade out to completely black before updating everything
        sp_Display->fadeBacklightOut(600, 0); // Smooth fade out (600ms)
        
        // Reset progress bar for new song
        sp_Display->resetProgressBar();
        
        // Update text if needed (always do this for new songs)
        if (textNeedsUpdate || forceUpdate)
        {
          sp_Display->printCurrentlyPlayingToScreen(lastCurrentlyPlaying);
          textNeedsUpdate = false;
        }
        
        // Update image if album changed
        if (albumArtChanged || forceUpdate)
        {
          sp_Display->clearImage();
          int displayImageResult = sp_Display->displayImage();

          if (displayImageResult)
          {
            albumArtChanged = false;
          }
          else
          {
            Serial.print("failed to display image: ");
            Serial.println(displayImageResult);
          }
        }
        
        // Fade back in smoothly after both text and image are displayed
        sp_Display->fadeBacklightIn(600); // Smooth fade in (600ms)
      }
    }
    else if (status == 204)
    {
      songStartMillis = 0;
      Serial.println("Doesn't seem to be anything playing");
    }
    else
    {
      Serial.print("Error: ");
      Serial.println(status);
    }

    requestDueTime = millis() + delayBetweenRequests;
  }
}
