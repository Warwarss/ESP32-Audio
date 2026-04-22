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
#include "uezuem.h"

#define BUF_SIZE (1024)
#define VOLTAGE_THRESHOLD 2900
#define SAMPLE_COUNT 2
static const char *TAG = "UART";

// Handles
i2s_chan_handle_t tx_handle;
static TaskHandle_t s_task_handle;
static TaskHandle_t i2s_task_handle;
adc_continuous_handle_t adc_handle = NULL;
// ADC Konfigürasyonu
static adc_channel_t channel[2] = {ADC_CHANNEL_5};
#define USED_ADC_UNITS ADC_UNIT_1
#define USED_ATTEN_VALUE ADC_ATTEN_DB_12

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

static esp_err_t i2s_init_std_mode(i2s_chan_handle_t *handler)
{
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    tx_chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, handler, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_4,
            .ws = GPIO_NUM_5,
            .dout = GPIO_NUM_2 ,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    }; 
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;  
    return i2s_channel_init_std_mode(*handler, &std_cfg);
};

void list_files(const char *path) {
    printf("Opening directory: %s\n", path);
    DIR *dir = opendir(path);
    if (dir == NULL) {
        printf("Failed to open directory: %s\n", path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        printf("Found file: %s\n", entry->d_name);
    }

    closedir(dir);
}

typedef struct {
    char id[4];
    uint32_t size;
} chunk_header_t;

#pragma pack(push, 1)
typedef struct {
    wav_header_t wav_header;
    int16_t *data;
} wav_file_t;
#pragma pack(pop)

static bool chunkIDMatches(char chunk[4], const char* chunkName)
{
  for (int i=0; i<4; ++i) {
    if (chunk[i] != chunkName[i]) {
      return false;
    }
  }
  return true;
}

wav_file_t read_wav_file(const char *path){
    wav_file_t wav_file = {0};
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        printf("Failed to open file for reading\n");
        return wav_file;
    }
    fread(&wav_file.wav_header, 36, 1, file);
    printf("chunk_id: %s\n", wav_file.wav_header.descriptor_chunk.chunk_id);
    printf("chunk_size: %lu\n", wav_file.wav_header.descriptor_chunk.chunk_size);
    printf("chunk_format: %s\n", wav_file.wav_header.descriptor_chunk.chunk_format);
    printf("subchunk_id: %s\n", wav_file.wav_header.fmt_chunk.subchunk_id);
    printf("subchunk_size: %lu\n", wav_file.wav_header.fmt_chunk.subchunk_size);
    printf("audio_format: %u\n", wav_file.wav_header.fmt_chunk.audio_format);
    printf("num_of_channels: %u\n", wav_file.wav_header.fmt_chunk.num_of_channels);
    printf("sample_rate: %lu\n", wav_file.wav_header.fmt_chunk.sample_rate);
    printf("byte_rate: %lu\n", wav_file.wav_header.fmt_chunk.byte_rate);
    printf("block_align: %u\n", wav_file.wav_header.fmt_chunk.block_align);
    printf("bits_per_sample: %u\n", wav_file.wav_header.fmt_chunk.bits_per_sample);
    if (wav_file.wav_header.fmt_chunk.subchunk_size > 16) {
        fseek(file, wav_file.wav_header.fmt_chunk.subchunk_size - 16, SEEK_CUR);
    }

    if (!heap_caps_check_integrity_all(true)) {
        printf("Heap corruption detected\n");
        vTaskDelete(NULL);
        fclose(file);
        return wav_file;
    }

    chunk_header_t chunk_header;
    while(fread(&chunk_header.id, sizeof(char), 4, file) == 4) {
        fread(&chunk_header.size, sizeof(uint32_t), 1, file);
        //long current_pos = ftell(file);
        if(chunkIDMatches(chunk_header.id, "data")) {
            printf("subchunk_id: %.4s\n", chunk_header.id);
            printf("Data chunk size: %lu\n", chunk_header.size);
            wav_file.wav_header.data_chunk.subchunk_size = chunk_header.size;
            wav_file.data = (int16_t *)malloc(wav_file.wav_header.data_chunk.subchunk_size);
            fread(wav_file.data, 1, wav_file.wav_header.data_chunk.subchunk_size, file);
            fseek(file, chunk_header.size % 2, SEEK_CUR);
        }
        else if (chunkIDMatches(chunk_header.id, "LIST")){
            long list_end = ftell(file) + chunk_header.size;
            char listType[4];
            fread(&listType, sizeof(char), 4, file);
            while(ftell(file) < list_end) {
                chunk_header_t subchunk_header;
                fread(&subchunk_header, sizeof(chunk_header_t), 1, file);
                char subchunk_data[sizeof(subchunk_header.size)];
                fread(&subchunk_data, sizeof(char), subchunk_header.size, file);
                printf("%.4s : %s\n", subchunk_header.id, subchunk_data);
                fseek(file, subchunk_header.size % 2, SEEK_CUR);
            }
        }
        else if(chunkIDMatches(chunk_header.id, "smpl")){
            SmplChunkHeader_t smpl_chunk_header;
            fread(&smpl_chunk_header, chunk_header.size, 1, file);
            printf("smpl manufacturer: %lu\n", smpl_chunk_header.manufacturer);
            printf("smpl product: %lu\n", smpl_chunk_header.product);
            printf("smpl sample period: %lu\n", smpl_chunk_header.samplePeriod);
            printf("smpl midi unity note: %lu\n", smpl_chunk_header.midiUnityNote);
            printf("smpl midi pitch fraction: %lu\n", smpl_chunk_header.pitchFraction);
            printf("smpl smpte format: %lu\n", smpl_chunk_header.smpteFormat);
            printf("smpl smpte offset: %lu\n", smpl_chunk_header.smpteOffset);
            printf("smpl num loops: %lu\n", smpl_chunk_header.numLoops);
            printf("smpl sampler data size: %lu\n", smpl_chunk_header.samplerDataSize);
            //long current_pos = ftell(file);
            //printf("Current position End: %ld\n", current_pos);    
            fseek(file, chunk_header.size % 2, SEEK_CUR);          
        }
        else if(chunkIDMatches(chunk_header.id, "inst")){
            inst_chunk_t inst_chunk;
            fread(&inst_chunk, chunk_header.size, 1, file);
            printf("inst unshifted note: %u\n", inst_chunk.unshiftedNote);
            printf("inst fine tune: %u\n", inst_chunk.fineTune);
            printf("inst gain: %u\n", inst_chunk.gain);
            printf("inst low note: %u\n", inst_chunk.lowNote);
            printf("inst high note: %u\n", inst_chunk.highNote);
            printf("inst low velocity: %u\n", inst_chunk.lowVelocity);
            printf("inst high velocity: %u\n", inst_chunk.highVelocity);
            fseek(file, chunk_header.size % 2, SEEK_CUR);
        }
        else if (chunkIDMatches(chunk_header.id, "acid")){
            AcidChunk_t acid_chunk;
            fread(&acid_chunk, chunk_header.size, 1, file);
            printf("Root Flags: %lu\n", acid_chunk.flags);
            printf("Root note: %u\n", acid_chunk.root_note);
            printf("numBeats: %lu\n", acid_chunk.numBeats);
            printf("meter_denominator: %u\n", acid_chunk.meter_denominator);
            printf("meter_numerator: %u\n", acid_chunk.meter_numerator);
            printf("Tempo: %f\n", acid_chunk.Tempo);
            fseek(file, chunk_header.size % 2, SEEK_CUR);
        }
        else{
            fseek(file, chunk_header.size + (chunk_header.size % 2), SEEK_CUR);
        }   
    } 
    
    if (!heap_caps_check_integrity_all(true)) {
        printf("Heap corruption detected\n");
        vTaskDelete(NULL);
        fclose(file);
        return wav_file;
    }

    printf("\n");
    fclose(file);
    return wav_file;
}

typedef struct {
    i2s_chan_handle_t *tx_chan;
    wav_file_t wav_file;
    TaskHandle_t *task_handle;
}i2s_task_params_t;

void i2s_transmit_wav_function(i2s_chan_handle_t *tx_handle, wav_file_t *wav_file){
    // Parameter acquisition
    wav_header_t *wav_header = &wav_file->wav_header;
    if (wav_header == NULL) {
        printf("Error: wav_header is NULL\n");
        *tx_handle = NULL;
        vTaskDelete(NULL);
    }
    int16_t *data = wav_file->data;
    if (data == NULL) {
        printf("Error: data is NULL\n");
        *tx_handle = NULL;
        vTaskDelete(NULL);
    }

    size_t written_bytes = 0;
    size_t data_size = wav_header->data_chunk.subchunk_size;
    size_t psram_size = esp_psram_get_size();
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    printf("PSRAM size: %zu\n", psram_size);
    printf("Internal free: %zu\n", internal_free);
    printf("Free 8BIT: %zu\n", free_8bit);
    printf("Free PSRAM: %zu\n", free_psram);
    printf("Allocating buffer of size: %zu bytes\n", data_size);
    i2s_channel_enable(*tx_handle);
    int16_t *buf = heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        printf("Error: Failed to allocate memory for buffer\n");
        vTaskDelete(NULL);
        return;
    }
    printf("Free heap size after calloc: %zu bytes\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    memcpy(buf, data, data_size);

    printf("Buffer buffer: ");
    for (int i = 0; i < 10; i++) {
        printf("%d ", buf[i]);
    }
    printf("Data buffer: ");
    for (int i = 0; i < 10; i++) {
        printf("%d ", data[i]);
    }
    printf("\n");
    int64_t start_time = esp_timer_get_time();
    if(data_size > 0){
        printf("I arrived here\n");  
        ESP_ERROR_CHECK(i2s_channel_write(*tx_handle, buf, data_size, &written_bytes, 1000));
        printf("Written bytes: %zu\n", written_bytes);  
    }
    int64_t end_time = esp_timer_get_time();
    int64_t time_taken_us = end_time - start_time;
    double time_taken_s = time_taken_us / 1000000.0;
    printf("Time taken to transmit: %.6f seconds\n", time_taken_s);
    printf("Data bytes: %zu\n", data_size);
    printf("Written bytes: %zu\n", written_bytes);
    
    i2s_channel_disable(*tx_handle);
    free(buf);
    //vTaskDelete(NULL);
}

void i2s_transmit_wav_task(void *pvParameters){
    // Parameter acquisition
    printf("Task start\n");
    i2s_task_params_t *params = (i2s_task_params_t *)pvParameters;
    i2s_chan_handle_t *tx_chan = params->tx_chan;
    wav_header_t *wav_header = &params->wav_file.wav_header;
    TaskHandle_t *task_handle = params->task_handle;
    int16_t *data = params->wav_file.data;
    if (wav_header == NULL) {
        printf("Error: wav_header is NULL\n");
        *task_handle = NULL;
        vTaskDelete(NULL);
    }
    if (data == NULL) {
        printf("Error: data is NULL\n");
        *task_handle = NULL;
        vTaskDelete(NULL);
    }
    size_t written_bytes = 0;
    size_t data_size = wav_header->data_chunk.subchunk_size;
    uint16_t bits_per_sample = wav_header->fmt_chunk.bits_per_sample;
    printf("bits_per_sample : %zu\n", bits_per_sample);
    switch(bits_per_sample)
     {
        case 16:
            printf("Case 16 \n");
            i2s_std_slot_config_t std_slot_config_16 = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
            i2s_channel_reconfig_std_slot(*tx_chan, &std_slot_config_16);
            break;
        case 24:
            printf("Case 24 \n");
            i2s_std_slot_config_t std_slot_config_24 = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_24BIT, I2S_SLOT_MODE_STEREO);
            i2s_channel_reconfig_std_slot(*tx_chan, &std_slot_config_24);
            break;
        case 32:
            i2s_std_slot_config_t std_slot_config_32 = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
            i2s_channel_reconfig_std_slot(*tx_chan, &std_slot_config_32);
            break;
        default:
            printf("Unsupported bit depth: %d\n", bits_per_sample);
            *task_handle = NULL;
            vTaskDelete(NULL);    
     }
    
    
/*     size_t psram_size = esp_psram_get_size();
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    printf("PSRAM size: %zu\n", psram_size);
    printf("Internal free: %zu\n", internal_free);
    printf("Free 8BIT: %zu\n", free_8bit);
    printf("Free PSRAM: %zu\n", free_psram);
    printf("Allocating buffer of size: %zu bytes\n", data_size);
 */
    if (!heap_caps_check_integrity_all(true)) {
        printf("Heap corruption detected\n");
        *task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    int16_t *buf = heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        printf("Error: Failed to allocate memory for buffer\n");
        *task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }
    // int16_t *buf = heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM);
    memcpy(buf, data, data_size);
    i2s_channel_enable(*tx_chan);
/*     printf("Buffer buffer: ");
    for (int i = 0; i < 10; i++) {
        printf("%d ", buf[i]);
    }
    printf("Data buffer: ");
    for (int i = 0; i < 10; i++) {
        printf("%d ", data[i]);
    }
    printf("\n"); */
    printf("Starting to transmit\n");  
    // Start time measurement
    int64_t start_time = esp_timer_get_time();
    ESP_ERROR_CHECK(i2s_channel_write(*tx_chan, buf, data_size, &written_bytes, 1000));
    // End time measurement
    int64_t end_time = esp_timer_get_time();
    int64_t time_taken_us = end_time - start_time;
    int64_t time_taken_ms = time_taken_us / 1000;
    printf("Written bytes: %zu\n", written_bytes);  
    printf("Data Size: %zu\n", data_size);
    printf("Time taken to transmit: %lld ms\n", time_taken_ms);
    i2s_channel_disable(*tx_chan);
    free(buf);
    *task_handle = NULL;
    printf("Task with sample rate : %zu has finished \n", bits_per_sample);  
    vTaskDelete(NULL);
} 

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
    if(adc_handle == NULL){
        ESP_LOGE("AnalogRead", "ADC handle is NULL");
        return -1;
    }
    esp_err_t ret;
    uint32_t ret_num = 0;
    uint8_t result[256] = {0};
    memset(result, 0xcc, 256);
    int sum_voltage = 0;
    int secondary_voltage;
    int voltage;
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) == 0) {
        ESP_LOGE("AnalogRead", "ADC conversion timeout");
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
                    //ESP_LOGI(TAG, "ADC%d Channel[%"PRIu32"] Cali Voltage: %d mV", ADC_UNIT_1 + 1, chan_num, voltage);
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
    double average_voltage = sum_voltage / SAMPLE_COUNT;
    return average_voltage;
}

void app_main(void){
    // LittleFS başlatılıyor
    initialize_littlefs();
    size_t total_bytes = 0;
    size_t used_bytes = 0;
    ESP_ERROR_CHECK(esp_littlefs_info("littlefs", &total_bytes, &used_bytes));
    printf("Total bytes: %d\n", total_bytes);
    printf("Used bytes: %d\n", used_bytes);
    // PSRAM Okunuyor
    size_t psram_size = esp_psram_get_size();
    printf("PSRAM size: %d bytes\n", psram_size);
    // ADC başlatılıyor
    adc_cali_handle_t adc1_cali_chan0_handle = NULL;
    s_task_handle = xTaskGetCurrentTaskHandle();
    continuous_adc_init(channel, sizeof(channel) / sizeof(adc_channel_t), &adc_handle, &adc1_cali_chan0_handle);
    // I2S başlatılıyor
    ESP_ERROR_CHECK(i2s_init_std_mode(&tx_handle));
    // WAV dosyası okunuyor
    wav_file_t wav_file = read_wav_file("/littlefs/Kick2.wav");
    wav_file_t wav_file_2 = read_wav_file("/littlefs/Clap4.wav");
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
    double average_voltage;
    bool kick_played = false;
    bool clap_played = false;
        while(1){
        average_voltage = AnalogRead(&adc_handle, &adc1_cali_chan0_handle);
        //printf("Voltage: %f mV\n", average_voltage);
        if(average_voltage >= 2900 && i2s_task_handle == NULL && !kick_played){
            printf("Playing WAW1\n");
            xTaskCreate(i2s_transmit_wav_task, "i2s", 4096, (void*) &params, 5, &i2s_task_handle);
            kick_played = true;
            clap_played = false;
            };

        if(average_voltage <= 2500 && i2s_task_handle == NULL && !clap_played){
            printf("Playing WAW2\n");
            xTaskCreate(i2s_transmit_wav_task, "i2s", 4096, (void*) &params_2, 5, &i2s_task_handle);
            clap_played = true;
            kick_played = false;
            };
        }
};
