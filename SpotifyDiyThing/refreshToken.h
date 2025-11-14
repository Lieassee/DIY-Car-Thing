#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

SpotifyArduino *spotify_refresh;

#define USE_IP_ADDRESS 1

#ifdef USE_IP_ADDRESS
char callbackURItemplate[] = "%s%s%s%s";
char callbackURIProtocol[] = "http%3A%2F%2F"; // "http://"
char callbackURIPort[] = "%3A80"; // ":80"
char callbackURIAddress[] = "%2Fcallback%2F"; // "/callback/"
char callbackURI[100];
#else
char callbackURI[] = "http%3A%2F%2Farduino.local%2Fcallback%2F";
#endif

bool haveRefreshToken = false;
char refreshToken[400];

char *clientIdRefresh;

WebServer server(80);

void handleRoot()
{
  char webpage[1150];
  char scope[] = "user-read-playback-state%20user-modify-playback-state%20user-library-modify";

  const char *webpageTemplate =
    R"(
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8">
    <meta http-equiv="X-UA-Compatible" content="IE=edge">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no" />
  </head>
  <body onload='decode()'>
    <div>
      Click here to auth the device: <a href="https://accounts.spotify.com/authorize?client_id=%s&response_type=code&redirect_uri=%s&scope=%s">spotify Auth</a>
    </div>
    </br>
    <div>
      Make sure to add <span style="font-weight:bold;" id="uri"> %s </span> to the "redirect URIs" in the <a href="https://developer.spotify.com/dashboard/applications">Spotify Developers Dashboard</a>
    </div>
  </body>
  <script>
    function decode() {
        document.getElementById('uri').innerText = decodeURIComponent(document.getElementById('uri').innerText);
    }
</script>
</html>
)";
  
  sprintf(webpage, webpageTemplate, clientIdRefresh, callbackURI, scope, callbackURI);
  server.send(200, "text/html", webpage);
}

// Forward declaration
extern char storedAccessToken[400];

void handleCallback()
{
  String code = "";
  const char *rt = NULL;
  for (uint8_t i = 0; i < server.args(); i++)
  {
    if (server.argName(i) == "code")
    {
      code = server.arg(i);
      rt = spotify_refresh->requestAccessTokens(code.c_str(), callbackURI);
    }
  }

  if (rt != NULL)
  {
    strcpy(refreshToken, rt);
    haveRefreshToken = true;
    
    // Note: Access token is stored internally by SpotifyArduino library
    // For manual API calls, we'll need to refresh and extract it separately
    // For now, the token will be populated after first refresh
    storedAccessToken[0] = '\0'; // Will be populated on next refresh
    
    server.send(200, "text/plain", "Got Token, your device should be ready");
  }
  else
  {
    server.send(404, "text/plain", "Failed to load token, check serial monitor");
  }
}

void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  Serial.print(message);
  server.send(404, "text/plain", message);
}

bool launchRefreshTokenFlow(SpotifyArduino *spotifyObj, char *clientId){
  spotify_refresh = spotifyObj;
  clientIdRefresh = clientId;

#ifdef USE_IP_ADDRESS
  // Building up callback URL using loopback address (required by Spotify for HTTP)
  sprintf(callbackURI, callbackURItemplate, callbackURIProtocol, "127.0.0.1", callbackURIPort, callbackURIAddress);
#endif

  server.on("/", handleRoot);
  server.on("/callback/", handleCallback);
  server.onNotFound(handleNotFound);
  server.begin();

  delay(100);

  while(!haveRefreshToken){
    server.handleClient();
  }

  return true;
}
