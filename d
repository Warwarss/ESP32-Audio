[33mcommit ce8f09864f4b6f2f694428afac473f20612d8dfe[m[33m ([m[1;36mHEAD -> [m[1;32mmaster[m[33m)[m
Author: Warwarss <emirhanuzum@gmail.com>
Date:   Wed Apr 22 23:30:17 2026 +0200

    First commit

[1mdiff --git a/.gitignore b/.gitignore[m
[1mnew file mode 100644[m
[1mindex 0000000..89cc49c[m
[1m--- /dev/null[m
[1m+++ b/.gitignore[m
[36m@@ -0,0 +1,5 @@[m
[32m+[m[32m.pio[m
[32m+[m[32m.vscode/.browse.c_cpp.db*[m
[32m+[m[32m.vscode/c_cpp_properties.json[m
[32m+[m[32m.vscode/launch.json[m
[32m+[m[32m.vscode/ipch[m
[1mdiff --git a/.vscode/extensions.json b/.vscode/extensions.json[m
[1mnew file mode 100644[m
[1mindex 0000000..080e70d[m
[1m--- /dev/null[m
[1m+++ b/.vscode/extensions.json[m
[36m@@ -0,0 +1,10 @@[m
[32m+[m[32m{[m
[32m+[m[32m    // See http://go.microsoft.com/fwlink/?LinkId=827846[m
[32m+[m[32m    // for the documentation about the extensions.json format[m
[32m+[m[32m    "recommendations": [[m
[32m+[m[32m        "platformio.platformio-ide"[m
[32m+[m[32m    ],[m
[32m+[m[32m    "unwantedRecommendations": [[m
[32m+[m[32m        "ms-vscode.cpptools-extension-pack"[m
[32m+[m[32m    ][m
[32m+[m[32m}[m
[1mdiff --git a/.vscode/settings.json b/.vscode/settings.json[m
[1mnew file mode 100644[m
[1mindex 0000000..7a09dad[m
[1m--- /dev/null[m
[1m+++ b/.vscode/settings.json[m
[36m@@ -0,0 +1,7 @@[m
[32m+[m[32m{[m
[32m+[m[32m    "cmake.configureOnOpen": false,[m
[32m+[m[32m    "files.associations": {[m
[32m+[m[32m        "uart.h": "c",[m
[32m+[m[32m        "esp_event.h": "c"[m
[32m+[m[32m    }[m
[32m+[m[32m}[m
\ No newline at end of file[m
[1mdiff --git a/CMakeLists.txt b/CMakeLists.txt[m
[1mnew file mode 100644[m
[1mindex 0000000..2e9fe6b[m
[1m--- /dev/null[m
[1m+++ b/CMakeLists.txt[m
[36m@@ -0,0 +1,5 @@[m
[32m+[m[32mcmake_minimum_required(VERSION 3.16.0)[m
[32m+[m[32minclude($ENV{IDF_PATH}/tools/cmake/project.cmake)[m
[32m+[m[32mget_filename_component(configName "${CMAKE_BINARY_DIR}" NAME)[m
[32m+[m[32mlist(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/.pio/libdeps/${configName}/esp_littlefs")[m
[32m+[m[32mproject(Demo)[m
[1mdiff --git a/Demo.code-workspace b/Demo.code-workspace[m
[1mnew file mode 100644[m
[1mindex 0000000..1cedda8[m
[1m--- /dev/null[m
[1m+++ b/Demo.code-workspace[m
[36m@@ -0,0 +1,22 @@[m
[32m+[m[32m{[m
[32m+[m	[32m"folders": [[m
[32m+[m		[32m{[m
[32m+[m			[32m"path": "."[m
[32m+[m		[32m},[m
[32m+[m		[32m{[m
[32m+[m			[32m"name": "Demo2",[m
[32m+[m			[32m"path": "../Demo2"[m
[32m+[m		[32m}[m
[32m+[m	[32m],[m
[32m+[m	[32m"settings": {[m
[32m+[m		[32m"files.associations": {[m
[32m+[m			[32m"limits": "c",[m
[32m+[m			[32m"cmath": "c",[m
[32m+[m			[32m"format_wav.h": "c",[m
[32m+[m			[32m"esp_heap_caps.h": "c",[m
[32m+[m			[32m"esp_system.h": "c",[m
[32m+[m			[32m"sdkconfig.h": "c",[m
[32m+[m			[32m"adc_cali_scheme.h": "c"[m
[32m+[m		[32m}[m
[32m+[m	[32m}[m
[32m+[m[32m}[m
\ No newline at end of file[m
[1mdiff --git a/Eski_dosyalar/adc_functions.c b/Eski_dosyalar/adc_functions.c[m
[1mnew file mode 100644[m
[1mindex 0000000..820982f[m
[1m--- /dev/null[m
[1m+++ b/Eski_dosyalar/adc_functions.c[m
[36m@@ -0,0 +1,161 @@[m
[32m+[m[32m#include "adc_functions.h"[m
[32m+[m
[32m+[m[32mconst char *ADC_TAG = "ADC_HANDLER";[m
[32m+[m
[32m+[m[32mtypedef struct {[m
[32m+[m[32m    TaskHandle_t *task_handle;[m[41m [m
[32m+[m[32m} ADCCallbackContext;[m
[32m+[m
[32m+[m[32mbool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)[m
[32m+[m[32m{[m
[32m+[m[32m    adc_cali_handle_t handle = NULL;[m
[32m+[m[32m    esp_err_t ret = ESP_FAIL;[m
[32m+[m[32m    bool calibrated = false;[m
[32m+[m
[32m+[m[32m    if (!calibrated) {[m
[32m+[m[32m        ESP_LOGI(ADC_TAG, "calibration scheme version is %s", "Curve Fitting");[m
[32m+[m[32m        adc_cali_curve_fitting_config_t cali_config = {[m
[32m+[m[32m            .unit_id = unit,[m
[32m+[m[32m            .atten = atten,[m
[32m+[m[32m            .chan = channel,[m
[32m+[m[32m            .bitwidth = ADC_BITWIDTH_DEFAULT,[m
[32m+[m[32m        };[m
[32m+[m[32m        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);[m
[32m+[m[32m        if (ret == ESP_OK) {[m
[32m+[m[32m            calibrated = true;[m
[32m+[m[32m        }[m
[32m+[m[32m    }[m
[32m+[m
[32m+[m[32m    *out_handle = handle;[m
[32m+[m[32m    if (ret == ESP_OK) {[m
[32m+[m[32m        ESP_LOGI(ADC_TAG, "Calibration Success");[m
[32m+[m[32m    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {[m
[32m+[m[32m        ESP_LOGW(ADC_TAG, "eFuse not burnt, skip software calibration");[m
[32m+[m[32m    } else {[m
[32m+[m[32m        ESP_LOGE(ADC_TAG, "Invalid arg or no memory");[m
[32m+[m[32m    }[m
[32m+[m
[32m+[m[32m    return calibrated;[m
[32m+[m[32m}[m
[32m+[m
[32m+[m[32mbool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)[m
[32m+[m[32m{[m
[32m+[m[32m    BaseType_t mustYield = pdFALSE;[m
[32m+[m[32m    // Notify that ADC continuous driver has done enough number of conversions[m
[32m+[m[32m    if (user_data != NULL) {[m
[32m+[m[32m        ADCCallbackContext *context = (ADCCallbackContext *)user_data;[m
[32m+[m[32m        if (context -> task_handle != NULL) {[m
[32m+[m[32m            vTaskNotifyGiveFromISR(*(context->task_handle), &mustYield);[m
[32m+[m[32m        } else {[m
[32m+[m[32m            ESP_LOGE(ADC_TAG, "Task handle is NULL");[m
[32m+[m[32m        }[m
[32m+[m[32m    } else {[m
[32m+[m[32m        ESP_LOGE(ADC_TAG, "User data is NULL");[m
[32m+[m[32m    }[m
[32m+[m
[32m+[m[32m    return (mustYield == pdTRUE);[m
[32m+[m[32m}[m
[32m+[m
[32m+[m[32mvoid continuous_adc_init(adc_channel_t *channel, uint8_t channel_num, adc_continuous_handle_t *out_handle, adc_cali_handle_t *out_handle_cali, TaskHandle_t *task_handle) {[m
[32m+[m[32m    // Task Handle configuration, buffer and frame size[m
[32m+[m[32m    adc_continuous_handle_t handle = NULL;[m
[32m+[m[32m    adc_continuous_handle_cfg_t adc_config = {[m
[32m+[m[32m        // Lower these values for fewer samples and faster response time[m
[32m+[m[32m        .max_store_buf_size = 128,[m
[32m+[m[32m        .conv_frame_size = 128,[m
[32m+[m[32m    };[m
[32m+[m[32m    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &handle));[m
[32m+[m
[32m+[m[32m    // ADC configuration: sampling frequency, conversion mode, and format[m
[32m+[m[32m    adc_continuous_config_t dig_cfg = {[m
[32m+[m[32m        .pattern_num = channel_num,[m
[32m+[m[32m        .sample_freq_hz = 80 * 1000,[m
[32m+[m[32m        .conv_mode = ADC_CONV_SINGLE_UNIT_1,[m
[32m+[m[32m        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,[m
[32m+[m[32m    };[m
[32m+[m[41m    [m
[32m+[m[32m    // ADC pattern configuration: attenuation, channel, unit, and bit width[m
[32m+[m[32m    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};[m
[32m+[m[32m    for(int i = 0; i < channel_num; i++) {[m
[32m+[m[32m        adc_pattern[i].atten = USED_ATTEN_VALUE;[m
[32m+[m[32m        adc_pattern[i].channel = channel[i] & 0xF;[m
[32m+[m[32m        adc_pattern[i].unit = USED_ADC_UNITS;[m
[32m+[m[32m        adc_pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;[m
[32m+[m
[32m+[m[32m        ESP_LOGI(ADC_TAG, "adc_pattern[%d].atten is :%"PRIx8, i, adc_pattern[i].atten);[m
[32m+[m[32m        ESP_LOGI(ADC_TAG, "adc_pattern[%d].channel is :%"PRIx8, i, adc_pattern[i].channel);[m
[32m+[m[32m        ESP_LOGI(ADC_TAG, "adc_pattern[%d].unit is :%"PRIx8, i, adc_pattern[i].unit);[m
[32m+[m[32m    }[m
[32m+[m[32m    dig_cfg.adc_pattern = adc_pattern;[m
[32m+[m[32m    ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));[