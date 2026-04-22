#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <stdint.h>
#include <math.h>
#include "dirent.h"
#include "format_wav.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "sdkconfig.h"
#include "i2s_pdm_example.h"
#include "i2s_example_pins.h"
#include "esp_littlefs.h"
#include "esp_timer.h"
#include "esp_psram.h"
#include "adc_functions.h"
#include "wav_functions.h"
#include "i2s_functions.h"

#define BUF_SIZE (1024)
#define AUDIO_BUFF 2052
#define VOLTAGE_THRESHOLD 2900
#define SAMPLE_COUNT 2
static const char *TAG = "UART";

// Handles
i2s_chan_handle_t tx_handle;
static TaskHandle_t s_task_handle;
static TaskHandle_t i2s_task_handle;
adc_continuous_handle_t adc_handle = NULL;
QueueHandle_t xQueue;
QueueHandle_t fileQueue;

// ADC Konfigürasyonu
static adc_channel_t channel[2] = {ADC_CHANNEL_5};
#define USED_ADC_UNITS ADC_UNIT_1
#define USED_ATTEN_VALUE ADC_ATTEN_DB_12

void initialize_littlefs(){
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return;
    }
    list_files("/littlefs");
    if (!heap_caps_check_integrity_all(true)) {
        printf("Heap corruption detected\n");
        return;
    }
}

void app_main(void){
    // LittleFS başlatılıyor
    initialize_littlefs();
    esp_psram_get_size();
    size_t total_bytes = 0;
    size_t used_bytes = 0;
    ESP_ERROR_CHECK(esp_littlefs_info("littlefs", &total_bytes, &used_bytes));
    printf("Total bytes: %d\n", total_bytes);
    printf("Used bytes: %d\n", used_bytes);
    // PSRAM Okunuyor
    size_t psram_size = esp_psram_get_size();
    printf("PSRAM size: %d bytes\n", psram_size);
    // Queue başlatılıyor
    xQueue = xQueueCreate(1, sizeof(struct wav_file_t *));
    #define QUEUE_LENGTH 1
    #define QUEUE_ITEM_SIZE sizeof(int32_t*)
    StaticQueue_t xQueueBuffer;
    uint8_t ucQueueStorage[QUEUE_LENGTH * QUEUE_ITEM_SIZE];
    fileQueue = xQueueCreateStatic(QUEUE_LENGTH, QUEUE_ITEM_SIZE, &(ucQueueStorage[0]), &xQueueBuffer);
    // ADC başlatılıyor
    adc_cali_handle_t adc1_cali_chan0_handle = NULL;
    s_task_handle = xTaskGetCurrentTaskHandle();
    continuous_adc_init(channel, sizeof(channel) / sizeof(adc_channel_t), &adc_handle, &adc1_cali_chan0_handle, &s_task_handle);
    // I2S başlatılıyor
    ESP_ERROR_CHECK(i2s_init_std_mode(&tx_handle));
    // WAV dosyası okunuyor
    wav_file_t wav_file = read_wav_file("/littlefs/Kick2.wav");
    wav_file_t wav_file_2 = read_wav_file("/littlefs/Snare.wav");
    // I2S görevi başlatılıyor
    i2s_task_params_t params = {
        .tx_chan = &tx_handle,
        .wav_file = wav_file,
        .task_handle = &i2s_task_handle,
    };
    i2s_task_params_t params_2 = {
        .tx_chan = &tx_handle,
        .wav_file = wav_file_2,
        .task_handle = &i2s_task_handle,
    };
    int32_t *printout;
    double average_voltage;
    bool kick_played = false;
    bool clap_played = false;
        while(1){
        if (xQueueReceive(fileQueue, &printout, 0) == pdTRUE) {
            printf("Received- 10 Samples from printout: %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld\n", 
                printout[0], printout[1], printout[2], printout[3], printout[4], 
                printout[5], printout[6], printout[7], printout[8], printout[9]);
            xQueueReset(fileQueue);
        }
        average_voltage = AnalogRead(&adc_handle, &adc1_cali_chan0_handle);
        printf("Voltage: %f mV\n", average_voltage);
        if(average_voltage >= 2900 && !kick_played){
            if (i2s_task_handle == NULL){
                printf("Playing WAW1\n");
                xTaskCreate(i2s_transmit_wav_task, "i2s", 4096, (void*) &params, 5, &i2s_task_handle);
            kick_played = true;
            clap_played = false;
            }
            else if(i2s_task_handle != NULL){
                printf("Mixing Wav1 and Wav2\n");
                wav_file_t *wav_ptr = &wav_file;
                xQueueSend(xQueue, ( void * ) &wav_ptr, 0);
            };
        }
        if(average_voltage <= 2000 && !clap_played){
            if(i2s_task_handle == NULL){
            printf("Playing WAW2\n");
            xTaskCreate(i2s_transmit_wav_task, "i2s", 4096, (void*) &params_2, 5, &i2s_task_handle);
            clap_played = true;
            kick_played = false;
            }
            else if(i2s_task_handle != NULL){
                printf("Mixing Wav2 and Wav1\n");
                wav_file_t *wav_ptr = &wav_file_2;
                xQueueSend(xQueue, ( void * ) &wav_ptr, 0);
            }
        }
        }    
};
