#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "driver/uart.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#define BUF_SIZE (1024)
#define VOLTAGE_THRESHOLD 2900
#define SAMPLE_COUNT 10
static const char *TAG = "UART";

static adc_channel_t channel[2] = {ADC_CHANNEL_0};
#define USED_ADC_UNITS ADC_UNIT_1
#define USED_ATTEN_VALUE ADC_ATTEN_DB_12

 static TaskHandle_t s_task_handle;

void echo_task (void *arg) {
    // UART'nin konfigürasyonu, baud rate, data bits, parity, stop bits, flow control ve clock source
    // USB kullanıyorsan uart kanalı 0, eğer GPIO kullanıyorsan uart kanalı 1 olmalı
   const uart_port_t uart_num = UART_NUM_0;
   uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity    = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
    };  
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    // ESP_ERROR_CHECK(uart_set_pin(uart_num, 17, 18, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    QueueHandle_t uart_queue;
    ESP_ERROR_CHECK(uart_driver_install(uart_num, BUF_SIZE * 2, BUF_SIZE * 2, 10, &uart_queue, 0));
    char* test_str = "This is a test string.\n";
    while (1){
        uart_write_bytes(uart_num, (const char *)test_str, strlen(test_str));
        vTaskDelay(1000 / portTICK_PERIOD_MS); // 1 second delay
    }
}

// ADC calibration fonksiyonu, unit, channel, atten ve handle parametreleri alır
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
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
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

// ADC continuous driver event callback function, called when enough number of conversions are done
static bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    BaseType_t mustYield = pdFALSE;
    //Notify that ADC continuous driver has done enough number of conversions
    vTaskNotifyGiveFromISR(s_task_handle, &mustYield);

    return (mustYield == pdTRUE);
}

static void continuous_adc_init(adc_channel_t *channel, uint8_t channel_num, adc_continuous_handle_t *out_handle, adc_cali_handle_t *out_handle_cali) {
    // Task Handle'ın konfigürasyonu, buffer ve frame size
    adc_continuous_handle_t handle = NULL;
    adc_continuous_handle_cfg_t adc_config = {
        // Bu değerler azalırsa daha az örnek alınır, ve tepki süresi azalır
        .max_store_buf_size = 128,
        .conv_frame_size = 128,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &handle));

    // ADC'nin konfigürasyonu, örnekleme frekansı, dönüşüm modu ve formatı
    adc_continuous_config_t dig_cfg = {
        .pattern_num = channel_num,
        .sample_freq_hz = 80 * 1000,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };
    // ADC'nin pattern'larının konfigürasyonu, atten, channel, unit ve bit_width
    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};
    for(int i = 0; i < channel_num; i++) {
        adc_pattern[i].atten = USED_ATTEN_VALUE;
        adc_pattern[i].channel = channel[i] & 0xF;
        adc_pattern[i].unit = USED_ADC_UNITS;
        adc_pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

        ESP_LOGI(TAG, "adc_pattern[%d].atten is :%"PRIx8, i, adc_pattern[i].atten);
        ESP_LOGI(TAG, "adc_pattern[%d].channel is :%"PRIx8, i, adc_pattern[i].channel);
        ESP_LOGI(TAG, "adc_pattern[%d].unit is :%"PRIx8, i, adc_pattern[i].unit);
    }
    dig_cfg.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));
    // initialize calibration handle
    adc_cali_handle_t adc1_cali_chan0_handle = NULL;
    bool do_calibration1_chan0 = example_adc_calibration_init(ADC_UNIT_1, ADC_CHANNEL_0, ADC_ATTEN_DB_12, &adc1_cali_chan0_handle);
    // ADC continuous event callback'lerinin konfigürasyonu, ve başlatılması
    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = s_conv_done_cb,
    };
    // ADC continuous event callback'lerinin tayini
    // ADC ünitesinin başlatılması
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(handle, &cbs, NULL));
    ESP_ERROR_CHECK(adc_continuous_start(handle));

    *out_handle = handle;
    *out_handle_cali = adc1_cali_chan0_handle;

    
}

int AnalogRead(adc_continuous_handle_t *handle, adc_cali_handle_t *adc1_cali_chan0_handle) {
    esp_err_t ret;
    uint32_t ret_num = 0;
    uint8_t result[256] = {0};
    memset(result, 0xcc, 256);
    int sum_voltage = 0;
    int secondary_voltage;
    int voltage;
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) == 0) {
        ESP_LOGE("AnalogRead", "ADC conversion timeout");
        ESP_ERROR_CHECK(adc_continuous_stop(*handle));
        return -1;  // Timeout error
    }
    for (int j = 0; j < SAMPLE_COUNT; j++) {
        ret = (adc_continuous_read(*handle, result, sizeof(result), &ret_num, 10));
            if (ret == ESP_OK){
                //ESP_LOGI("TASK", "ret is %x, ret_num is %"PRIu32" bytes", ret, ret_num);
                for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
                    adc_digi_output_data_t *p = (adc_digi_output_data_t*)&result[i];
                    uint32_t chan_num = p->type2.channel;
                    uint32_t data = p->type2.data;
                    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(*adc1_cali_chan0_handle, data, &voltage));
                    if (voltage > VOLTAGE_THRESHOLD) {
                        //ADC Extension logic buraya gelecekti
                        int secondary_voltage;                   
                    }
                    ESP_LOGI(TAG, "ADC%d Channel[%"PRIu32"] Cali Voltage: %d mV", ADC_UNIT_1 + 1, chan_num, voltage);
                    sum_voltage = sum_voltage + voltage;
                    break;
                }
                vTaskDelay(1); // 1 second delay
            }
            else if (ret == ESP_ERR_TIMEOUT){
                ESP_LOGE(TAG, "ADC read timeout");
                return -1;
            }
            else {
                ESP_LOGE(TAG, "ADC read error");
                return -1;
            }
    }
    free(result);
    double average_voltage = sum_voltage / SAMPLE_COUNT;
    return average_voltage;
}

void app_main(void) {
    esp_err_t ret;
    uint32_t ret_num = 0;
    // 256 byte'lık bir buffer oluşturuluyor ve içi 0xcc ile dolduruluyor
    uint8_t result[256] = {0};
    memset(result, 0xcc, 256);
    int voltage;
    double average_voltage;
    s_task_handle = xTaskGetCurrentTaskHandle();
    // ADC task handle'ı oluşturup init ediliyor. bu kodda oluşturulen handle, 
    // çağrılan fonksiyonun içinde üretilen handle'a eşitleniyor
    adc_continuous_handle_t handle = NULL;
    adc_cali_handle_t adc1_cali_chan0_handle = NULL;
    continuous_adc_init(channel, sizeof(channel) / sizeof(adc_channel_t), &handle, &adc1_cali_chan0_handle);
    AnalogRead(&handle, &adc1_cali_chan0_handle);
    while(1){
        average_voltage = AnalogRead(&handle, &adc1_cali_chan0_handle);
        printf("Voltage: %f mV\n", average_voltage);
        AnalogRead(&handle, &adc1_cali_chan0_handle);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    // ADC continuous event callback'lerinin konfigürasyonu, ve başlatılması
    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = s_conv_done_cb,
    };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(handle, &cbs, NULL));
    ESP_ERROR_CHECK(adc_continuous_start(handle));
    while (1){
        // Callback fonksiyonu pdTRUE döndüğünde, bu fonksiyon çalışıyor
        // yani ADC yeteri kadar conversion yapıp işlemi bitirdiğinde bu fonksiyon çalışıyor
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        char unit[] = "ADC_UNIT_1";
        while (1){
            ret = (adc_continuous_read(handle, result, sizeof(result), &ret_num, 10));
            if (ret == ESP_OK){
                ESP_LOGI("TASK", "ret is %x, ret_num is %"PRIu32" bytes", ret, ret_num);
                for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
                    adc_digi_output_data_t *p = (adc_digi_output_data_t*)&result[i];
                    uint32_t chan_num = p->type2.channel;
                    uint32_t data = p->type2.data;
                    printf("Raw Data : %"PRIx32" \n", data);
                    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan0_handle, data, &voltage));
                    ESP_LOGI(TAG, "ADC%d Channel[%"PRIu32"] Cali Voltage: %d mV", ADC_UNIT_1 + 1, chan_num, voltage);

                    /* Check the channel number validation, the data is invalid if the channel num exceed the maximum channel */
                    // if (chan_num < 10) {
                    //     ESP_LOGI(TAG, "Unit: %s, Channel: %"PRIu32", Value: %f"PRIx32, unit, chan_num, mV);
                    // } else {
                    //     ESP_LOGW(TAG, "Invalid data [%s_%"PRIu32"_%"PRIx32"]", unit, chan_num, data);
                    // }
                }
                vTaskDelay(1); // 1 second delay
            }
            else if (ret == ESP_ERR_TIMEOUT){
                ESP_LOGE(TAG, "ADC read timeout");
                break;
            }
        }
    }

    xTaskCreate(echo_task, "uart_echo_task", 4096, NULL, 10, NULL);
};