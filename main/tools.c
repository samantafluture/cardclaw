#include "tools.h"
#include "tools_handlers.h"
#include "user_tools.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "tools";

// -----------------------------------------------------------------------------
// Tool Registry
// -----------------------------------------------------------------------------

#define TOOL_ENTRY(tool_name, tool_description, tool_schema, tool_execute) \
    { \
        .name = tool_name, \
        .description = tool_description, \
        .input_schema_json = tool_schema, \
        .execute = tool_execute \
    },

static const tool_def_t s_tools[] = {
#include "builtin_tools.def"
};

#undef TOOL_ENTRY

static const int s_tool_count = sizeof(s_tools) / sizeof(s_tools[0]);

void tools_init(void)
{
    // Initialize user-defined tools from NVS
    user_tools_init();

    ESP_LOGI(TAG, "Registered %d built-in tools, %d user tools",
             s_tool_count, user_tools_count());
    for (int i = 0; i < s_tool_count; i++) {
        ESP_LOGI(TAG, "  %s", s_tools[i].name);
    }
}

const tool_def_t *tools_get_all(int *count)
{
    *count = s_tool_count;
    return s_tools;
}

bool tools_execute(const char *name, const cJSON *input, char *result, size_t result_len)
{
    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            ESP_LOGI(TAG, "Exec: %s", name);
            return s_tools[i].execute(input, result, result_len);
        }
    }
    snprintf(result, result_len, "Unknown tool: %s", name);
    return false;
}
