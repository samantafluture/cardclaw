#ifndef AGENT_COMMANDS_H
#define AGENT_COMMANDS_H

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

bool agent_is_command(const char *message, const char *name);
bool agent_is_slash_command(const char *message);
bool agent_is_cron_trigger_message(const char *message);
const char *agent_command_payload(const char *message, const char *name);
bool agent_parse_gpio_command_args(const char *message,
                                   const char **tool_name_out,
                                   cJSON *tool_input,
                                   char *error,
                                   size_t error_len);
bool agent_parse_diag_command_args(const char *message,
                                   cJSON *tool_input,
                                   char *error,
                                   size_t error_len);

#endif // AGENT_COMMANDS_H
