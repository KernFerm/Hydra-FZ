/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct HydraExternal HydraExternal;
typedef struct {
    bool active, connected, running;
    uint32_t protocol, serial_errors, files, matched, changed, missing, new_files;
    uint64_t bytes;
    int32_t exit_code;
    char state[16], version[32], target[64], error[64];
} HydraExternalSnapshot;

HydraExternal* hydra_external_alloc(void);
void hydra_external_free(HydraExternal* external);
bool hydra_external_start(HydraExternal* external, uint32_t baudrate);
void hydra_external_stop(HydraExternal* external);
bool hydra_external_run(HydraExternal* external, const char* operation);
bool hydra_external_cancel(HydraExternal* external);
void hydra_external_snapshot(HydraExternal* external, HydraExternalSnapshot* snapshot);
