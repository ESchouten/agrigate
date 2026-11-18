// Minimal stand-ins so main.cpp can be syntax-checked on a PC (not the real libraries).
#pragma once
#include "esp_core_dump.h"
esp_err_t esp_flash_read(void*, void*, uint32_t, uint32_t);
