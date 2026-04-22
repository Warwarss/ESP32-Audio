#ifndef ADC_HANDLER_H
#define ADC_HANDLER_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <stdint.h>
#include <string.h>

#define VOLTAGE_THRESHOLD 2900
#define SAMPLE_COUNT 2

// Define ADC settings as extern so you can set them in main
extern const char *ADC_TAG;

#define USED_ADC_UNITS ADC_UNIT_1
#define USED_ATTEN_VALUE ADC_ATTEN_DB_12

/**
 * Initialize ADC calibration
 * 
 * @param unit ADC unit
 * @param channel ADC channel
 * @param atten ADC attenuation
 * @param out_handle Pointer to calibration handle
 * @return true if calibration successful, false otherwise
 */
bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);

/**
 * Callback for ADC conversion complete
 * 
 * @param handle ADC handle
 * @param edata Event data
 * @param user_data User data
 * @return true if yield required, false otherwise
 */
bool s_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data);

/**
 * Initialize ADC in continuous mode
 * 
 * @param channel Array of ADC channels
 * @param channel_num Number of channels
 * @param out_handle Pointer to ADC handle
 * @param out_handle_cali Pointer to calibration handle
 */
void continuous_adc_init(adc_channel_t *channel, uint8_t channel_num, adc_continuous_handle_t *out_handle, adc_cali_handle_t *out_handle_cali, TaskHandle_t *task_handle);

/**
 * Read ADC value and return average voltage
 * 
 * @param handle Pointer to ADC handle
 * @param adc1_cali_chan0_handle Pointer to calibration handle
 * @return Average voltage in mV, or -1 if error
 */
int AnalogRead(adc_continuous_handle_t *handle, adc_cali_handle_t *adc1_cali_chan0_handle);

#endif /* ADC_HANDLER_H */