#include "agent_commands.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static bool is_whitespace_char(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool agent_is_command(const char *message, const char *name)
{
    if (!message || !name || name[0] == '\0') {
        return false;
    }

    while (*message && is_whitespace_char(*message)) {
        message++;
    }

    if (*message != '/') {
        return false;
    }

    size_t name_len = strlen(name);
    const char *cursor = message + 1;
    if (strncmp(cursor, name, name_len) != 0) {
        return false;
    }
    cursor += name_len;

    // Accept "/<name>", "/<name> payload", and "/<name>@bot payload".
    if (*cursor == '\0' || is_whitespace_char(*cursor)) {
        return true;
    }
    if (*cursor != '@') {
        return false;
    }
    cursor++;
    if (*cursor == '\0') {
        return false;
    }
    while (*cursor && !is_whitespace_char(*cursor)) {
        cursor++;
    }

    return true;
}

const char *agent_command_payload(const char *message, const char *name)
{
    const char *cursor;
    size_t name_len;

    if (!agent_is_command(message, name)) {
        return NULL;
    }

    while (*message && is_whitespace_char(*message)) {
        message++;
    }

    cursor = message + 1;
    name_len = strlen(name);
    cursor += name_len;

    if (*cursor == '@') {
        cursor++;
        while (*cursor && !is_whitespace_char(*cursor)) {
            cursor++;
        }
    }

    while (*cursor && is_whitespace_char(*cursor)) {
        cursor++;
    }

    return cursor;
}

static bool is_diag_scope_token(const char *token)
{
    if (!token) {
        return false;
    }

    return strcmp(token, "quick") == 0 ||
           strcmp(token, "runtime") == 0 ||
           strcmp(token, "memory") == 0 ||
           strcmp(token, "rates") == 0 ||
           strcmp(token, "time") == 0 ||
           strcmp(token, "all") == 0;
}

static bool parse_gpio_state_token(const char *token, int *state_out)
{
    if (!token || !state_out) {
        return false;
    }

    if (strcmp(token, "1") == 0 ||
        strcmp(token, "high") == 0 ||
        strcmp(token, "on") == 0) {
        *state_out = 1;
        return true;
    }

    if (strcmp(token, "0") == 0 ||
        strcmp(token, "low") == 0 ||
        strcmp(token, "off") == 0) {
        *state_out = 0;
        return true;
    }

    return false;
}

bool agent_parse_gpio_command_args(const char *message,
                                   const char **tool_name_out,
                                   cJSON *tool_input,
                                   char *error,
                                   size_t error_len)
{
    const char *payload = agent_command_payload(message, "gpio");
    char payload_buf[64];
    char *cursor;
    char *state_token;
    char *endptr = NULL;
    long pin;
    int state;

    if (!tool_name_out || !tool_input) {
        return false;
    }

    *tool_name_out = "gpio_read_all";

    if (!payload || payload[0] == '\0') {
        return true;
    }

    if (strlen(payload) >= sizeof(payload_buf)) {
        snprintf(error, error_len, "Error: /gpio arguments too long");
        return false;
    }

    snprintf(payload_buf, sizeof(payload_buf), "%s", payload);
    cursor = strtok(payload_buf, " \t\r\n");
    if (!cursor) {
        return true;
    }

    if (strcmp(cursor, "all") == 0) {
        if (strtok(NULL, " \t\r\n") != NULL) {
            snprintf(error, error_len, "Error: /gpio all does not take extra arguments");
            return false;
        }
        return true;
    }

    pin = strtol(cursor, &endptr, 10);
    if (!endptr || *endptr != '\0') {
        snprintf(error, error_len, "Error: unknown /gpio argument '%s' (use 'all' or a pin number)",
                 cursor);
        return false;
    }
    state_token = strtok(NULL, " \t\r\n");
    if (!state_token) {
        *tool_name_out = "gpio_read";
        cJSON_AddNumberToObject(tool_input, "pin", pin);
        return true;
    }

    if (!parse_gpio_state_token(state_token, &state)) {
        snprintf(error, error_len,
                 "Error: unknown GPIO state '%s' (use high/low/on/off/1/0)",
                 state_token);
        return false;
    }

    if (strtok(NULL, " \t\r\n") != NULL) {
        snprintf(error, error_len, "Error: /gpio takes at most a pin and optional state");
        return false;
    }

    *tool_name_out = "gpio_write";
    cJSON_AddNumberToObject(tool_input, "pin", pin);
    cJSON_AddNumberToObject(tool_input, "state", state);
    return true;
}

bool agent_parse_diag_command_args(const char *message,
                                   cJSON *tool_input,
                                   char *error,
                                   size_t error_len)
{
    const char *payload = agent_command_payload(message, "diag");
    char payload_buf[128];
    char *cursor;
    bool verbose = false;
    const char *scope = NULL;

    if (!payload || payload[0] == '\0') {
        return true;
    }

    if (strlen(payload) >= sizeof(payload_buf)) {
        snprintf(error, error_len, "Error: /diag arguments too long");
        return false;
    }

    snprintf(payload_buf, sizeof(payload_buf), "%s", payload);
    cursor = strtok(payload_buf, " \t\r\n");
    while (cursor) {
        for (size_t i = 0; cursor[i] != '\0'; i++) {
            cursor[i] = (char)tolower((unsigned char)cursor[i]);
        }

        if (strcmp(cursor, "verbose") == 0 || strcmp(cursor, "--verbose") == 0) {
            verbose = true;
        } else if (!scope && is_diag_scope_token(cursor)) {
            scope = cursor;
        } else {
            snprintf(error, error_len,
                     "Error: unknown /diag argument '%s' (use scope + optional verbose)",
                     cursor);
            return false;
        }

        cursor = strtok(NULL, " \t\r\n");
    }

    if (scope) {
        cJSON_AddStringToObject(tool_input, "scope", scope);
    }
    if (verbose) {
        cJSON_AddBoolToObject(tool_input, "verbose", true);
    }

    return true;
}

bool agent_is_slash_command(const char *message)
{
    if (!message) {
        return false;
    }

    while (*message && is_whitespace_char(*message)) {
        message++;
    }

    return *message == '/';
}

bool agent_is_cron_trigger_message(const char *message)
{
    if (!message) {
        return false;
    }

    while (*message && is_whitespace_char(*message)) {
        message++;
    }

    return strncmp(message, "[CRON ", 6) == 0;
}
