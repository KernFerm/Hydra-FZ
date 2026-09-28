/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HYDRA_EXTERNAL_PROTOCOL_VERSION 1U
#define HYDRA_EXTERNAL_LINE_MAX 256U

typedef enum { HydraExternalNone, HydraExternalInfo, HydraExternalStatus, HydraExternalError } HydraExternalType;
typedef struct {
    HydraExternalType type;
    uint32_t protocol;
    uint64_t bytes;
    uint32_t files;
    uint32_t matched;
    uint32_t changed;
    uint32_t missing;
    uint32_t new_files;
    int32_t exit_code;
    char state[16];
    char version[32];
    char target[64];
    char error[64];
} HydraExternalMessage;
typedef struct { char line[HYDRA_EXTERNAL_LINE_MAX]; size_t length; bool overflow; } HydraExternalDecoder;
typedef void (*HydraExternalCallback)(const HydraExternalMessage*, void*);

void hydra_external_decoder_reset(HydraExternalDecoder* decoder);
bool hydra_external_decoder_feed(HydraExternalDecoder* decoder, const uint8_t* data, size_t length, HydraExternalCallback callback, void* context);
bool hydra_external_parse_line(const char* line, HydraExternalMessage* message);
