#ifndef I2S_HANDLER_H
#define I2S_HANDLER_H
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_timer.h"
#include <stdint.h>
#include <math.h>  // Include your WAV format header
#include "format_wav.h"
#define AUDIO_BUFF 2052

extern const char *I2S_TAG;

// Check if the I2S channel state is valid for operations
typedef enum {
    I2S_CHAN_STATE_REGISTER,                /*!< i2s channel is registered (not initialized)  */
    I2S_CHAN_STATE_READY,                   /*!< i2s channel is disabled (initialized) */
    I2S_CHAN_STATE_RUNNING,                 /*!< i2s channel is idling (initialized and enabled) */
} i2s_state_t;

// Structure to pass parameters to I2S task
typedef struct {
    i2s_chan_handle_t *tx_chan;
    wav_file_t wav_file;
    TaskHandle_t *task_handle;
} i2s_task_params_t;

/**
 * Initialize I2S in standard mode
 * 
 * @param handler Pointer to I2S channel handle
 * @return ESP_OK on success, or an error code
 */
esp_err_t i2s_init_std_mode(i2s_chan_handle_t *handler);

/**
 * Configure I2S bit depth
 * 
 * @param tx_chan Pointer to I2S channel handle
 * @param bits_per_sample Bit depth (16, 24, or 32)
 * @param task_handle Pointer to task handle (for error handling)
 * @return true on success, false on failure
 */
bool configure_i2s_bit_depth(i2s_chan_handle_t *tx_chan, uint16_t bits_per_sample, TaskHandle_t *task_handle);

/**
 * Task to transmit WAV file via I2S
 * 
 * @param pvParameters Pointer to i2s_task_params_t
 */
void i2s_transmit_wav_task(void *pvParameters);

/**
 * Normalize sample values based on bit depth
 * 
 * @param sample_value Sample value to normalize
 * @param shift_bit_depth Bit depth to shift by
 * @return Normalized sample value
 */
int32_t normalize_x_bits(int32_t sample_value, uint8_t shift_bit_depth);

/**
 * Clip sample values to fit within bit depth range
 * 
 * @param sample_value Sample value to clip
 * @param bit_depth Target bit depth
 * @return Clipped sample value
 */
int32_t clip_sample(int32_t sample_value, uint8_t bit_depth);

/**
  * Transmit WAV function
  * 
  * @param tx_handle Pointer to I2S channel handle
  * @param wav_file Pointer to WAV file structure
  * /
  */
void i2s_transmit_wav_function(i2s_chan_handle_t *tx_handle, wav_file_t *wav_file);
#endif /* I2S_HANDLER_H */