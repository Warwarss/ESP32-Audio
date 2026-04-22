#include "adc_functions.h"

const char *ADC_TAG = "ADC_HANDLER";

typedef struct {
    TaskHandle_t *task_handle; 
} ADCCallbackContext;

bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

    if (!calibrated) {
        ESP_LOGI(ADC_TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .chan = channel,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(ADC_TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(ADC_TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(ADC_TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    BaseType_t mustYield = pdFALSE;
    // Notify that ADC continuous driver has done enough number of conversions
    if (user_data != NULL) {
        ADCCallbackContext *context = (ADCCallbackContext *)user_data;
        if (context -> task_handle != NULL) {
            vTaskNotifyGiveFromISR(*(context->task_handle), &mustYield);
        } else {
            ESP_LOGE(ADC_TAG, "Task handle is NULL");
        }
    } else {
        ESP_LOGE(ADC_TAG, "User data is NULL");
    }

    return (mustYield == pdTRUE);
}

void continuous_adc_init(adc_channel_t *channel, uint8_t channel_num, adc_continuous_handle_t *out_handle, adc_cali_handle_t *out_handle_cali, TaskHandle_t *task_handle) {
    // Task Handle configuration, buffer and frame size
    adc_continuous_handle_t handle = NULL;
    adc_continuous_handle_cfg_t adc_config = {
        // Lower these values for fewer samples and faster response time
        .max_store_buf_size = 128,
        .conv_frame_size = 128,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &handle));

    // ADC configuration: sampling frequency, conversion mode, and format
    adc_continuous_config_t dig_cfg = {
        .pattern_num = channel_num,
        .sample_freq_hz = 80 * 1000,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };
    
    // ADC pattern configuration: attenuation, channel, unit, and bit width
    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};
    for(int i = 0; i < channel_num; i++) {
        adc_pattern[i].atten = USED_ATTEN_VALUE;
        adc_pattern[i].channel = channel[i] & 0xF;
        adc_pattern[i].unit = USED_ADC_UNITS;
        adc_pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

        ESP_LOGI(ADC_TAG, "adc_pattern[%d].atten is :%"PRIx8, i, adc_pattern[i].atten);
        ESP_LOGI(ADC_TAG, "adc_pattern[%d].channel is :%"PRIx8, i, adc_pattern[i].channel);
        ESP_LOGI(ADC_TAG, "adc_pattern[%d].unit is :%"PRIx8, i, adc_pattern[i].unit);
    }
    dig_cfg.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));
    
    // Initialize calibration handle
    adc_cali_handle_t adc1_cali_chan0_handle = NULL;
    bool do_calibration1_chan0 = adc_calibration_init(ADC_UNIT_1, ADC_CHANNEL_0, ADC_ATTEN_DB_12, &adc1_cali_chan0_handle);
    
    // Configure ADC continuous event callbacks
    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = s_conv_done_cb,
    };
    
    // Register callbacks and start ADC
    ADCCallbackContext *context = heap_caps_malloc(sizeof(ADCCallbackContext), MALLOC_CAP_SPIRAM);
    if (context == NULL) {
        ESP_LOGE(ADC_TAG, "Failed to allocate memory for ADC callback context");
        return;
    }
    context->task_handle = task_handle;
    heap_caps_check_integrity_all(true);
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(handle, &cbs, context));
    ESP_ERROR_CHECK(adc_continuous_start(handle));

    *out_handle = handle;
    *out_handle_cali = adc1_cali_chan0_handle;
}

int AnalogRead(adc_continuous_handle_t *handle, adc_cali_handle_t *adc1_cali_chan0_handle) {
    if(*handle == NULL){
        ESP_LOGE(ADC_TAG, "ADC handle is NULL");
        return -1;
    }
    
    esp_err_t ret;
    uint32_t ret_num = 0;
    uint8_t result[256] = {0};
    memset(result, 0xcc, 256);
    int sum_voltage = 0;
    int voltage;
    
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) == 0) {
        ESP_LOGE(ADC_TAG, "ADC conversion timeout");
        return -1;  // Timeout error
    }
    
    for (int j = 0; j < SAMPLE_COUNT; j++) {
        ret = (adc_continuous_read(*handle, result, sizeof(result), &ret_num, 10));
        if (ret == ESP_OK) {
            // Process ADC data
            for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
                adc_digi_output_data_t *p = (adc_digi_output_data_t*)&result[i];
                uint32_t chan_num = p->type2.channel;
                uint32_t data = p->type2.data;
                ESP_ERROR_CHECK(adc_cali_raw_to_voltage(*adc1_cali_chan0_handle, data, &voltage));
                
                // Add voltage to sum
                sum_voltage = sum_voltage + voltage;
                break;
            }
            vTaskDelay(1); // 1ms delay
        } else if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGE(ADC_TAG, "ADC read timeout");
            return -1;
        } else {
            ESP_LOGE(ADC_TAG, "ADC read error");
            return -1;
        }
    }
    
    // Calculate average voltage
    int average_voltage = sum_voltage / SAMPLE_COUNT;
    return average_voltage;
}