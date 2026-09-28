/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "hydra_engine.h"

bool hydra_candidate_equal(const char* candidate, size_t candidate_length, const char* secret, size_t secret_length) {
    if(!candidate || !secret || candidate_length != secret_length || !candidate_length) return false;
    uint8_t difference = 0U;
    for(size_t i = 0; i < candidate_length; i++) difference |= (uint8_t)candidate[i] ^ (uint8_t)secret[i];
    return difference == 0U;
}

bool hydra_pin_candidate(uint64_t index, uint8_t digits, char* output, size_t capacity) {
    if(!output || !digits || digits > 8U || capacity <= digits) return false;
    uint64_t combinations = 1U;
    for(uint8_t i = 0; i < digits; i++) combinations *= 10U;
    if(index >= combinations) return false;
    output[digits] = '\0';
    for(uint8_t i = 0; i < digits; i++) {
        output[digits - i - 1U] = (char)('0' + index % 10U);
        index /= 10U;
    }
    return true;
}

uint32_t hydra_delay_for_attempt(const HydraLabConfig* config, const HydraLabState* state) {
    if(!config || !state) return 0U;
    if(config->protection == HydraProtectRate) return config->rate_delay_ms;
    if(config->protection == HydraProtectLockout && config->lockout_threshold &&
       state->consecutive_failures && state->consecutive_failures % config->lockout_threshold == 0U)
        return config->lockout_ms;
    if(config->protection == HydraProtectDelay) {
        uint64_t delay = config->base_delay_ms;
        uint32_t shifts = state->consecutive_failures > 20U ? 20U : state->consecutive_failures;
        while(shifts--) {
            delay *= 2U;
            if(delay >= config->max_delay_ms) return config->max_delay_ms;
        }
        return (uint32_t)delay;
    }
    return 0U;
}

bool hydra_config_valid(const HydraLabConfig* config) {
    if(!config || !config->attempt_limit || config->attempt_limit > 100000000U) return false;
    if(config->protection > HydraProtectDelay) return false;
    if(config->protection == HydraProtectRate && config->rate_delay_ms > 60000U) return false;
    if(config->protection == HydraProtectLockout && (!config->lockout_threshold || config->lockout_threshold > 1000U || config->lockout_ms > 3600000U)) return false;
    if(config->protection == HydraProtectDelay && (!config->base_delay_ms || config->base_delay_ms > config->max_delay_ms || config->max_delay_ms > 3600000U)) return false;
    return true;
}
