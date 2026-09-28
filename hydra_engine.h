/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HYDRA_CANDIDATE_MAX 64U
#define HYDRA_SECRET_MAX 32U

typedef enum { HydraProtectNone, HydraProtectRate, HydraProtectLockout, HydraProtectDelay } HydraProtection;
typedef struct {
    HydraProtection protection;
    uint32_t attempt_limit;
    uint32_t rate_delay_ms;
    uint32_t lockout_threshold;
    uint32_t lockout_ms;
    uint32_t base_delay_ms;
    uint32_t max_delay_ms;
} HydraLabConfig;
typedef struct {
    uint64_t candidate_index;
    uint64_t attempts;
    uint64_t output_bytes;
    uint32_t consecutive_failures;
    uint32_t current_delay_ms;
    uint32_t lockouts;
    bool success;
    bool exhausted;
    bool cancelled;
    char matched[HYDRA_CANDIDATE_MAX + 1U];
} HydraLabState;

bool hydra_candidate_equal(const char* candidate, size_t candidate_length, const char* secret, size_t secret_length);
bool hydra_pin_candidate(uint64_t index, uint8_t digits, char* output, size_t capacity);
uint32_t hydra_delay_for_attempt(const HydraLabConfig* config, const HydraLabState* state);
bool hydra_config_valid(const HydraLabConfig* config);

