/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "hydra_external.h"
#include "hydra_external_protocol.h"
#include <expansion/expansion.h>
#include <furi.h>
#include <furi_hal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { HdFlagStop = 1U << 0, HdFlagRx = 1U << 1, HdFlagError = 1U << 2 };
struct HydraExternal {
    FuriMutex* mutex; FuriMutex* tx_mutex; FuriStreamBuffer* rx; FuriThread* worker;
    bool started; FuriHalSerialHandle* serial; Expansion* expansion;
    HydraExternalDecoder decoder; HydraExternalSnapshot snapshot;
    uint32_t last_message_tick;
};

#define HYDRA_COMPANION_TIMEOUT_MS 5000U

static bool hd_send(HydraExternal* external, const char* text) {
    if(!external->serial) return false;
    furi_mutex_acquire(external->tx_mutex, FuriWaitForever);
    furi_hal_serial_tx(external->serial, (const uint8_t*)text, strlen(text));
    furi_hal_serial_tx_wait_complete(external->serial);
    furi_mutex_release(external->tx_mutex);
    return true;
}

static void hd_message(const HydraExternalMessage* message, void* context) {
    HydraExternal* external = context;
    furi_mutex_acquire(external->mutex, FuriWaitForever);
    external->last_message_tick = furi_get_tick();
    HydraExternalSnapshot* s = &external->snapshot;
    if(message->type == HydraExternalInfo) {
        s->protocol = message->protocol;
        s->connected = message->protocol == HYDRA_EXTERNAL_PROTOCOL_VERSION;
        snprintf(s->version, sizeof(s->version), "%s", message->version);
        if(!s->connected) snprintf(s->error, sizeof(s->error), "Protocol mismatch"); else s->error[0] = '\0';
    } else if(message->type == HydraExternalStatus) {
        snprintf(s->state, sizeof(s->state), "%s", message->state);
        snprintf(s->target, sizeof(s->target), "%s", message->target);
        s->files = message->files; s->bytes = message->bytes; s->matched = message->matched;
        s->changed = message->changed; s->missing = message->missing;
        s->new_files = message->new_files; s->exit_code = message->exit_code;
        s->running = !strcmp(s->state, "STARTING") || !strcmp(s->state, "RUNNING") || !strcmp(s->state, "STOPPING");
    } else if(message->type == HydraExternalError) {
        snprintf(s->error, sizeof(s->error), "%s", message->error); s->running = false;
    }
    furi_mutex_release(external->mutex);
}

static void hd_irq(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    HydraExternal* external = context; uint32_t flags = 0U;
    if(event & FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        flags |= furi_stream_buffer_send(external->rx, &byte, 1U, 0U) == 1U ? HdFlagRx : HdFlagError;
    }
    if(event & (FuriHalSerialRxEventFrameError | FuriHalSerialRxEventNoiseError | FuriHalSerialRxEventOverrunError | FuriHalSerialRxEventParityError)) flags |= HdFlagError;
    if(flags && external->worker) furi_thread_flags_set(furi_thread_get_id(external->worker), flags);
}

static int32_t hd_worker(void* context) {
    HydraExternal* external = context;
    while(true) {
        uint32_t flags = furi_thread_flags_wait(HdFlagStop | HdFlagRx | HdFlagError, FuriFlagWaitAny, 1000U);
        if(flags & FuriFlagError) {
            if(flags == (uint32_t)FuriFlagErrorTimeout) {
                bool connected;
                furi_mutex_acquire(external->mutex, FuriWaitForever);
                uint32_t elapsed = furi_get_tick() - external->last_message_tick;
                uint32_t frequency = furi_kernel_get_tick_frequency();
                connected = external->snapshot.connected;
                if(connected && frequency && elapsed > (HYDRA_COMPANION_TIMEOUT_MS * frequency) / 1000U) {
                    external->snapshot.connected = false;
                    external->snapshot.running = false;
                    snprintf(external->snapshot.state, sizeof(external->snapshot.state), "TIMEOUT");
                    snprintf(external->snapshot.error, sizeof(external->snapshot.error), "Companion timeout");
                    connected = false;
                }
                furi_mutex_release(external->mutex);
                hd_send(external, connected ? "HYD1 STATUS\n" : "HYD1 HELLO\n");
            }
            continue;
        }
        if(flags & HdFlagStop) break;
        if(flags & HdFlagError) { furi_mutex_acquire(external->mutex, FuriWaitForever); external->snapshot.serial_errors++; snprintf(external->snapshot.error, sizeof(external->snapshot.error), "UART receive error"); furi_mutex_release(external->mutex); }
        if(flags & HdFlagRx) {
            uint8_t data[64]; size_t count; bool rejected = false;
            do {
                count = furi_stream_buffer_receive(external->rx, data, sizeof(data), 0U);
                if(count) rejected |= hydra_external_decoder_feed(&external->decoder, data, count, hd_message, external);
            } while(count);
            if(rejected) {
                furi_mutex_acquire(external->mutex, FuriWaitForever);
                external->snapshot.serial_errors++;
                snprintf(external->snapshot.error, sizeof(external->snapshot.error), "Protocol line rejected");
                furi_mutex_release(external->mutex);
            }
        }
    }
    return 0;
}

HydraExternal* hydra_external_alloc(void) {
    HydraExternal* external = calloc(1U, sizeof(*external)); if(!external) return NULL;
    external->mutex = furi_mutex_alloc(FuriMutexTypeNormal); external->tx_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!external->mutex || !external->tx_mutex) { if(external->mutex) furi_mutex_free(external->mutex); if(external->tx_mutex) furi_mutex_free(external->tx_mutex); free(external); return NULL; }
    snprintf(external->snapshot.state, sizeof(external->snapshot.state), "DISCONNECTED"); return external;
}

bool hydra_external_start(HydraExternal* external, uint32_t baudrate) {
    if(!external || external->serial || baudrate < 9600U) return false;
    memset(&external->snapshot, 0, sizeof(external->snapshot)); external->snapshot.active = true;
    external->last_message_tick = furi_get_tick();
    snprintf(external->snapshot.state, sizeof(external->snapshot.state), "CONNECTING");
    hydra_external_decoder_reset(&external->decoder);
    external->rx = furi_stream_buffer_alloc(1024U, 1U); external->worker = furi_thread_alloc_ex("HydraUART", 2048U, hd_worker, external);
    if(!external->rx || !external->worker) { hydra_external_stop(external); return false; }
    external->expansion = furi_record_open(RECORD_EXPANSION); expansion_disable(external->expansion);
    external->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!external->serial) { hydra_external_stop(external); return false; }
    furi_hal_serial_init(external->serial, baudrate);
    furi_thread_start(external->worker);
    external->started = true;
    furi_hal_serial_async_rx_start(external->serial, hd_irq, external, false);
    if(!hd_send(external, "HYD1 HELLO\n")) {
        hydra_external_stop(external);
        return false;
    }
    return true;
}

void hydra_external_stop(HydraExternal* external) {
    if(!external) return;
    if(external->serial) furi_hal_serial_async_rx_stop(external->serial);
    if(external->started && external->worker) { furi_thread_flags_set(furi_thread_get_id(external->worker), HdFlagStop); furi_thread_join(external->worker); external->started = false; }
    if(external->serial) { furi_hal_serial_deinit(external->serial); furi_hal_serial_control_release(external->serial); external->serial = NULL; }
    if(external->worker) { furi_thread_free(external->worker); external->worker = NULL; }
    if(external->rx) { furi_stream_buffer_free(external->rx); external->rx = NULL; }
    if(external->expansion) { expansion_enable(external->expansion); furi_record_close(RECORD_EXPANSION); external->expansion = NULL; }
    furi_mutex_acquire(external->mutex, FuriWaitForever); external->snapshot.active = false; external->snapshot.connected = false; external->snapshot.running = false; snprintf(external->snapshot.state, sizeof(external->snapshot.state), "DISCONNECTED"); furi_mutex_release(external->mutex);
}

bool hydra_external_run(HydraExternal* external, const char* operation) {
    if(!external || !operation || strcmp(operation, "LAB")) return false;
    char command[40]; snprintf(command, sizeof(command), "HYD1 RUN %s\n", operation); return hd_send(external, command);
}
bool hydra_external_cancel(HydraExternal* external) { return external && hd_send(external, "HYD1 CANCEL\n"); }
void hydra_external_snapshot(HydraExternal* external, HydraExternalSnapshot* snapshot) { furi_mutex_acquire(external->mutex, FuriWaitForever); *snapshot = external->snapshot; furi_mutex_release(external->mutex); }
void hydra_external_free(HydraExternal* external) { if(!external) return; hydra_external_stop(external); furi_mutex_free(external->tx_mutex); furi_mutex_free(external->mutex); free(external); }
