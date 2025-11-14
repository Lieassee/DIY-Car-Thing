#include "spotifyDisplay.h"

#include "touchScreen.h"

#include <TFT_eSPI.h>
#include <SPIFFS.h>
#include <cmath>

// Include Free Fonts header to access FreeSans bitmap fonts
// FreeSans fonts are rendered smoothly with anti-aliasing via SMOOTH_FONT flag
// Available fonts: FreeSans and FreeSansBold in 9pt, 12pt, 18pt, 24pt sizes
#include "Free_Fonts.h"
// A library for checking if the reset button has been pressed twice
// Can be used to enable config mode
// Can be installed from the library manager (Search for "ESP_DoubleResetDetector")
// https://github.com/khoih-prog/ESP_DoubleResetDetector

#include <JPEGDEC.h>
// Library for decoding Jpegs from the API responses
//
// Can be installed from the library manager (Search for "JPEGDEC")
// https://github.com/bitbank2/JPEGDEC

// -------------------------------
// Putting this stuff outside the class because
// I can't easily pass member functions in as callbacks for jpegdec

// -------------------------------

TFT_eSPI tft = TFT_eSPI();
JPEGDEC jpeg;

const char *ALBUM_ART = "/album.jpg";

// Image positioning constants for rounded corners
static int imageMarginLeft = 20;
static int imageMarginTop = 20;
static int cornerRadius = 22; // Radius for rounded corners
static int currentImageWidth = 150;
static int currentImageHeight = 150;

// Saturation boost amount (1.00 = no change, higher = more saturation)
// Adjust between 1.00-1.02 for best results
static float saturationBoost = 1.0108; // Please dont change this i took a lot of time to find the best thing 

// Function to boost saturation of an RGB565 pixel
uint16_t boostSaturation(uint16_t pixel, float boost)
{
  // Extract RGB components from RGB565
  uint8_t r5 = (pixel >> 11) & 0x1F;  // 5 bits
  uint8_t g6 = (pixel >> 5) & 0x3F;   // 6 bits
  uint8_t b5 = pixel & 0x1F;          // 5 bits
  
  // Expand to 8-bit for processing (proper conversion)
  float r = (r5 * 255.0) / 31.0;
  float g = (g6 * 255.0) / 63.0;
  float b = (b5 * 255.0) / 31.0;
  
  // Calculate luminance (weighted average, more accurate than simple average)
  float gray = 0.299 * r + 0.587 * g + 0.114 * b;
  
  // Push each component away from gray to increase saturation
  r = gray + (r - gray) * boost;
  g = gray + (g - gray) * boost;
  b = gray + (b - gray) * boost;
  
  // Clamp to valid range
  r = constrain(r, 0.0, 255.0);
  g = constrain(g, 0.0, 255.0);
  b = constrain(b, 0.0, 255.0);
  
  // Convert back to RGB565 (proper rounding)
  r5 = (uint8_t)((r * 31.0 / 255.0) + 0.5);
  g6 = (uint8_t)((g * 63.0 / 255.0) + 0.5);
  b5 = (uint8_t)((b * 31.0 / 255.0) + 0.5);
  
  return (r5 << 11) | (g6 << 5) | b5;
}

// This next function will be called during decoding of the jpeg file to
// render each block to the TFT display.
int JPEGDraw(JPEGDRAW *pDraw)
{
  // Stop further decoding as image is running off bottom of screen
  if (pDraw->y >= tft.height())
    return 0;

  // Apply saturation boost to pixels if enabled
  if (saturationBoost > 1.0)
  {
    int totalPixels = pDraw->iWidth * pDraw->iHeight;
    for (int i = 0; i < totalPixels; i++)
    {
      pDraw->pPixels[i] = boostSaturation(pDraw->pPixels[i], saturationBoost);
    }
  }
  
  // Draw the image with enhanced saturation
  tft.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
  return 1;
}

fs::File myfile;

void *myOpen(const char *filename, int32_t *size)
{
  myfile = SPIFFS.open(filename);
  *size = myfile.size();
  return &myfile;
}
void myClose(void *handle)
{
  if (myfile)
    myfile.close();
}
int32_t myRead(JPEGFILE *handle, uint8_t *buffer, int32_t length)
{
  if (!myfile)
    return 0;
  return myfile.read(buffer, length);
}
int32_t mySeek(JPEGFILE *handle, int32_t position)
{
  if (!myfile)
    return 0;
  return myfile.seek(position);
}

class CheapYellowDisplay : public SpotifyDisplay
{
public:
  // Backlight control constants
  static const int BACKLIGHT_PWM_CHANNEL = 0;
  static const int BACKLIGHT_DEFAULT_BRIGHTNESS = 153; // 60% brightness
  int currentBrightness = BACKLIGHT_DEFAULT_BRIGHTNESS;
  
  // Progress bar reset flag
  bool progressBarNeedsReset = false;

  void displaySetup(SpotifyArduino *spotifyObj)
  {

    spotify_display = spotifyObj;

    touchSetup(spotifyObj);

    Serial.println("cyd display setup");
    setWidth(320);
    setHeight(240);

    setImageHeight(150);
    setImageWidth(150);

    // Start the tft display and set it to black
    tft.init();
    tft.setRotation(3);
    tft.fillScreen(TFT_BLACK);

    // Configure backlight brightness using PWM (60% brightness)
    // ESP32 uses LEDC (LED Control) for PWM
    // Set this after tft.init() to override the library's default backlight control
    ledcSetup(BACKLIGHT_PWM_CHANNEL, 5000, 8);  // Channel 0, 5kHz frequency, 8-bit resolution (0-255)
    ledcAttachPin(TFT_BL, BACKLIGHT_PWM_CHANNEL);  // Attach TFT_BL pin to channel 0
    ledcWrite(BACKLIGHT_PWM_CHANNEL, BACKLIGHT_DEFAULT_BRIGHTNESS);  // Set brightness to 60% (153 out of 255)
    currentBrightness = BACKLIGHT_DEFAULT_BRIGHTNESS;
    
    // Load custom smooth fonts from SPIFFS
    loadCustomFonts();
  }

  // Smoothly fade backlight from one brightness level to another
  void fadeBacklight(int fromBrightness, int toBrightness, int durationMs)
  {
    int steps = 50; // Number of fade steps for smooth animation
    int delayPerStep = durationMs / steps;
    
    // Ensure minimum delay of 1ms per step
    if (delayPerStep < 1)
    {
      delayPerStep = 1;
      steps = durationMs;
    }
    
    for (int i = 0; i <= steps; i++)
    {
      int brightness = fromBrightness + ((toBrightness - fromBrightness) * i) / steps;
      ledcWrite(BACKLIGHT_PWM_CHANNEL, brightness);
      currentBrightness = brightness;
      delay(delayPerStep);
    }
  }

  // Fade backlight out (to dim or off)
  void fadeBacklightOut(int durationMs = 300, int targetBrightness = 0)
  {
    fadeBacklight(currentBrightness, targetBrightness, durationMs);
  }

  // Fade backlight in (to default or specified brightness)
  void fadeBacklightIn(int durationMs = 300, int targetBrightness = BACKLIGHT_DEFAULT_BRIGHTNESS)
  {
    fadeBacklight(currentBrightness, targetBrightness, durationMs);
  }

  // Set backlight to specific brightness instantly
  void setBacklightBrightness(int brightness)
  {
    if (brightness < 0) brightness = 0;
    if (brightness > 255) brightness = 255;
    ledcWrite(BACKLIGHT_PWM_CHANNEL, brightness);
    currentBrightness = brightness;
  }

  void resetProgressBar()
  {
    // Reset static variables in displayTrackProgress
    // This will be called via a flag check in displayTrackProgress
    progressBarNeedsReset = true;
  }
  
  void showDefaultScreen()
  {
    tft.fillScreen(TFT_BLACK);
    resetProgressBar();
  }

  // Helper function to format milliseconds to MM:SS format
  String formatTime(long milliseconds)
  {
    long totalSeconds = milliseconds / 1000;
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;
    
    String result = "";
    if (minutes < 10) result += "0";
    result += String(minutes);
    result += ":";
    if (seconds < 10) result += "0";
    result += String(seconds);
    
    return result;
  }

  // Create a smooth gradient color between two colors
  uint16_t interpolateColor(uint16_t color1, uint16_t color2, float ratio)
  {
    // Extract RGB components from 565 format
    uint8_t r1 = (color1 >> 11) & 0x1F;
    uint8_t g1 = (color1 >> 5) & 0x3F;
    uint8_t b1 = color1 & 0x1F;
    
    uint8_t r2 = (color2 >> 11) & 0x1F;
    uint8_t g2 = (color2 >> 5) & 0x3F;
    uint8_t b2 = color2 & 0x1F;
    
    // Interpolate
    uint8_t r = r1 + (r2 - r1) * ratio;
    uint8_t g = g1 + (g2 - g1) * ratio;
    uint8_t b = b1 + (b2 - b1) * ratio;
    
    // Recombine into 565 format
    return (r << 11) | (g << 5) | b;
  }

  void displayTrackProgress(long progress, long duration)
  {
    float percentage = ((float)progress / (float)duration) * 100;
    int clampedPercentage = (int)percentage;
    
    // Enhanced progress bar dimensions and positioning
    int barHeight = 8; // Thicker bar for better visibility
    int barPadding = 25;
    int barMarginFromBottom = 45; // More space for time labels
    int progressStartY = screenHeight - barHeight - barMarginFromBottom;
    int barWidth = screenWidth - (barPadding * 2);
    int barRadius = 4; // Smooth rounded corners (scaled with height)
    
    // Calculate the actual drawable area (accounting for border)
    int maxDrawableWidth = barWidth - 2;
    
    // Map percentage to the drawable area so 100% reaches the edge
    int filledWidth = map(clampedPercentage, 0, 100, 0, maxDrawableWidth);
    
    // Ensure minimum width for visibility when progress > 0
    if (filledWidth > 0 && filledWidth < barRadius * 2)
    {
      filledWidth = barRadius * 2;
    }

    // Define modern gradient colors (white gradient)
    uint16_t gradientStart = tft.color565(255, 255, 255); // Pure white
    uint16_t gradientEnd = tft.color565(220, 220, 220);   // Light gray
    uint16_t trackBgColor = tft.color565(40, 40, 40);     // Dark gray for unfilled portion
    uint16_t trackBorderColor = tft.color565(60, 60, 60); // Subtle border
    
    // Track last filled width to avoid redrawing the entire bar
    static int lastFilledWidth = -1;
    static bool barInitialized = false;
    
    // Reset bar if needed (e.g., new song or screen clear)
    if (progressBarNeedsReset)
    {
      barInitialized = false;
      lastFilledWidth = -1;
      progressBarNeedsReset = false;
    }
    
    // Only draw the bar structure once or when explicitly needed
    if (!barInitialized)
    {
      // Draw subtle border/shadow effect
      tft.fillRoundRect(barPadding, progressStartY, barWidth, barHeight, barRadius, trackBorderColor);
      
      // Draw background track (unfilled portion)
      tft.fillRoundRect(barPadding + 1, progressStartY + 1, maxDrawableWidth, barHeight - 2, barRadius - 1, trackBgColor);
      
      barInitialized = true;
      lastFilledWidth = -1; // Force redraw of filled portion
    }
    
    // Only update filled portion if width has changed significantly (reduce micro-updates)
    if (abs(filledWidth - lastFilledWidth) >= 1)
    {
      // If the bar got smaller, clear the old filled area first
      if (filledWidth < lastFilledWidth)
      {
        int clearWidth = lastFilledWidth - filledWidth;
        int clearStartX = barPadding + 1 + filledWidth;
        tft.fillRect(clearStartX, progressStartY + 1, clearWidth, barHeight - 2, trackBgColor);
      }
      
      // Draw filled portion with gradient effect
      if (filledWidth > 0)
      {
        // Draw gradient-filled progress bar
        // Create vertical gradient effect
        for (int y = 0; y < barHeight - 2; y++)
        {
          float gradientRatio = (float)y / (barHeight - 2);
          uint16_t lineColor = interpolateColor(gradientStart, gradientEnd, gradientRatio);
          
          // Draw horizontal line with proper rounded corner masking
          int lineY = progressStartY + 1 + y;
          
          // Calculate the width for this line considering rounded corners
          int lineStartX = barPadding + 1;
          int lineWidth = filledWidth;
          
          // Simple rounded corner approximation
          if (y < barRadius - 1)
          {
            int cornerOffset = barRadius - 1 - y;
            lineStartX += cornerOffset;
            lineWidth -= cornerOffset * 2;
            if (lineWidth < 0) lineWidth = 0;
          }
          else if (y >= barHeight - 2 - (barRadius - 1))
          {
            int cornerOffset = y - (barHeight - 2 - barRadius + 1);
            lineStartX += cornerOffset;
            lineWidth -= cornerOffset * 2;
            if (lineWidth < 0) lineWidth = 0;
          }
          
          if (lineWidth > 0)
          {
            tft.drawFastHLine(lineStartX, lineY, lineWidth, lineColor);
          }
        }
        
        // Add subtle highlight on top edge for glossy effect
        uint16_t highlightColor = tft.color565(255, 255, 255); // Bright white highlight
        if (filledWidth > barRadius * 2)
        {
          tft.drawFastHLine(barPadding + barRadius, progressStartY + 1, filledWidth - barRadius * 2, highlightColor);
        }
      }
      
      lastFilledWidth = filledWidth;
    }
    
    // Display time labels below progress bar - only update when time changes
    static String lastCurrentTime = "";
    static String lastTotalTime = "";
    
    String currentTime = formatTime(progress);
    String totalTime = formatTime(duration);
    
    // Only redraw time if it has changed (reduces flashing)
    if (currentTime != lastCurrentTime || totalTime != lastTotalTime)
    {
      setFont(1); // Small font for time
      int fontHeight = tft.fontHeight();
      
      // Ensure enough spacing so clear area doesn't touch progress bar
      int minGap = 4; // Minimum gap between progress bar and time labels
      int timeY = progressStartY + barHeight + fontHeight + minGap; // Position below the bar
      
      // Calculate proper clear area based on actual font metrics
      // Clear area starts above the text baseline
      int clearStartY = timeY - fontHeight + 2; // Start above baseline
      int clearHeight = fontHeight + 4; // Ensure full height coverage
      
      // Verify clear area doesn't overlap with progress bar
      if (clearStartY < progressStartY + barHeight)
      {
        clearStartY = progressStartY + barHeight; // Adjust to not overlap
      }
      
      // Left side - current time area (clear wider area to handle all digits)
      int currentTimeWidth = tft.textWidth("88:88") + 8; // Use widest digits for clearing
      tft.fillRect(barPadding, clearStartY, currentTimeWidth, clearHeight, TFT_BLACK);
      
      // Right side - total time area
      int totalTimeWidth = tft.textWidth("88:88") + 8;
      tft.fillRect(screenWidth - barPadding - totalTimeWidth, clearStartY, totalTimeWidth, clearHeight, TFT_BLACK);
      
      tft.setTextColor(tft.color565(180, 180, 180), TFT_BLACK); // Light gray text
      
      // Display current time (left aligned)
      tft.setCursor(barPadding, timeY);
      tft.print(currentTime);
      
      // Display total duration (right aligned)
      int actualTotalWidth = tft.textWidth(totalTime.c_str());
      tft.setCursor(screenWidth - barPadding - actualTotalWidth, timeY);
      tft.print(totalTime);
      
      // Update last displayed values
      lastCurrentTime = currentTime;
      lastTotalTime = totalTime;
    }
  }

  // ============================================================================================
  // FONT CONFIGURATION
  // ============================================================================================
  // Using FreeSans bitmap fonts with smooth anti-aliased rendering
  // SMOOTH_FONT flag in platformio.ini enables anti-aliasing for smoother appearance
  // FreeSans fonts are clean sans-serif fonts without sharp edges
  // Available sizes: 9pt, 12pt, 18pt, 24pt (regular and bold)
  // 
  // ENABLE_CHAR_REPLACEMENT: Fallback for special characters not in ASCII
  // ============================================================================================
  const bool ENABLE_CHAR_REPLACEMENT = true; // Fallback replacement for missing glyphs
  
  // Helper function to replace unsupported Unicode characters with ASCII equivalents
  String replaceUnsupportedChars(const char* text)
  {
    if (!ENABLE_CHAR_REPLACEMENT) return String(text);
    
    String result = String(text);
    
    // Polish characters
    result.replace("ł", "l"); // U+0142
    result.replace("Ł", "L"); // U+0141
    result.replace("ą", "a"); // U+0105
    result.replace("Ą", "A"); // U+0104
    result.replace("ć", "c"); // U+0107
    result.replace("Ć", "C"); // U+0106
    result.replace("ę", "e"); // U+0119
    result.replace("Ę", "E"); // U+0118
    result.replace("ń", "n"); // U+0144
    result.replace("Ń", "N"); // U+0143
    result.replace("ó", "o"); // U+00F3 (actually in Latin-1, should work)
    result.replace("Ó", "O"); // U+00D3
    result.replace("ś", "s"); // U+015B
    result.replace("Ś", "S"); // U+015A
    result.replace("ź", "z"); // U+017A
    result.replace("Ź", "Z"); // U+0179
    result.replace("ż", "z"); // U+017C
    result.replace("Ż", "Z"); // U+017B
    
    // Czech characters
    result.replace("ř", "r");
    result.replace("Ř", "R");
    result.replace("č", "c");
    result.replace("Č", "C");
    result.replace("š", "s");
    result.replace("Š", "S");
    result.replace("ž", "z");
    result.replace("Ž", "Z");
    result.replace("ů", "u");
    result.replace("Ů", "U");
    result.replace("ě", "e");
    result.replace("Ě", "E");
    
    // Other common characters
    result.replace("ğ", "g"); // Turkish
    result.replace("Ğ", "G");
    result.replace("ş", "s");
    result.replace("Ş", "S");
    result.replace("İ", "I");
    result.replace("ı", "i");
    
    return result;
  }

  // Initialize font system (now using built-in FreeSans fonts)
  void loadCustomFonts()
  {
    Serial.println("Using FreeSans smooth bitmap fonts with anti-aliasing");
    // No need to load fonts from SPIFFS - FreeSans fonts are built into TFT_eSPI library
  }

  // Helper function to set FreeSans fonts with smooth rendering
  void setFont(int font)
  {
    // Map font sizes to FreeSans fonts
    // TFT_eSPI's SMOOTH_FONT flag provides anti-aliasing for smoother appearance
    switch(font)
    {
      case 1: // Regular text (artist/album)
        tft.setFreeFont(FS9); // FreeSans 9pt regular - clean and readable
        break;
      case 2: // Bold text (song title)
        tft.setFreeFont(FSB12); // FreeSansBold 12pt - larger and bold
        break;
      case 4: // Config messages
        tft.setFreeFont(FSB9); // FreeSansBold 9pt
        break;
      case 6: // Large text (if needed)
        tft.setFreeFont(FSB18); // FreeSansBold 18pt - smooth and bold
        break;
      default:
        tft.setFreeFont(FSB9); // Default to 9pt bold
        break;
    }
  }

  // Helper function to count lines needed for text (UTF-8 aware)
  int countTextLines(const char* text, int maxWidth, int font)
  {
    setFont(font);
    String textStr = String(text);
    int textLen = strlen(text); // Use strlen for byte count, not character count
    
    // If text fits in one line, return 1
    if (tft.textWidth(text) <= maxWidth)
    {
      return 1;
    }
    
    // Count lines by checking word boundaries
    int lines = 1;
    int currentLineWidth = 0;
    int wordStart = 0;
    int spaceWidth = tft.textWidth(" ");
    
    for (int i = 0; i <= textLen; i++)
    {
      if (i == textLen || text[i] == ' ' || text[i] == '\0')
      {
        if (i > wordStart)
        {
          String word = textStr.substring(wordStart, i);
          int wordWidth = tft.textWidth(word.c_str());
          int totalWidth = currentLineWidth;
          
          if (currentLineWidth > 0)
          {
            totalWidth += spaceWidth + wordWidth;
          }
          else
          {
            totalWidth = wordWidth;
          }
          
          if (totalWidth > maxWidth && currentLineWidth > 0)
          {
            lines++;
            currentLineWidth = wordWidth;
          }
          else
          {
            currentLineWidth = totalWidth;
          }
        }
        wordStart = i + 1;
      }
    }
    
    return lines;
  }

  // Helper function to truncate text to fit exactly 1 line with "..." if needed
  String truncateToOneLine(const char* text, int maxWidth, int font)
  {
    setFont(font);
    String textStr = String(text);
    
    // If text fits in one line, return as is
    if (tft.textWidth(text) <= maxWidth)
    {
      return textStr;
    }
    
    // Need to truncate and add "..."
    String ellipsis = "...";
    int ellipsisWidth = tft.textWidth(ellipsis.c_str());
    int textLen = textStr.length();
    
    // Binary search for the right truncation point
    int left = 0;
    int right = textLen;
    String bestResult = ellipsis;
    
    while (left <= right)
    {
      int mid = (left + right) / 2;
      String testStr = textStr.substring(0, mid) + ellipsis;
      int testWidth = tft.textWidth(testStr.c_str());
      
      if (testWidth <= maxWidth)
      {
        bestResult = testStr;
        left = mid + 1; // Try to include more characters
      }
      else
      {
        right = mid - 1; // Need fewer characters
      }
    }
    
    // Ensure we don't break words - find the last space before the truncation point
    int truncatePos = bestResult.length() - ellipsis.length();
    if (truncatePos > 0)
    {
      int lastSpace = textStr.lastIndexOf(' ', truncatePos);
      if (lastSpace > truncatePos * 0.7) // Only use space if it's not too far back
      {
        bestResult = textStr.substring(0, lastSpace) + ellipsis;
      }
    }
    
    return bestResult;
  }

  // Helper function to truncate text to fit exactly 2 lines with "..." if needed (UTF-8 aware)
  String truncateToTwoLines(const char* text, int maxWidth, int font)
  {
    setFont(font);
    String textStr = String(text);
    String ellipsis = "...";
    
    // If text fits in 1 line, return as is
    if (tft.textWidth(text) <= maxWidth)
    {
      return textStr;
    }
    
    // Text needs multiple lines - use conservative approach
    // Reduce effective width by 10% to prevent overflow at boundaries
    int safeMaxWidth = maxWidth * 0.90;
    int textLen = textStr.length();
    
    // Binary search for the right truncation point
    int left = 0;
    int right = textLen;
    String bestResult = ellipsis;
    
    while (left <= right)
    {
      int mid = (left + right) / 2;
      String testStr = textStr.substring(0, mid) + ellipsis;
      int testLines = countTextLines(testStr.c_str(), safeMaxWidth, font);
      
      if (testLines <= 2)
      {
        bestResult = testStr;
        left = mid + 1; // Try to include more characters
      }
      else
      {
        right = mid - 1; // Need fewer characters
      }
    }
    
    // Ensure we don't break words - find the last space before the truncation point
    int truncatePos = bestResult.length() - ellipsis.length();
    if (truncatePos > 0)
    {
      int lastSpace = textStr.lastIndexOf(' ', truncatePos);
      if (lastSpace > truncatePos * 0.7) // Only use space if it's not too far back
      {
        bestResult = textStr.substring(0, lastSpace) + ellipsis;
      }
    }
    
    return bestResult;
  }

  // Helper function to print text with manual wrapping that respects vertical bounds
  void printTextWithBounds(int x, int y, const char* text, int maxWidth, int maxY, int font)
  {
    setFont(font);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextWrap(false); // Disable automatic wrapping - we handle it manually
    
    String textStr = String(text);
    int fontHeight = tft.fontHeight();
    int currentY = y;
    int currentX = x;
    
    // FreeSans fonts handle positioning correctly with their baseline
    
    // Split text into words and print line by line
    int wordStart = 0;
    int textLen = textStr.length();
    String currentLine = "";
    
    for (int i = 0; i <= textLen; i++)
    {
      if (i == textLen || textStr[i] == ' ' || textStr[i] == '\0')
      {
        if (i > wordStart)
        {
          String word = textStr.substring(wordStart, i);
          String testLine = currentLine;
          if (testLine.length() > 0)
          {
            testLine += " ";
          }
          testLine += word;
          
          int testWidth = tft.textWidth(testLine.c_str());
          
          // Check if line fits
          if (testWidth > maxWidth && currentLine.length() > 0)
          {
            // Print current line and move to next
            tft.setCursor(currentX, currentY);
            tft.print(currentLine);
            
            currentY += fontHeight;
            
            // Check if we've exceeded vertical bounds
            if (currentY + fontHeight > maxY)
            {
              return; // Stop printing
            }
            
            currentLine = word; // Start new line with current word
          }
          else
          {
            currentLine = testLine;
          }
        }
        wordStart = i + 1;
      }
    }
    
    // Print the last line if there's anything left
    if (currentLine.length() > 0 && currentY + fontHeight <= maxY)
    {
      tft.setCursor(currentX, currentY);
      tft.print(currentLine);
    }
  }

  void printCurrentlyPlayingToScreen(CurrentlyPlaying currentlyPlaying)
  {
    // Image positioning constants - moved right and down
    int imageMarginLeft = 20; // Increased from 10 to move right
    int imageMarginTop = 20;  // Increased from 10 to move down
    int textMarginLeft = 10;  // Margin between image and text
    
    // Calculate text position - to the right of the image, aligned with middle of image
    int textStartX = imageMarginLeft + imageWidth + textMarginLeft;
    int imageMiddleY = imageMarginTop + (imageHeight / 2);
    int textStartY = imageMiddleY - 25; // Start text slightly above middle for better vertical centering
    
    // Calculate available width for text (screen width minus text start X and right margin)
    int textWidth = screenWidth - textStartX - 10; // 10px margin on right
    
    // Limit text area to not go under the image (max height = image height)
    int maxTextHeight = imageHeight;
    int textAreaEndY = imageMarginTop + maxTextHeight;
    
    // Clear the text area (from right of image to end of screen, but limited to image height)
    // Also clear a small strip to the right of image to remove any white artifacts
    tft.fillRect(imageMarginLeft + imageWidth, imageMarginTop, textMarginLeft, maxTextHeight, TFT_BLACK);
    tft.fillRect(textStartX, imageMarginTop, screenWidth - textStartX, maxTextHeight, TFT_BLACK);

    // Set text color to white for all text
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    
    // Draw title (song name) - use bold font, limited to 2 lines max with truncation
    setFont(2);  // Bold 9pt for track title
    int titleFontHeight = tft.fontHeight();
    int titleStartY = textStartY;
    
    // Replace unsupported characters and truncate title to 2 lines max with "..." if needed
    String trackName = replaceUnsupportedChars(currentlyPlaying.trackName);
    String titleText = truncateToTwoLines(trackName.c_str(), textWidth, 2);
    
    // Calculate actual number of lines used by title
    int actualTitleLines = countTextLines(titleText.c_str(), textWidth, 2);
    if (actualTitleLines > 2) actualTitleLines = 2; // Cap at 2
    
    // Print title with bounds checking (max 2 lines)
    int titleMaxY = textAreaEndY;
    printTextWithBounds(textStartX, titleStartY, titleText.c_str(), textWidth, titleMaxY, 2);
    
    // Calculate Y position for artist (title height + spacing) - use actual lines, not always 2
    int titleSpacing = 8; // Reduced spacing
    int currentY = titleStartY + (actualTitleLines * titleFontHeight) + titleSpacing;
    
    // Draw artist - use regular (non-bold) font
    setFont(1);  // Regular 9pt for artist/album
    int artistFontHeight = tft.fontHeight();
    
    // Check if we have room for artist (at least one line)
    if (currentY + artistFontHeight < textAreaEndY)
    {
      int artistStartY = currentY;
      
      // Build artist string with all artists (separated by ", " or " & " for last one)
      String allArtists = "";
      for (int i = 0; i < currentlyPlaying.numArtists; i++)
      {
        if (i > 0)
        {
          if (i == currentlyPlaying.numArtists - 1)
          {
            allArtists += " & ";
          }
          else
          {
            allArtists += ", ";
          }
        }
        // Replace unsupported characters in artist name
        allArtists += replaceUnsupportedChars(currentlyPlaying.artists[i].artistName);
      }
      
      // Truncate artist to 2 lines max with "..." if needed
      String artistText = truncateToTwoLines(allArtists.c_str(), textWidth, 1);
      
      // Calculate actual number of lines used by artist
      int actualArtistLines = countTextLines(artistText.c_str(), textWidth, 1);
      if (actualArtistLines > 2) actualArtistLines = 2; // Cap at 2
      
      // Print artist with bounds checking (max 2 lines, don't go below image)
      int artistMaxY = textAreaEndY; // Allow artist to use remaining height
      printTextWithBounds(textStartX, artistStartY, artistText.c_str(), textWidth, artistMaxY, 1);
      
      // Calculate Y position for album (artist height + spacing) - use actual lines, not always 2
      int artistSpacing = 8; // Reduced spacing
      currentY = artistStartY + (actualArtistLines * artistFontHeight) + artistSpacing;
      
      // Check if we have room for album (at least one line)
      if (currentY + artistFontHeight < textAreaEndY)
      {
        // Draw album - replace unsupported characters and truncate to 2 lines
        String albumName = replaceUnsupportedChars(currentlyPlaying.albumName);
        String albumText = truncateToTwoLines(albumName.c_str(), textWidth, 1);
        printTextWithBounds(textStartX, currentY, albumText.c_str(), textWidth, textAreaEndY, 1);
      }
    }
  }

  void displayFavoriteIndicator()
  {
    // Empty stub for interface compatibility
  }

  void checkForInput()
  {
    // Not used - touch input is handled via touchScreen.h if needed
  }

  // Image Related
  void clearImage()
  {
    int imageMarginLeft = 20; // Margin from left edge - moved right
    int imageMarginTop = 20;  // Margin from top edge - moved down
    int textMarginLeft = 10;  // Margin between image and text
    // Clear image area and also clear the gap between image and text to prevent white bar
    tft.fillRect(imageMarginLeft, imageMarginTop, imageWidth + textMarginLeft, imageHeight, TFT_BLACK);
  }

  boolean processImageInfo(CurrentlyPlaying currentlyPlaying)
  {
    SpotifyImage currentlyPlayingMedImage = currentlyPlaying.albumImages[currentlyPlaying.numImages - 2];
    if (!albumDisplayed || !isSameAlbum(currentlyPlayingMedImage.url))
    {
      // We have a different album than we currently have displayed
      albumDisplayed = false;
      setImageHeight(currentlyPlayingMedImage.height / 2); // medium image is 300, we are going to scale it to half
      setImageWidth(currentlyPlayingMedImage.width / 2);
      // Update global image dimensions for rounded corner calculation
      currentImageWidth = imageWidth;
      currentImageHeight = imageHeight;
      setAlbumArtUrl(currentlyPlayingMedImage.url);
      return true;
    }

    return false;
  }

  int displayImage()
  {
    int imageStatus = displayImageUsingFile(_albumArtUrl);
    Serial.print("imageStatus: ");
    Serial.println(imageStatus);
    if (imageStatus == 1)
    {
      albumDisplayed = true;
      return imageStatus;
    }

    return imageStatus;
  }

  void drawWifiManagerMessage(WiFiManager *myWiFiManager)
  {
    Serial.println("Entered Conf Mode");
    tft.fillScreen(TFT_BLACK);
    
    // Use FreeSans smooth fonts
    setFont(4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(5, 20);
    tft.println("Entered Conf Mode:");
    tft.setCursor(5, 45);
    tft.println("Connect to the following WIFI AP:");
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    tft.setCursor(20, 70);
    tft.println(myWiFiManager->getConfigPortalSSID());
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(5, 90);
    tft.println("Password:");
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    tft.setCursor(20, 110);
    tft.println("thing123");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(5, 140);
    tft.println("If it doesn't AutoConnect, use this IP:");
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    tft.setCursor(20, 165);
    tft.println(WiFi.softAPIP().toString());
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }

  void drawRefreshTokenMessage()
  {
    Serial.println("Refresh Token Mode");
    tft.fillScreen(TFT_BLACK);
    
    // Use FreeSans smooth fonts
    setFont(4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(5, 20);
    tft.println("Refresh Token Mode:");
    tft.setCursor(5, 45);
    tft.println("You need to authorize this device to use");
    tft.setCursor(5, 68);
    tft.println("your spotify account.");
    tft.setCursor(5, 105);
    tft.println("Visit the following address and follow");
    tft.setCursor(5, 128);
    tft.println("the instructions:");
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    tft.setCursor(10, 158);
    tft.println(WiFi.localIP().toString());
    tft.setCursor(10, 180);
    tft.println("Port: 80");
  }

private:
  int displayImageUsingFile(char *albumArtUrl)
  {

    // In this example I reuse the same filename
    // over and over, maybe saving the art using
    // the album URI as the name would be better
    // as you could save having to download them each
    // time, but this seems to work fine.
    if (SPIFFS.exists(ALBUM_ART) == true)
    {
      Serial.println("Removing existing image");
      SPIFFS.remove(ALBUM_ART);
    }

    fs::File f = SPIFFS.open(ALBUM_ART, "w+");
    if (!f)
    {
      Serial.println("file open failed");
      return -1;
    }

    // Spotify uses a different cert for the Image server, so we need to swap to that for the call
    client.setCACert(spotify_image_server_cert);
    bool gotImage = spotify_display->getImage(albumArtUrl, &f);

    // Swapping back to the main spotify cert
    client.setCACert(spotify_server_cert);

    // Make sure to close the file!
    f.close();

    if (gotImage)
    {
      return drawImagefromFile(ALBUM_ART);
    }
    else
    {
      return -2;
    }
  }

  int drawImagefromFile(const char *imageFileUri)
  {
    unsigned long lTime = millis();
    lTime = millis();
    jpeg.open((const char *)imageFileUri, myOpen, myClose, myRead, mySeek, JPEGDraw);
    jpeg.setPixelType(1);
    int imageMarginLeft = 20; // Margin from left edge - moved right
    int imageMarginTop = 20;  // Margin from top edge - moved down
    // decode will return 1 on sucess and 0 on a failure
    int decodeStatus = jpeg.decode(imageMarginLeft, imageMarginTop, JPEG_SCALE_HALF);
    // jpeg.decode(45, 0, 0);
    jpeg.close();
    
    // Apply rounded corners after decoding (much faster than processing during decode)
    if (decodeStatus == 1)
    {
      applyRoundedCorners(imageMarginLeft, imageMarginTop, imageWidth, imageHeight);
      
      // Clip any pixels that extend beyond the image width by drawing black rectangles
      // on the right edge if the image rendered wider than expected
      int imageRightEdge = imageMarginLeft + imageWidth;
      int textMarginLeft = 10;
      
      // Clear the gap between image and text (this removes any white bar)
      tft.fillRect(imageRightEdge, imageMarginTop, textMarginLeft, imageHeight, TFT_BLACK);
      
      // Also clear a few pixels to the right of the image edge in case of slight overflow
      // This is a safety measure without affecting image quality
      if (imageRightEdge < screenWidth)
      {
        tft.fillRect(imageRightEdge, imageMarginTop, 5, imageHeight, TFT_BLACK);
      }
    }
    
    Serial.print("Time taken to decode and display Image (ms): ");
    Serial.println(millis() - lTime);

    return decodeStatus;
  }

  // Fast function to apply rounded corners using horizontal line drawing
  void applyRoundedCorners(int imgX, int imgY, int imgWidth, int imgHeight)
  {
    int radius = cornerRadius;
    
    // Pre-calculate the radius squared to avoid repeated multiplication
    int radiusSq = radius * radius;
    
    // Process each row in the corner regions
    for (int y = 0; y < radius; y++)
    {
      int dy = radius - y - 1; // Adjusted for proper corner alignment
      int dySq = dy * dy;
      
      // Calculate the horizontal distance using sqrt for precise corner masking
      // xOffset represents how many pixels from the corner edge should be masked
      int xOffset = radius - (int)(sqrt(radiusSq - dySq) + 0.5); // Round to nearest integer
      
      // Draw horizontal lines to mask corners (much faster than individual pixels)
      if (xOffset > 0)
      {
        // Top-left corner
        tft.drawFastHLine(imgX, imgY + y, xOffset, TFT_BLACK);
        
        // Top-right corner
        tft.drawFastHLine(imgX + imgWidth - xOffset, imgY + y, xOffset, TFT_BLACK);
        
        // Bottom-left corner
        tft.drawFastHLine(imgX, imgY + imgHeight - 1 - y, xOffset, TFT_BLACK);
        
        // Bottom-right corner
        tft.drawFastHLine(imgX + imgWidth - xOffset, imgY + imgHeight - 1 - y, xOffset, TFT_BLACK);
      }
    }
  }
};
