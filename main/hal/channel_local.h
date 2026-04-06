#ifndef CHANNEL_LOCAL_H
#define CHANNEL_LOCAL_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Start the local channel (keyboard → agent, agent → display).
// input_queue: shared inbound queue (same one Telegram/serial use)
// output_queue: dedicated display output queue
esp_err_t channel_local_start(QueueHandle_t input_queue,
                               QueueHandle_t output_queue);

#endif // CHANNEL_LOCAL_H
