#ifndef SPOTIFYDISPLAY_H
#define SPOTIFYDISPLAY_H

class SpotifyDisplay {
  public:
    virtual void displaySetup(SpotifyArduino *spotifyObj) = 0;

    virtual void showDefaultScreen() = 0;

    // Track related
    virtual void displayTrackProgress(long progress, long duration) = 0;
    virtual void printCurrentlyPlayingToScreen(CurrentlyPlaying currentlyPlaying) = 0;
    virtual void displayFavoriteIndicator() = 0;

    //Probably Touch screen related
    virtual void checkForInput() =0;

    //Image Related
    virtual void clearImage()= 0;
    virtual boolean processImageInfo (CurrentlyPlaying currentlyPlaying)=0;
    virtual int displayImage() = 0;

    virtual void drawWifiManagerMessage(WiFiManager *myWiFiManager) = 0;
    virtual void drawRefreshTokenMessage() = 0;

    // Backlight animation methods (default implementations do nothing for displays without backlight control)
    virtual void fadeBacklightOut(int durationMs = 300, int targetBrightness = 0) {}
    virtual void fadeBacklightIn(int durationMs = 300, int targetBrightness = 153) {}
    
    // Progress bar reset method (default implementation does nothing)
    virtual void resetProgressBar() {}

    void setAlbumArtUrl(const char* albumArtUrl){
      strcpy(_albumArtUrl, albumArtUrl);
    }

    char* getAlbumArtUrl(){
      return _albumArtUrl;
    }

    bool isSameAlbum(const char* albumArtUrl){
      return strcmp(_albumArtUrl, albumArtUrl) == 0;
    }

    void setWidth(int w) {
      screenWidth = w;
      screenCenterX = screenWidth / 2;
    }

    void setHeight(int h) {
      screenHeight = h;
    }

    void setImageHeight(int h) {
      imageHeight = h;
    }

    void setImageWidth(int w) {
      imageWidth = w;
    }

  protected:
    int screenWidth;
    int screenHeight;
    int screenCenterX;
    int imageWidth;
    int imageHeight;
    SpotifyArduino *spotify_display;
    char _albumArtUrl[200];
    boolean albumDisplayed = false;
};
#endif
