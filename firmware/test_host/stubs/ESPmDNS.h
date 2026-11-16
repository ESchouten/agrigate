// Minimal stand-in so main.cpp can be syntax-checked on a PC (not the real library).
#pragma once
struct MDNSResponder {
  bool begin(const char* host); void end(); bool addService(const char* service, const char* proto, int port);
};
extern MDNSResponder MDNS;
