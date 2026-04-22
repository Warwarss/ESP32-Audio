#ifndef FUNCTIONS_WAV_H
#define FUNCTIONS_WAV_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "format_wav.h"
extern const char *WAV_TAG;

/**
 * List all files in a directory
 * 
 * @param path Directory path to list
 */
void list_files(const char *path);

/**
 * Check if a chunk ID matches a specified name
 * 
 * @param chunk The 4-character chunk ID to check
 * @param chunkName The name to compare against
 * @return true if they match, false otherwise
 */
bool chunk_id_matches(char chunk[4], const char* chunkName);

/**
 * Read a WAV file and parse its contents
 * 
 * @param path Path to the WAV file
 * @return A wav_file_t structure containing the parsed WAV data
 */
wav_file_t read_wav_file(const char *path);

/**
 * Free memory allocated for WAV file data
 * 
 * @param wav_file Pointer to the WAV file structure to free
 */
void free_wav_file(wav_file_t *wav_file);

/**
 * Print details about a WAV file for debugging
 * 
 * @param wav_file The WAV file to print information about
 */
void print_wav_info(const wav_file_t *wav_file);

#endif /* FORMAT_WAV_H */