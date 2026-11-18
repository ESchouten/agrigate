// Minimal stand-ins so main.cpp can be syntax-checked on a PC (not the real libraries).
#pragma once
#include <cstdarg>
typedef int (*vprintf_like_t)(const char*, va_list);
vprintf_like_t esp_log_set_vprintf(vprintf_like_t);
