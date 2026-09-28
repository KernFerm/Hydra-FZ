/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "hydra_external_protocol.h"
#include <stdio.h>
#include <string.h>

void hydra_external_decoder_reset(HydraExternalDecoder* decoder) { memset(decoder, 0, sizeof(*decoder)); }

bool hydra_external_parse_line(const char* line, HydraExternalMessage* message) {
    memset(message, 0, sizeof(*message));
    unsigned long protocol = 0, files = 0, matched = 0, changed = 0, missing = 0, new_files = 0;
    unsigned long long bytes = 0;
    long exit_code = 0;
    int consumed = 0;
    if(sscanf(line, "HYD1 INFO %lu %31s%n", &protocol, message->version, &consumed) == 2 &&
       consumed > 0 && line[consumed] == '\0') {
        message->type = HydraExternalInfo; message->protocol = (uint32_t)protocol; return true;
    }
    consumed = 0;
    if(sscanf(line, "HYD1 STATUS %15s %lu %llu %lu %lu %lu %lu %ld %63s%n",
        message->state, &files, &bytes, &matched, &changed, &missing, &new_files,
        &exit_code, message->target, &consumed) == 9 && consumed > 0 && line[consumed] == '\0') {
        message->type = HydraExternalStatus; message->files = (uint32_t)files; message->bytes = (uint64_t)bytes;
        message->matched = (uint32_t)matched; message->changed = (uint32_t)changed;
        message->missing = (uint32_t)missing; message->new_files = (uint32_t)new_files;
        message->exit_code = (int32_t)exit_code; return true;
    }
    if(!strncmp(line, "HYD1 ERROR ", 11U)) {
        message->type = HydraExternalError; snprintf(message->error, sizeof(message->error), "%s", line + 11U); return true;
    }
    return false;
}

bool hydra_external_decoder_feed(HydraExternalDecoder* decoder, const uint8_t* data, size_t length, HydraExternalCallback callback, void* context) {
    bool rejected = false;
    for(size_t i = 0; i < length; i++) {
        uint8_t byte = data[i];
        if(byte == '\r') continue;
        if(byte == '\n') {
            if(!decoder->overflow && decoder->length) {
                decoder->line[decoder->length] = '\0';
                HydraExternalMessage message;
                if(hydra_external_parse_line(decoder->line, &message)) callback(&message, context);
                else rejected = true;
            }
            if(decoder->overflow) rejected = true;
            decoder->length = 0U; decoder->overflow = false;
        } else if(byte < 0x20U || byte > 0x7EU) {
            decoder->overflow = true;
        } else if(decoder->length + 1U < sizeof(decoder->line)) {
            decoder->line[decoder->length++] = (char)byte;
        } else decoder->overflow = true;
    }
    return rejected;
}
