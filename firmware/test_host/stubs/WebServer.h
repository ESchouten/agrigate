// Minimal stand-in so main.cpp can be syntax-checked on a PC (not the real library).
#pragma once
#include "WiFi.h"
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
enum HTTPMethod { HTTP_GET, HTTP_POST, HTTP_PUT };
struct WebServer {
  explicit WebServer(int port);
  void begin(); void stop(); void handleClient();
  void collectHeaders(const char* keys[], size_t n);
  void onNotFound(void (*fn)());
  HTTPMethod method(); String uri(); bool hasArg(const char*); String header(const char*);
  void send(int code, const char* type, const String& body);
  WiFiClient& client(); void setContentLength(size_t); void sendContent(const char*, size_t);
};
