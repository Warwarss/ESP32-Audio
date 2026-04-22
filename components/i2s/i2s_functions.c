#include "i2s_functions.h"
#include "esp_psram.h"

// Uncomment the line below to enable 16-bit to 24-bit conversion testing
#define ENABLE_16_TO_24_TESTING
#define ENABLE_24_TESTING
const char *I2S_TAG = "I2S_HANDLER";

// Required for mixing feature
extern QueueHandle_t xQueue;
extern QueueHandle_t fileQueue;

esp_err_t i2s_init_std_mode(i2s_chan_handle_t *handler) {
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
            .dout = GPIO_NUM_2,
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
}

bool configure_i2s_bit_depth(i2s_chan_handle_t *tx_chan, uint16_t bits_per_sample, TaskHandle_t *task_handle) {
    i2s_channel_disable(*tx_chan);
    switch(bits_per_sample) {
        case 16:
            ESP_LOGI(I2S_TAG, "Configuring I2S for 16-bit depth");
            i2s_std_slot_config_t std_slot_config_16 = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
            std_slot_config_16.slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT;
            i2s_channel_reconfig_std_slot(*tx_chan, &std_slot_config_16);
            break;
        case 24:
            ESP_LOGI(I2S_TAG, "Configuring I2S for 24-bit depth");
            i2s_std_slot_config_t std_slot_config_24 = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_24BIT, I2S_SLOT_MODE_STEREO);
            std_slot_config_24.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
            i2s_channel_reconfig_std_slot(*tx_chan, &std_slot_config_24);
            break;
        case 32:
            ESP_LOGI(I2S_TAG, "Configuring I2S for 32-bit depth");
            i2s_std_slot_config_t std_slot_config_32 = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
            i2s_channel_reconfig_std_slot(*tx_chan, &std_slot_config_32);
            break;
        default:
            ESP_LOGE(I2S_TAG, "Unsupported bit depth: %d", bits_per_sample);
            if (task_handle != NULL) {
                *task_handle = NULL;
            }
            return false;
    }
    i2s_channel_enable(*tx_chan);
    return true;
}

int32_t normalize_x_bits(int32_t sample_value, uint8_t shift_bit_depth) {
    switch (shift_bit_depth) {
        case 8:
            // Normalize to 16-bit range
            return sample_value << 8;
        case 16:
            // Normalize to 24-bit range
            return sample_value << 16;
        default:
            return sample_value;
    }
}

int32_t clip_sample(int32_t sample_value, uint8_t bit_depth) {
    switch (bit_depth) {
        case 16:
            // 16-bit: -32768 to 32767
            if (sample_value > INT16_MAX)
                return INT16_MAX;
            if (sample_value < INT16_MIN)
                return INT16_MIN;
            break;
            
        case 24:
            // 24-bit: -8388608 to 8388607
            if (sample_value > 8388607)
                return 8388607;
            if (sample_value < -8388608)
                return -8388608;
            break;
            
        case 32:
            // 32-bit: No clipping needed (unless int64_t input)
            // INT32_MIN to INT32_MAX is already the full range
            break;
            
        default:
            // Default to 16-bit for unknown bit depths
            if (sample_value > INT16_MAX)
                return INT16_MAX;
            if (sample_value < INT16_MIN)
                return INT16_MIN;
            break;
    }
    
    return sample_value;
}

void i2s_transmit_wav_task(void *pvParameters) {
    // Parameter acquisition
    if (xQueue == NULL) {
        ESP_LOGE(I2S_TAG, "Queue is not initialized");
        vTaskDelete(NULL);
        return;
    }
    
    xQueueReset(xQueue);
    ESP_LOGI(I2S_TAG, "I2S transmit task started");
    
    i2s_task_params_t *params = (i2s_task_params_t *)pvParameters;
    i2s_chan_handle_t *tx_chan = params->tx_chan;
    wav_header_t *wav_header = &params->wav_file.wav_header;
    TaskHandle_t *task_handle = params->task_handle;
    int16_t *data = params->wav_file.data;

    // Check for NULL pointers
    if (tx_chan == NULL || wav_header == NULL || task_handle == NULL || data == NULL) {
        ESP_LOGE(I2S_TAG, "Invalid parameters: %p, %p, %p, %p", 
                tx_chan, wav_header, task_handle, data);
        if (task_handle != NULL) {
            *task_handle = NULL;
        }
        vTaskDelete(NULL);
        return;
    }

    // Variables for mixing audio
    wav_file_t *audio_source = NULL;
    wav_file_t *new_audio_source = NULL;
    uint16_t audio_data_bit_depth = 0;
    uint16_t bit_difference = 0;
    size_t audio_data_size = 0;
    int16_t *audio_data = NULL;
    bool start_mixing = false;

    // Audio processing variables
    size_t written_bytes = 0;
    size_t total_sent_bytes = 0;
    size_t audio_total_sent_bytes = 0;
    size_t data_size = wav_header->data_chunk.subchunk_size;
    uint16_t bits_per_sample = wav_header->fmt_chunk.bits_per_sample;
    size_t bytes_per_sample = bits_per_sample / 8;
    size_t num_samples = data_size / bytes_per_sample;
    size_t remaining_bytes = data_size - total_sent_bytes;
    uint8_t* second_file_position = 0;
    size_t audio_data_byte_depth = 0;

    // Configure I2S for the current bit depth
    if (!configure_i2s_bit_depth(tx_chan, bits_per_sample, task_handle)) {
        ESP_LOGE(I2S_TAG, "Failed to configure I2S for bit depth: %d", bits_per_sample);
        *task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Heap integrity check
    if (!heap_caps_check_integrity_all(true)) {
        ESP_LOGE(I2S_TAG, "Heap corruption detected");
        *task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // Allocate buffer for audio data
    int16_t *buf = heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        ESP_LOGE(I2S_TAG, "Failed to allocate memory for buffer");
        *task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    // Copy data to buffer and enable I2S channel
    memcpy(buf, data, data_size);
    i2s_channel_enable(*tx_chan);
    
    ESP_LOGI(I2S_TAG, "Starting transmission");
    int64_t start_time = esp_timer_get_time();
    if(bits_per_sample == 16) {
        configure_i2s_bit_depth(tx_chan, 24, task_handle);
    }

    int32_t* printout = heap_caps_malloc(data_size * sizeof(int32_t), MALLOC_CAP_SPIRAM);
    int32_t* printout_ptr = printout;

    // Main transmission loop
    while (total_sent_bytes < data_size) {
        size_t bytes_to_write = fmin(AUDIO_BUFF, data_size - total_sent_bytes);
        uint8_t* current_pos = (uint8_t*)data + total_sent_bytes;
        
        // Check queue for new audio data for mixing
        if (xQueuePeek(xQueue, &new_audio_source, 0) == pdTRUE) {
            if (new_audio_source != audio_source) {
                xQueueReceive(xQueue, &audio_source, 0);    
                
                // Get properties of the new audio source
                audio_data_size = audio_source->wav_header.data_chunk.subchunk_size;
                audio_data_bit_depth = audio_source->wav_header.fmt_chunk.bits_per_sample;
                audio_data_byte_depth = audio_data_bit_depth / 8;
                audio_data = audio_source->data;
                start_mixing = true;
                
                // Handle bit depth differences
                bit_difference = abs(audio_data_bit_depth - bits_per_sample);
                if (bit_difference != 0) {
                    uint16_t target_bit_depth = (audio_data_bit_depth > bits_per_sample) ? 
                                               audio_data_bit_depth : bits_per_sample;
                    
                    if (!configure_i2s_bit_depth(tx_chan, target_bit_depth, task_handle)) {
                        ESP_LOGE(I2S_TAG, "Failed to configure I2S bit depth");
                        *task_handle = NULL;
                        vTaskDelete(NULL);
                        return;
                    }
                }
            }
        }

        // Handle standard playback (no mixing)
        if (!start_mixing) {
            switch (bits_per_sample) {
                case 16: {
#ifdef ENABLE_16_TO_24_TESTING
                    // Testing mode: Convert 16-bit to 24-bit
                    // configure_i2s_bit_depth(tx_chan, 24, task_handle); // Convert to 24-bit
                    size_t num_samples_16 = bytes_to_write / 2;
                    size_t bytes_to_write_24 = num_samples_16 * 3; // Convert to 24-bit
                    uint8_t  *dst_24 = heap_caps_malloc(bytes_to_write_24, MALLOC_CAP_SPIRAM);
                    if (dst_24 == NULL) {
                        ESP_LOGE(I2S_TAG, "Failed to allocate memory for 16-bit buffer");
                        *task_handle = NULL;
                        vTaskDelete(NULL);
                        return;
                    }
                    uint8_t *pdst_24 = dst_24;
                    const int16_t *psrc_16 = (const int16_t*)current_pos;
                    for (int i = 0; i < bytes_to_write / 2; i++) {
                        *pdst_24 = 0;
                        pdst_24 += 1;
                        *(int16_t*)pdst_24 = psrc_16[i];
                        pdst_24 += 2;
                        *printout_ptr = (int32_t)((*(pdst_24-3)) | (*(pdst_24-2) << 8) | (*(pdst_24-1) << 16));
                        if (i == 0){
                        ESP_LOGI(I2S_TAG, "10 printout samples: %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld",
                                 printout_ptr[0], printout_ptr[1], printout_ptr[2], printout_ptr[3], printout_ptr[4],
                                 printout_ptr[5], printout_ptr[6], printout_ptr[7], printout_ptr[8], printout_ptr[9]);
                        ESP_LOGI(I2S_TAG, "Bytes %02X %02X %02X Converted Sample: %ld", 
                                 *(pdst_24-1), *(pdst_24-2), *(pdst_24-3), *printout_ptr);
                        }
                        
                        /*if (i < 3) {
                            // Debugging output for the first few samples
                            ESP_LOGI(I2S_TAG, "Sample %d: %02X %02X %02X Converted Sample: %ld", i, 
                                     *(pdst_24-1), *(pdst_24-2), *(pdst_24-3), *printout_ptr);                        
                        }*/
                        printout_ptr++;
                    } 
                    ESP_ERROR_CHECK(i2s_channel_write(*tx_chan, dst_24, bytes_to_write_24, &written_bytes, 1000));
                    free(dst_24);
#else
                    // Normal mode: Play 16-bit audio directly
                    ESP_ERROR_CHECK(i2s_channel_write(*tx_chan, current_pos, bytes_to_write, &written_bytes, 1000));
#endif
                    break;
                }
                
                case 24: {
#ifdef ENABLE_24_TESTING
                    uint8_t *current_pos_24 = heap_caps_malloc(bytes_to_write, MALLOC_CAP_SPIRAM);          
                    if (current_pos_24 == NULL) {
                        ESP_LOGE(I2S_TAG, "Failed to allocate memory for 24-bit buffer");
                        *task_handle = NULL;
                        vTaskDelete(NULL);
                        return;
                    }
                    
                    for (int i = 0; i < (bytes_to_write / 3); i++) {
                        uint32_t sample = (uint32_t)(current_pos[i*3] | 
                                                    (current_pos[i*3 + 1] << 8) | 
                                                    (current_pos[i*3 + 2] << 16));      
                        current_pos_24[i*3] = sample & 0xFF;
                        current_pos_24[i*3 + 1] = (sample >> 8) & 0xFF;
                        current_pos_24[i*3 + 2] = (sample >> 16) & 0xFF;
                    }
                    
                    ESP_ERROR_CHECK(i2s_channel_write(*tx_chan, current_pos_24, bytes_to_write, &written_bytes, 1000));
                    free(current_pos_24);
#else
                    // Normal mode: Play 24-bit audio directly
                    ESP_ERROR_CHECK(i2s_channel_write(*tx_chan, current_pos, bytes_to_write, &written_bytes, 1000));
#endif
                    break;
                }
                
                case 32:
                    ESP_ERROR_CHECK(i2s_channel_write(*tx_chan, current_pos, bytes_to_write, &written_bytes, 1000));
                    break;
            }
        }
        // Handle mixing of audio
        else {
            remaining_bytes = data_size - total_sent_bytes;
            if (remaining_bytes < data_size) {
                // Determine highest bit depth
                uint8_t highest_bit_depth = (bits_per_sample > audio_data_bit_depth) ? 
                                            bits_per_sample : audio_data_bit_depth;
                
                // Position in second file
                second_file_position = (uint8_t*)audio_data + audio_total_sent_bytes;
                
                // Buffers for mixing
                uint8_t *mixed_buffer = NULL;
                uint32_t *mixed_buffer_32 = NULL;
                
                // Process each sample
                for (int i = 0; i < bytes_to_write / bytes_per_sample; i++) {
                    // If bit depths differ, handle normalization
                    if (bit_difference != 0) {
                        // Extract samples from both sources
                        int32_t sample1 = 0;
                        int16_t sample_16 = 0;
                        
                        // Extract first sample based on bit depth
                        if (bits_per_sample == 16) {
                            sample_16 = (int16_t)(current_pos[i*2] | (current_pos[i*2 + 1] << 8));  
                            sample1 = (int32_t)sample_16;
                        }
                        else if (bits_per_sample == 24) {
                            sample1 = (int32_t)(current_pos[i*3] | 
                                              (current_pos[i*3 + 1] << 8) | 
                                              (current_pos[i*3 + 2] << 16));  
                            if (sample1 & 0x800000) {
                                sample1 |= 0xFF000000; // Sign extension
                            }
                        }
                        else if (bits_per_sample == 32) {
                            sample1 = (int32_t)(current_pos[i*4] | 
                                              (current_pos[i*4 + 1] << 8) | 
                                              (current_pos[i*4 + 2] << 16) | 
                                              (current_pos[i*4 + 3] << 24));
                        }

                        // Extract second sample based on bit depth
                        int32_t sample2 = 0;
                        if (audio_data_bit_depth == 16) {
                            sample2 = (int16_t)(second_file_position[i*2] | 
                                              (second_file_position[i*2 + 1] << 8));  
                        }
                        else if (audio_data_bit_depth == 24) {
                            sample2 = (int32_t)(second_file_position[i*3] | 
                                              (second_file_position[i*3 + 1] << 8) | 
                                              (second_file_position[i*3 + 2] << 16));  
                            if (sample2 & 0x800000) {
                                sample2 |= 0xFF000000; // Sign extension
                            }
                        }
                        else if (audio_data_bit_depth == 32) {
                            sample2 = (int32_t)(second_file_position[i*4] | 
                                              (second_file_position[i*4 + 1] << 8) | 
                                              (second_file_position[i*4 + 2] << 16) | 
                                              (second_file_position[i*4 + 3] << 24));
                        }
                        
                        // Normalize samples to match bit depths
                        if (audio_data_bit_depth > bits_per_sample) {
                            sample1 = normalize_x_bits(sample1, bit_difference);
                        }
                        else if (bits_per_sample > audio_data_bit_depth) {
                            sample2 = normalize_x_bits(sample2, bit_difference);
                        }
                        
                        // Mix and clip
                        int32_t mixed = sample1 + sample2;
                        mixed = clip_sample(mixed, highest_bit_depth);

                        // Apply the mixed sample based on bit depth
                        switch (highest_bit_depth) {
                            case 16:
                                buf[(total_sent_bytes/bytes_per_sample) + i] = (int16_t)mixed;
                                break;
                            
                            case 24:
                                // For 24-bit, we need a special buffer
                                if (!mixed_buffer) {
                                    mixed_buffer = heap_caps_malloc(AUDIO_BUFF * 2, MALLOC_CAP_SPIRAM);
                                    if (!mixed_buffer) {
                                        ESP_LOGE(I2S_TAG, "Failed to allocate mixed buffer");
                                        *task_handle = NULL;
                                        vTaskDelete(NULL);
                                        return;
                                    }
                                }
                                
                                // Convert sample to 24-bit representation
                                mixed_buffer[i*3] = (uint8_t)(mixed & 0xFF);
                                mixed_buffer[i*3 + 1] = (uint8_t)((mixed >> 8) & 0xFF);
                                mixed_buffer[i*3 + 2] = (uint8_t)((mixed >> 16) & 0xFF);
                                break;
                            
                            case 32:
                                // For 32-bit, store directly
                                if ((total_sent_bytes/bytes_per_sample) + i < data_size/bytes_per_sample) {
                                    *((int32_t*)buf + (total_sent_bytes/bytes_per_sample) + i) = mixed;
                                }
                                break;
                        }
                    }
                    // If bit depths are the same, simpler mixing
                    else if (bit_difference == 0) {
                        switch (highest_bit_depth) {
                            case 16: {
                                int32_t sample1 = (int16_t)(current_pos[i*2] | (current_pos[i*2 + 1] << 8));
                                int32_t sample2 = (int16_t)(second_file_position[i*2] | (second_file_position[i*2 + 1] << 8));  
                                int32_t mixed = sample1 + sample2;
                                mixed = clip_sample(mixed, highest_bit_depth);
                                buf[(total_sent_bytes/bytes_per_sample) + i] = (int16_t)mixed;
                                break;
                            }
                            
                            case 24:
                                // Handle 24-bit mixing (similar to above)
                                // ...
                                break;

                            case 32:
                                // Handle 32-bit mixing (similar to above)
                                // ...
                                break;
                        }
                    }
                }

                // Write the mixed audio to I2S
                ESP_LOGI(I2S_TAG, "Bit difference: %d, Highest bit depth: %d", bit_difference, highest_bit_depth);
                switch (highest_bit_depth) {
                    case 16:
                        ESP_ERROR_CHECK(i2s_channel_write(*tx_chan, current_pos, bytes_to_write, &written_bytes, 1000));
                        break;
                        
                    case 24:
                        if (mixed_buffer) {
                            size_t samples_in_chunk = bytes_to_write / bytes_per_sample;
                            ESP_ERROR_CHECK(i2s_channel_write(*tx_chan, mixed_buffer, 
                                          (bytes_to_write / bytes_per_sample) * 3, &written_bytes, 1000));
                            free(mixed_buffer);
                            mixed_buffer = NULL;
                        }
                        break;
                        
                    case 32:
                        ESP_ERROR_CHECK(i2s_channel_write(*tx_chan, buf, bytes_to_write, &written_bytes, 1000));
                        break;
                }
                
                // Clean up and update counters
                audio_total_sent_bytes += written_bytes;
            }
        }
        
        total_sent_bytes += written_bytes;
    }
    
    // End time measurement and report
    int64_t end_time = esp_timer_get_time();
    int64_t time_taken_us = end_time - start_time;
    int64_t time_taken_ms = time_taken_us / 1000;
    ESP_LOGI(I2S_TAG, "Data Size: %zu bytes", data_size);
    ESP_LOGI(I2S_TAG, "Time taken to transmit: %lld ms", time_taken_ms);
    
    // Clean up
    i2s_channel_disable(*tx_chan);
    if(xQueueSend(fileQueue, (void *) &printout, 0) == pdTRUE){
        ESP_LOGI(I2S_TAG, "Queue send successful");
    }
    else {
        ESP_LOGE(I2S_TAG, "Queue send failed");
    }
    free(printout);
    *task_handle = NULL;
    ESP_LOGI(I2S_TAG, "Task with bit depth %u has finished", bits_per_sample);
    xQueueReset(xQueue);
    vTaskDelete(NULL);
}

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
