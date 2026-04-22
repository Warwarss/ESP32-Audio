/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#ifndef _FORMAT_WAV_H_
#define _FORMAT_WAV_H_
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Header structure for WAV file with only one data chunk
 *
 * @note See this for reference: http://soundfile.sapp.org/doc/WaveFormat/
 *
 * @note Assignment to variables in this struct directly is only possible for little endian architectures
 *       (including Xtensa & RISC-V)
 */
#pragma pack(push, 1)
typedef struct {
    struct {
        char chunk_id[4]; /*!< Contains the letters "RIFF" in ASCII form */
        uint32_t chunk_size; /*!< This is the size of the rest of the chunk following this number */
        char chunk_format[4]; /*!< Contains the letters "WAVE" */
    } descriptor_chunk; /*!< Canonical WAVE format starts with the RIFF header */
    struct {
        char subchunk_id[4]; /*!< Contains the letters "fmt " */
        uint32_t subchunk_size; /*!< This is the size of the rest of the Subchunk which follows this number */
        uint16_t audio_format; /*!< PCM = 1, values other than 1 indicate some form of compression */
        uint16_t num_of_channels; /*!< Mono = 1, Stereo = 2, etc. */
        uint32_t sample_rate; /*!< 8000, 44100, etc. */
        uint32_t byte_rate; /*!< ==SampleRate * NumChannels * BitsPerSample s/ 8 */
        uint16_t block_align; /*!< ==NumChannels * BitsPerSample / 8 */
        uint16_t bits_per_sample; /*!< 8 bits = 8, 16 bits = 16, etc. */
    } fmt_chunk; /*!< The "fmt " subchunk describes the sound data's format */
    struct {
        char subchunk_id[4]; /*!< Contains the letters "data" */
        uint32_t subchunk_size; /*!< ==NumSamples * NumChannels * BitsPerSample / 8 */
        //int16_t data[0]; /*!< Holds raw audio data */
    } data_chunk; /*!< The "data" subchunk contains the size of the data and the actual sound */
} wav_header_t;

typedef struct {
  uint32_t flags;         // Loop type (0 = one-shot, 1 = looped)
  uint16_t root_note;
  uint16_t question_mark;
  float    question_mark2;
  uint32_t numBeats;         // Tempo in 16.16 fixed-point (e.g., 120.0 BPM = 0x780000)
  uint16_t meter_denominator;  // Time signature numerator (e.g., 4 for 4/4)
  uint16_t meter_numerator;    // Time signature denominator (e.g., 4 for 4/4)
  float Tempo;  // Time signature denominator (e.g., 4 for 4/4)
} AcidChunk_t;

typedef struct {
  uint32_t manufacturer;      // MIDI Manufacturer ID (e.g., 0 = universal)
  uint32_t product;           // Product ID
  uint32_t samplePeriod;      // Nanoseconds per sample (e.g., 1e9 / 44100 ≈ 22676)
  uint32_t midiUnityNote;     // MIDI note for original pitch (0-127)
  uint32_t pitchFraction;     // Fine-tuning (0x0-0xFFFFFFFF = -100% to +100%)
  uint32_t smpteFormat;       // SMPTE timecode format (0 = none)
  uint32_t smpteOffset;       // SMPTE timecode offset
  uint32_t numLoops;          // Number of loop points
  uint32_t samplerDataSize;   // Extra data (often 0)
  // Followed by loop points (one per loop)
} SmplChunkHeader_t;

typedef struct {
  uint32_t cuePointID;        // Unique ID for the cue point
  uint32_t cuePointPos;       // Position in samples
  uint32_t dataChunkID;       // "data" or "slnt"
  uint32_t chunkStart;        // Start of the chunk
  uint32_t blockStart;        // Start of the block
  uint32_t sampleOffset;      // Offset in samples
} SmplChunkLoop_t;

typedef struct {
  char     chunkID[4];    // "cue "
  uint32_t chunkSize;     // Size of the chunk (varies)
  uint32_t numCuePoints;  // Number of cue points
  // Followed by cue points (one per cue)
} CueChunkHeader_t;

typedef struct {
  char     chunkID[4];    // "list"
  uint32_t chunkSize;     // Size of the chunk (varies)
  char     listType[4];   // Type of list (e.g., "adtl")
  // Followed by sub-chunks
} ListChunkHeader_t;

//write me inst chunk
typedef struct{
    uint8_t unshiftedNote;
    uint8_t fineTune;
    uint8_t gain;
    uint8_t lowNote;
    uint8_t highNote;
    uint8_t lowVelocity;
    uint8_t highVelocity;
}inst_chunk_t;

typedef struct {
    char id[4];
    uint32_t size;
} chunk_header_t;

typedef struct {
    wav_header_t wav_header;
    int16_t *data;
} wav_file_t;

#pragma pack(pop)
/**
 * @brief Default header for PCM format WAV files
 *
 */
#define WAV_HEADER_PCM_DEFAULT(wav_sample_size, wav_sample_bits, wav_sample_rate, wav_channel_num) { \
    .descriptor_chunk = { \
        .chunk_id = {'R', 'I', 'F', 'F'}, \
        .chunk_size = (wav_sample_size) + sizeof(wav_header_t) - 8, \
        .chunk_format = {'W', 'A', 'V', 'E'} \
    }, \
    .fmt_chunk = { \
        .subchunk_id = {'f', 'm', 't', ' '}, \
        .subchunk_size = 16, /* 16 for PCM */ \
        .audio_format = 1, /* 1 for PCM */ \
        .num_of_channels = (wav_channel_num), \
        .sample_rate = (wav_sample_rate), \
        .byte_rate = (wav_sample_bits) * (wav_sample_rate) * (wav_channel_num) / 8, \
        .block_align = (wav_sample_bits) * (wav_channel_num) / 8, \
        .bits_per_sample = (wav_sample_bits)\
    }, \
    .data_chunk = { \
        .subchunk_id = {'d', 'a', 't', 'a'}, \
        .subchunk_size = (wav_sample_size) \
    } \
}

#ifdef __cplusplus
}
#endif

#endif /* _FORMAT_WAV_H_ */ 