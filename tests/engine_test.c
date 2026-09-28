/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "../hydra_engine.h"
#include <assert.h>
#include <string.h>

int main(void) {
    char pin[9];
    assert(hydra_pin_candidate(0U, 4U, pin, sizeof(pin)) && !strcmp(pin, "0000"));
    assert(hydra_pin_candidate(42U, 4U, pin, sizeof(pin)) && !strcmp(pin, "0042"));
    assert(hydra_pin_candidate(9999U, 4U, pin, sizeof(pin)) && !strcmp(pin, "9999"));
    assert(!hydra_pin_candidate(10000U, 4U, pin, sizeof(pin)));
    assert(!hydra_pin_candidate(0U, 0U, pin, sizeof(pin)));
    assert(!hydra_pin_candidate(0U, 9U, pin, sizeof(pin)));
    assert(hydra_candidate_equal("0042", 4U, "0042", 4U));
    assert(!hydra_candidate_equal("0043", 4U, "0042", 4U));
    assert(!hydra_candidate_equal("", 0U, "", 0U));

    HydraLabConfig cfg = {.protection = HydraProtectNone, .attempt_limit = 1000U};
    HydraLabState state = {0};
    assert(hydra_config_valid(&cfg));
    assert(hydra_delay_for_attempt(&cfg, &state) == 0U);
    cfg.protection = HydraProtectRate; cfg.rate_delay_ms = 125U;
    assert(hydra_config_valid(&cfg));
    assert(hydra_delay_for_attempt(&cfg, &state) == 125U);
    cfg.protection = HydraProtectLockout; cfg.lockout_threshold = 3U; cfg.lockout_ms = 900U;
    state.consecutive_failures = 2U; assert(hydra_delay_for_attempt(&cfg, &state) == 0U);
    state.consecutive_failures = 3U; assert(hydra_delay_for_attempt(&cfg, &state) == 900U);
    cfg.protection = HydraProtectDelay; cfg.base_delay_ms = 10U; cfg.max_delay_ms = 1000U;
    state.consecutive_failures = 0U; assert(hydra_delay_for_attempt(&cfg, &state) == 10U);
    state.consecutive_failures = 3U; assert(hydra_delay_for_attempt(&cfg, &state) == 80U);
    state.consecutive_failures = 30U; assert(hydra_delay_for_attempt(&cfg, &state) == 1000U);
    cfg.attempt_limit = 0U; assert(!hydra_config_valid(&cfg));
    return 0;
}
