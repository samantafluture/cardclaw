#ifndef LOCAL_ADMIN_H
#define LOCAL_ADMIN_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    LOCAL_ADMIN_ACTION_NONE = 0,
    LOCAL_ADMIN_ACTION_REBOOT,
    LOCAL_ADMIN_ACTION_FACTORY_RESET_REBOOT,
} local_admin_action_t;

void local_admin_set_safe_mode(bool safe_mode);
void local_admin_set_device_configured(bool device_configured);

bool local_admin_is_command(const char *message);
bool local_admin_handle_command(const char *message,
                                char *result,
                                size_t result_len,
                                local_admin_action_t *action_out);
void local_admin_perform_action(local_admin_action_t action);

bool local_admin_wifi_connect_from_store(void);

#ifdef TEST_BUILD
void local_admin_test_reset(void);
void local_admin_test_set_wifi_status(const char *status_text);
void local_admin_test_set_wifi_scan(const char *scan_text);
local_admin_action_t local_admin_test_last_action(void);
#endif

#endif // LOCAL_ADMIN_H
