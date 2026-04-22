#include "wav_functions.h"

const char *WAV_TAG = "WAV_HANDLER";

void list_files(const char *path) {
    ESP_LOGI(WAV_TAG, "Opening directory: %s", path);
    DIR *dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(WAV_TAG, "Failed to open directory: %s", path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        ESP_LOGI(WAV_TAG, "Found file: %s", entry->d_name);
    }

    closedir(dir);
}

bool chunk_id_matches(char chunk[4], const char* chunkName) {
    for (int i = 0; i < 4; ++i) {
        if (chunk[i] != chunkName[i]) {
            return false;
        }
    }
    return true;
}

wav_file_t read_wav_file(const char *path) {
    wav_file_t wav_file = {0};
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(WAV_TAG, "Failed to open file for reading: %s", path);
        return wav_file;
    }

    // Read WAV header (first 36 bytes)
    if (fread(&wav_file.wav_header, 36, 1, file) != 1) {
        ESP_LOGE(WAV_TAG, "Failed to read WAV header");
        fclose(file);
        return wav_file;
    }

    // Print basic WAV info
    ESP_LOGI(WAV_TAG, "chunk_id: %.4s", wav_file.wav_header.descriptor_chunk.chunk_id);
    ESP_LOGI(WAV_TAG, "chunk_size: %lu", wav_file.wav_header.descriptor_chunk.chunk_size);
    ESP_LOGI(WAV_TAG, "chunk_format: %.4s", wav_file.wav_header.descriptor_chunk.chunk_format);
    ESP_LOGI(WAV_TAG, "subchunk_id: %.4s", wav_file.wav_header.fmt_chunk.subchunk_id);
    ESP_LOGI(WAV_TAG, "subchunk_size: %lu", wav_file.wav_header.fmt_chunk.subchunk_size);
    ESP_LOGI(WAV_TAG, "audio_format: %u", wav_file.wav_header.fmt_chunk.audio_format);
    ESP_LOGI(WAV_TAG, "num_of_channels: %u", wav_file.wav_header.fmt_chunk.num_of_channels);
    ESP_LOGI(WAV_TAG, "sample_rate: %lu", wav_file.wav_header.fmt_chunk.sample_rate);
    ESP_LOGI(WAV_TAG, "byte_rate: %lu", wav_file.wav_header.fmt_chunk.byte_rate);
    ESP_LOGI(WAV_TAG, "block_align: %u", wav_file.wav_header.fmt_chunk.block_align);
    ESP_LOGI(WAV_TAG, "bits_per_sample: %u", wav_file.wav_header.fmt_chunk.bits_per_sample);

    // Handle extra bytes in fmt chunk if any
    if (wav_file.wav_header.fmt_chunk.subchunk_size > 16) {
        fseek(file, wav_file.wav_header.fmt_chunk.subchunk_size - 16, SEEK_CUR);
    }

    // Check heap integrity
    if (!heap_caps_check_integrity_all(true)) {
        ESP_LOGE(WAV_TAG, "Heap corruption detected");
        fclose(file);
        return wav_file;
    }

    // Read all chunks until EOF
    chunk_header_t chunk_header;
    while (fread(&chunk_header.id, sizeof(char), 4, file) == 4) {
        if (fread(&chunk_header.size, sizeof(uint32_t), 1, file) != 1) {
            ESP_LOGE(WAV_TAG, "Failed to read chunk size");
            break;
        }

        // Handle data chunk - this is the actual audio data
        if (chunk_id_matches(chunk_header.id, "data")) {
            ESP_LOGI(WAV_TAG, "Found data chunk, size: %lu bytes", chunk_header.size);
            memcpy(wav_file.wav_header.data_chunk.subchunk_id, chunk_header.id, 4);
            wav_file.wav_header.data_chunk.subchunk_size = chunk_header.size;
            
            // Allocate memory for audio data
            wav_file.data = (int16_t *)heap_caps_malloc(chunk_header.size, MALLOC_CAP_SPIRAM);
            if (wav_file.data == NULL) {
                ESP_LOGE(WAV_TAG, "Failed to allocate memory for audio data");
                fclose(file);
                return wav_file;
            }
            
            // Read audio data
            if (fread(wav_file.data, 1, chunk_header.size, file) != chunk_header.size) {
                ESP_LOGE(WAV_TAG, "Failed to read audio data");
                free(wav_file.data);
                wav_file.data = NULL;
                fclose(file);
                return wav_file;
            }
            
            // Skip padding byte if chunk size is odd
            fseek(file, chunk_header.size % 2, SEEK_CUR);
        }
        // Handle LIST chunk - contains metadata
        else if (chunk_id_matches(chunk_header.id, "LIST")) {
            long list_end = ftell(file) + chunk_header.size;
            char listType[4];
            
            if (fread(&listType, sizeof(char), 4, file) != 4) {
                ESP_LOGE(WAV_TAG, "Failed to read LIST type");
                break;
            }
            
            ESP_LOGI(WAV_TAG, "LIST chunk type: %.4s", listType);
            
            // Process LIST subchunks
            while (ftell(file) < list_end) {
                chunk_header_t subchunk_header;
                
                if (fread(&subchunk_header, sizeof(chunk_header_t), 1, file) != 1) {
                    ESP_LOGE(WAV_TAG, "Failed to read LIST subchunk header");
                    break;
                }
                
                // Allocate and read subchunk data
                char *subchunk_data = (char *)malloc(subchunk_header.size + 1); // +1 for null terminator
                if (subchunk_data == NULL) {
                    ESP_LOGE(WAV_TAG, "Failed to allocate memory for LIST subchunk data");
                    break;
                }
                
                if (fread(subchunk_data, sizeof(char), subchunk_header.size, file) != subchunk_header.size) {
                    ESP_LOGE(WAV_TAG, "Failed to read LIST subchunk data");
                    free(subchunk_data);
                    break;
                }
                
                // Null-terminate the string
                subchunk_data[subchunk_header.size] = '\0';
                ESP_LOGI(WAV_TAG, "LIST subchunk %.4s: %s", subchunk_header.id, subchunk_data);
                
                free(subchunk_data);
                
                // Skip padding byte if chunk size is odd
                fseek(file, subchunk_header.size % 2, SEEK_CUR);
            }
        }
        // Handle smpl chunk - contains sample information (loops, etc.)
        else if (chunk_id_matches(chunk_header.id, "smpl")) {
            SmplChunkHeader_t smpl_chunk_header;
            
            if (fread(&smpl_chunk_header, sizeof(SmplChunkHeader_t), 1, file) != 1) {
                ESP_LOGE(WAV_TAG, "Failed to read smpl chunk data");
                fseek(file, chunk_header.size, SEEK_CUR);
            } else {
                ESP_LOGI(WAV_TAG, "smpl manufacturer: %lu", smpl_chunk_header.manufacturer);
                ESP_LOGI(WAV_TAG, "smpl product: %lu", smpl_chunk_header.product);
                ESP_LOGI(WAV_TAG, "smpl sample period: %lu", smpl_chunk_header.samplePeriod);
                ESP_LOGI(WAV_TAG, "smpl midi unity note: %lu", smpl_chunk_header.midiUnityNote);
                ESP_LOGI(WAV_TAG, "smpl midi pitch fraction: %lu", smpl_chunk_header.pitchFraction);
                ESP_LOGI(WAV_TAG, "smpl smpte format: %lu", smpl_chunk_header.smpteFormat);
                ESP_LOGI(WAV_TAG, "smpl smpte offset: %lu", smpl_chunk_header.smpteOffset);
                ESP_LOGI(WAV_TAG, "smpl num loops: %lu", smpl_chunk_header.numLoops);
                ESP_LOGI(WAV_TAG, "smpl sampler data size: %lu", smpl_chunk_header.samplerDataSize);
                
                // Skip any remaining bytes in the chunk
                long remaining = chunk_header.size - sizeof(SmplChunkHeader_t);
                if (remaining > 0) {
                    fseek(file, remaining, SEEK_CUR);
                }
            }
            
            // Skip padding byte if chunk size is odd
            fseek(file, chunk_header.size % 2, SEEK_CUR);
        }
        // Handle inst chunk - contains instrument parameters
        else if (chunk_id_matches(chunk_header.id, "inst")) {
            inst_chunk_t inst_chunk;
            
            if (fread(&inst_chunk, sizeof(inst_chunk_t), 1, file) != 1) {
                ESP_LOGE(WAV_TAG, "Failed to read inst chunk data");
                fseek(file, chunk_header.size, SEEK_CUR);
            } else {
                ESP_LOGI(WAV_TAG, "inst unshifted note: %u", inst_chunk.unshiftedNote);
                ESP_LOGI(WAV_TAG, "inst fine tune: %d", inst_chunk.fineTune);
                ESP_LOGI(WAV_TAG, "inst gain: %d", inst_chunk.gain);
                ESP_LOGI(WAV_TAG, "inst low note: %u", inst_chunk.lowNote);
                ESP_LOGI(WAV_TAG, "inst high note: %u", inst_chunk.highNote);
                ESP_LOGI(WAV_TAG, "inst low velocity: %u", inst_chunk.lowVelocity);
                ESP_LOGI(WAV_TAG, "inst high velocity: %u", inst_chunk.highVelocity);
                
                // Skip any remaining bytes in the chunk
                long remaining = chunk_header.size - sizeof(inst_chunk_t);
                if (remaining > 0) {
                    fseek(file, remaining, SEEK_CUR);
                }
            }
            
            // Skip padding byte if chunk size is odd
            fseek(file, chunk_header.size % 2, SEEK_CUR);
        }
        // Handle acid chunk - contains Acid loop information
        else if (chunk_id_matches(chunk_header.id, "acid")) {
            AcidChunk_t acid_chunk;
            
            if (fread(&acid_chunk, sizeof(AcidChunk_t), 1, file) != 1) {
                ESP_LOGE(WAV_TAG, "Failed to read acid chunk data");
                fseek(file, chunk_header.size, SEEK_CUR);
            } else {
                ESP_LOGI(WAV_TAG, "acid flags: %lu", acid_chunk.flags);
                ESP_LOGI(WAV_TAG, "acid root note: %u", acid_chunk.root_note);
                ESP_LOGI(WAV_TAG, "acid numBeats: %lu", acid_chunk.numBeats);
                ESP_LOGI(WAV_TAG, "acid meter_denominator: %u", acid_chunk.meter_denominator);
                ESP_LOGI(WAV_TAG, "acid meter_numerator: %u", acid_chunk.meter_numerator);
                ESP_LOGI(WAV_TAG, "acid tempo: %f", acid_chunk.Tempo);
                
                // Skip any remaining bytes in the chunk
                long remaining = chunk_header.size - sizeof(AcidChunk_t);
                if (remaining > 0) {
                    fseek(file, remaining, SEEK_CUR);
                }
            }
            
            // Skip padding byte if chunk size is odd
            fseek(file, chunk_header.size % 2, SEEK_CUR);
        }
        // Skip other unknown chunks
        else {
            ESP_LOGI(WAV_TAG, "Skipping unknown chunk: %.4s, size: %lu bytes", chunk_header.id, chunk_header.size);
            fseek(file, chunk_header.size + (chunk_header.size % 2), SEEK_CUR);
        }
    }

    // Final heap integrity check
    if (!heap_caps_check_integrity_all(true)) {
        ESP_LOGE(WAV_TAG, "Heap corruption detected after reading WAV file");
        if (wav_file.data != NULL) {
            free(wav_file.data);
            wav_file.data = NULL;
        }
        memset(&wav_file, 0, sizeof(wav_file_t));
    }

    fclose(file);
    return wav_file;
}

void free_wav_file(wav_file_t *wav_file) {
    if (wav_file == NULL) {
        return;
    }
    
    if (wav_file->data != NULL) {
        free(wav_file->data);
        wav_file->data = NULL;
    }
    
    // Clear the header too for safety
    memset(&wav_file->wav_header, 0, sizeof(wav_header_t));
}

void print_wav_info(const wav_file_t *wav_file) {
    if (wav_file == NULL) {
        ESP_LOGE(WAV_TAG, "Cannot print info for NULL WAV file");
        return;
    }
    
    ESP_LOGI(WAV_TAG, "WAV File Information:");
    ESP_LOGI(WAV_TAG, "-------------------");
    ESP_LOGI(WAV_TAG, "Format: %.4s", wav_file->wav_header.descriptor_chunk.chunk_format);
    ESP_LOGI(WAV_TAG, "Channels: %u", wav_file->wav_header.fmt_chunk.num_of_channels);
    ESP_LOGI(WAV_TAG, "Sample Rate: %lu Hz", wav_file->wav_header.fmt_chunk.sample_rate);
    ESP_LOGI(WAV_TAG, "Bit Depth: %u bits", wav_file->wav_header.fmt_chunk.bits_per_sample);
    ESP_LOGI(WAV_TAG, "Duration: %.2f seconds", 
             (float)wav_file->wav_header.data_chunk.subchunk_size / 
             wav_file->wav_header.fmt_chunk.byte_rate);
    ESP_LOGI(WAV_TAG, "Data Size: %lu bytes", wav_file->wav_header.data_chunk.subchunk_size);
    ESP_LOGI(WAV_TAG, "Total Size: %lu bytes", wav_file->wav_header.descriptor_chunk.chunk_size + 8);
    
    if (wav_file->data == NULL) {
        ESP_LOGW(WAV_TAG, "Audio data is NULL");
    }
}