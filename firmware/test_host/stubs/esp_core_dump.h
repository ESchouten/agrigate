// Minimal stand-ins so main.cpp can be syntax-checked on a PC (not the real libraries).
#pragma once
#include <cstddef>
#include <cstdint>
typedef int esp_err_t;
enum { ESP_OK = 0 };
struct esp_core_dump_bt_info_t { uint32_t bt[16]; uint32_t depth; bool corrupted; };
struct esp_core_dump_summary_t { char exc_task[16]; uint32_t exc_pc; esp_core_dump_bt_info_t exc_bt_info; };
esp_err_t esp_core_dump_get_summary(esp_core_dump_summary_t*);
esp_err_t esp_core_dump_image_get(size_t*, size_t*);
