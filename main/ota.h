#ifndef OTA_H
#define OTA_H

#include "esp_err.h"
#include <stdbool.h>

// Initialize OTA subsystem
esp_err_t ota_init(void);

// Get the currently running firmware version
const char *ota_get_version(void);

// Mark current firmware as valid (prevent rollback)
esp_err_t ota_mark_valid(void);

// Mark current firmware as valid only if pending verification.
esp_err_t ota_mark_valid_if_pending(void);

// Returns true when running image is waiting for rollback confirmation.
bool ota_is_pending_verify(void);

// Rollback to previous firmware
esp_err_t ota_rollback(void);

#endif // OTA_H
