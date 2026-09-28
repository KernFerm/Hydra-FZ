/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "hydra_engine.h"
#include "hydra_external.h"
#include <dialogs/dialogs.h>
#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/view_dispatcher.h>
#include <storage/storage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HYDRA_VERSION "1.0.2"
#define HYDRA_REPORT APP_DATA_PATH("report.txt")
#define HYDRA_REPORT_TMP APP_DATA_PATH("report.txt.partial")
#define HYDRA_SESSION APP_DATA_PATH("session.txt")
#define HYDRA_SESSION_TMP APP_DATA_PATH("session.txt.partial")

typedef enum { HydraViewMenu, HydraViewInput, HydraViewSettings, HydraViewText, HydraViewExternal } HydraViewId;
typedef enum {
    HydraMenuSecret,
    HydraMenuFile,
    HydraMenuRun,
    HydraMenuSession,
    HydraMenuReport,
    HydraMenuExternal,
    HydraMenuSettings,
    HydraMenuAbout,
} HydraMenu;
typedef enum { HydraSourcePin, HydraSourceFile } HydraSource;

typedef struct {
    Gui* gui;
    Storage* storage;
    DialogsApp* dialogs;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    TextInput* input;
    VariableItemList* settings;
    Widget* widget;
    View* external_view;
    FuriString* text;
    FuriString* path;
    FuriThread* worker;
    FuriMutex* mutex;
    HydraExternal* external;
    VariableItem* source_item;
    VariableItem* digits_item;
    VariableItem* protection_item;
    VariableItem* limit_item;
    HydraViewId current;
    HydraViewId text_back;
    HydraSource source;
    HydraLabConfig config;
    HydraLabState state;
    uint32_t baud;
    uint32_t start_tick;
    uint32_t end_tick;
    uint32_t invalid;
    uint8_t baud_index;
    uint8_t pin_digits;
    char secret[HYDRA_SECRET_MAX + 1U];
    volatile bool cancel;
    bool worker_failed;
    bool report_saved;
    bool session_loaded;
    bool views_added;
} HydraApp;

typedef struct { HydraApp* app; uint32_t revision; } HydraExternalModel;
static const uint32_t hydra_bauds[] = {115200U, 230400U, 460800U};
static const char* const hydra_baud_names[] = {"115200", "230400", "460800"};
static const char* const hydra_protection_names[] = {"None", "Rate limit", "Lockout", "Increasing"};
static const uint32_t hydra_attempt_limits[] = {100U, 1000U, 10000U, 100000U};
static const char* const hydra_attempt_names[] = {"100", "1,000", "10,000", "100,000"};

static void hydra_switch(HydraApp* app, HydraViewId view) {
    app->current = view;
    view_dispatcher_switch_to_view(app->dispatcher, view);
}

static void hydra_show(HydraApp* app, const char* title, const char* body, HydraViewId back) {
    widget_reset(app->widget);
    furi_string_printf(app->text, "\e#%s\n%s", title, body);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, furi_string_get_cstr(app->text));
    app->text_back = back;
    hydra_switch(app, HydraViewText);
}

static bool hydra_mkdir(HydraApp* app) {
    return storage_simply_mkdir(app->storage, APP_DATA_PATH(""));
}

static bool hydra_write_all(File* file, const char* data) {
    size_t length = strlen(data);
    return storage_file_write(file, data, length) == length;
}

static bool hydra_replace(HydraApp* app, const char* temporary, const char* final) {
    char backup[128];
    int written = snprintf(backup, sizeof(backup), "%s.backup", final);
    if(written < 0 || (size_t)written >= sizeof(backup)) return false;
    bool had_final = storage_file_exists(app->storage, final);
    if(storage_file_exists(app->storage, backup) && storage_common_remove(app->storage, backup) != FSE_OK)
        return false;
    if(had_final && storage_common_rename(app->storage, final, backup) != FSE_OK) return false;
    if(storage_common_rename(app->storage, temporary, final) != FSE_OK) {
        if(had_final) storage_common_rename(app->storage, backup, final);
        return false;
    }
    if(had_final) storage_common_remove(app->storage, backup);
    return true;
}

static bool hydra_recover_backup(HydraApp* app, const char* final) {
    char backup[128];
    int written = snprintf(backup, sizeof(backup), "%s.backup", final);
    if(written < 0 || (size_t)written >= sizeof(backup)) return false;
    if(!storage_file_exists(app->storage, backup)) return true;
    if(storage_file_exists(app->storage, final)) return storage_common_remove(app->storage, backup) == FSE_OK;
    return storage_common_rename(app->storage, backup, final) == FSE_OK;
}

static bool hydra_save_session(HydraApp* app) {
    if(!hydra_mkdir(app) || !hydra_recover_backup(app, HYDRA_SESSION)) return false;
    HydraLabState state;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    state = app->state;
    furi_mutex_release(app->mutex);
    File* file = storage_file_alloc(app->storage);
    if(!file) return false;
    bool ok = storage_file_open(file, HYDRA_SESSION_TMP, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    FuriString* row = furi_string_alloc();
    if(!row) ok = false;
    if(ok) {
        furi_string_printf(
            row,
            "HYDRA_FZ_SESSION=1\nsource=%u\npin_digits=%u\nprotection=%u\nattempt_limit=%lu\n"
            "rate_delay_ms=%lu\nlockout_threshold=%lu\nlockout_ms=%lu\nbase_delay_ms=%lu\nmax_delay_ms=%lu\n"
            "candidate_index=%llu\nattempts=%llu\nfailures=%lu\nlockouts=%lu\npath=%s\n",
            (unsigned)app->source, (unsigned)app->pin_digits, (unsigned)app->config.protection,
            (unsigned long)app->config.attempt_limit, (unsigned long)app->config.rate_delay_ms,
            (unsigned long)app->config.lockout_threshold, (unsigned long)app->config.lockout_ms,
            (unsigned long)app->config.base_delay_ms, (unsigned long)app->config.max_delay_ms,
            (unsigned long long)state.candidate_index, (unsigned long long)state.attempts,
            (unsigned long)state.consecutive_failures, (unsigned long)state.lockouts,
            furi_string_get_cstr(app->path));
        ok = hydra_write_all(file, furi_string_get_cstr(row)) && storage_file_sync(file);
    }
    if(row) furi_string_free(row);
    if(storage_file_is_open(file)) storage_file_close(file);
    storage_file_free(file);
    if(ok) ok = hydra_replace(app, HYDRA_SESSION_TMP, HYDRA_SESSION);
    if(!ok) storage_common_remove(app->storage, HYDRA_SESSION_TMP);
    return ok;
}

static bool hydra_parse_u64(const char* text, uint64_t maximum, uint64_t* output) {
    if(!text || !*text || !output) return false;
    uint64_t value = 0U;
    for(const char* c = text; *c; c++) {
        if(*c < '0' || *c > '9') return false;
        uint8_t digit = (uint8_t)(*c - '0');
        if(value > (maximum - digit) / 10U) return false;
        value = value * 10U + digit;
    }
    *output = value;
    return true;
}

static bool hydra_load_session(HydraApp* app) {
    File* file = storage_file_alloc(app->storage);
    if(!file || !storage_file_open(file, HYDRA_SESSION, FSAM_READ, FSOM_OPEN_EXISTING)) {
        if(file) storage_file_free(file);
        return false;
    }
    char data[768];
    size_t got = storage_file_read(file, data, sizeof(data) - 1U);
    bool ok = storage_file_get_error(file) == FSE_OK && got && got < sizeof(data) - 1U;
    data[got] = '\0';
    storage_file_close(file);
    storage_file_free(file);
    if(!ok || strncmp(data, "HYDRA_FZ_SESSION=1\n", 19U)) return false;
    HydraLabConfig config = {0};
    HydraLabState state = {0};
    HydraSource source = HydraSourcePin;
    uint8_t digits = 4U;
    FuriString* path = furi_string_alloc();
    if(!path) return false;
    uint32_t seen = 0U;
    char* cursor = data + 19U;
    while(*cursor) {
        char* line = cursor;
        char* newline = strchr(cursor, '\n');
        if(newline) {
            *newline = '\0';
            cursor = newline + 1U;
        } else {
            cursor += strlen(cursor);
        }
        char* equals = strchr(line, '=');
        if(!equals) { ok = false; break; }
        *equals++ = '\0';
        uint64_t value = 0U;
        if(!strcmp(line, "path")) { if(strlen(equals) >= 256U) { ok = false; break; } furi_string_set(path, equals); seen |= 1UL << 13; continue; }
        if(!hydra_parse_u64(equals, UINT64_MAX, &value)) { ok = false; break; }
        if(!strcmp(line, "source") && value <= HydraSourceFile) { source = (HydraSource)value; seen |= 1UL << 0; }
        else if(!strcmp(line, "pin_digits") && value >= 1U && value <= 8U) { digits = (uint8_t)value; seen |= 1UL << 1; }
        else if(!strcmp(line, "protection") && value <= HydraProtectDelay) { config.protection = (HydraProtection)value; seen |= 1UL << 2; }
        else if(!strcmp(line, "attempt_limit") && value <= UINT32_MAX) { config.attempt_limit = (uint32_t)value; seen |= 1UL << 3; }
        else if(!strcmp(line, "rate_delay_ms") && value <= UINT32_MAX) { config.rate_delay_ms = (uint32_t)value; seen |= 1UL << 4; }
        else if(!strcmp(line, "lockout_threshold") && value <= UINT32_MAX) { config.lockout_threshold = (uint32_t)value; seen |= 1UL << 5; }
        else if(!strcmp(line, "lockout_ms") && value <= UINT32_MAX) { config.lockout_ms = (uint32_t)value; seen |= 1UL << 6; }
        else if(!strcmp(line, "base_delay_ms") && value <= UINT32_MAX) { config.base_delay_ms = (uint32_t)value; seen |= 1UL << 7; }
        else if(!strcmp(line, "max_delay_ms") && value <= UINT32_MAX) { config.max_delay_ms = (uint32_t)value; seen |= 1UL << 8; }
        else if(!strcmp(line, "candidate_index")) { state.candidate_index = value; seen |= 1UL << 9; }
        else if(!strcmp(line, "attempts")) { state.attempts = value; seen |= 1UL << 10; }
        else if(!strcmp(line, "failures") && value <= UINT32_MAX) { state.consecutive_failures = (uint32_t)value; seen |= 1UL << 11; }
        else if(!strcmp(line, "lockouts") && value <= UINT32_MAX) { state.lockouts = (uint32_t)value; seen |= 1UL << 12; }
        else { ok = false; break; }
    }
    uint8_t limit_index = 0U;
    bool known_limit = false;
    for(uint8_t i = 0U; i < COUNT_OF(hydra_attempt_limits); i++) {
        if(config.attempt_limit == hydra_attempt_limits[i]) {
            limit_index = i;
            known_limit = true;
            break;
        }
    }
    ok = ok && seen == 0x3FFFU && known_limit && hydra_config_valid(&config) &&
         state.attempts <= config.attempt_limit && state.candidate_index <= 100000000ULL &&
         state.consecutive_failures <= state.attempts && state.lockouts <= state.attempts &&
         (source != HydraSourceFile || furi_string_start_with_str(path, "/ext/"));
    if(ok) {
        app->source = source;
        app->pin_digits = digits;
        app->config = config;
        app->state = state;
        furi_string_set(app->path, path);
        app->session_loaded = true;
        variable_item_set_current_value_index(app->source_item, (uint8_t)source);
        variable_item_set_current_value_text(app->source_item, source == HydraSourcePin ? "PIN" : "File");
        variable_item_set_current_value_index(app->digits_item, digits - 1U);
        char digits_text[4];
        snprintf(digits_text, sizeof(digits_text), "%u", (unsigned)digits);
        variable_item_set_current_value_text(app->digits_item, digits_text);
        variable_item_set_current_value_index(app->protection_item, (uint8_t)config.protection);
        variable_item_set_current_value_text(app->protection_item, hydra_protection_names[config.protection]);
        variable_item_set_current_value_index(app->limit_item, limit_index);
        variable_item_set_current_value_text(app->limit_item, hydra_attempt_names[limit_index]);
    }
    furi_string_free(path);
    return ok;
}

static bool hydra_delay_cancelable(HydraApp* app, uint32_t delay_ms) {
    while(delay_ms && !app->cancel) {
        uint32_t part = delay_ms > 50U ? 50U : delay_ms;
        furi_delay_ms(part);
        delay_ms -= part;
    }
    return !app->cancel;
}

static bool hydra_attempt(HydraApp* app, const char* candidate, size_t length) {
    size_t secret_length = 0U;
    while(secret_length < sizeof(app->secret) && app->secret[secret_length]) secret_length++;
    bool success = hydra_candidate_equal(candidate, length, app->secret, secret_length);
    uint32_t delay;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->state.attempts++;
    app->state.candidate_index++;
    app->state.output_bytes += length + 1U;
    if(success) {
        app->state.success = true;
        memcpy(app->state.matched, candidate, length);
        app->state.matched[length] = '\0';
    } else {
        app->state.consecutive_failures++;
    }
    delay = success ? 0U : hydra_delay_for_attempt(&app->config, &app->state);
    app->state.current_delay_ms = delay;
    if(!success && app->config.protection == HydraProtectLockout && delay) app->state.lockouts++;
    furi_mutex_release(app->mutex);
    return success || !hydra_delay_cancelable(app, delay);
}

static bool hydra_skip_lines(HydraApp* app, File* file, uint64_t count) {
    uint64_t lines = 0U;
    uint8_t byte;
    while(lines < count && !app->cancel) {
        size_t got = storage_file_read(file, &byte, 1U);
        if(!got) return false;
        if(byte == '\n') lines++;
    }
    return !app->cancel;
}

static bool hydra_run_file(HydraApp* app) {
    File* file = storage_file_alloc(app->storage);
    if(!file || !storage_file_open(file, furi_string_get_cstr(app->path), FSAM_READ, FSOM_OPEN_EXISTING)) {
        if(file) storage_file_free(file);
        return false;
    }
    uint64_t start_index = app->state.candidate_index;
    bool ok = hydra_skip_lines(app, file, start_index);
    if(app->cancel) ok = true;
    char candidate[HYDRA_CANDIDATE_MAX + 1U];
    size_t used = 0U;
    bool overflow = false;
    while(ok && !app->cancel && !app->state.success && app->state.attempts < app->config.attempt_limit) {
        uint8_t byte;
        size_t got = storage_file_read(file, &byte, 1U);
        if(!got && storage_file_get_error(file) != FSE_OK) { ok = false; break; }
        bool finish = !got || byte == '\n';
        if(!finish) {
            if(byte != '\r') {
                if(used < HYDRA_CANDIDATE_MAX) candidate[used++] = (char)byte;
                else overflow = true;
            }
            continue;
        }
        if(overflow || !used) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(app->invalid == UINT32_MAX || app->state.candidate_index == UINT64_MAX) ok = false;
            else {
                app->invalid++;
                app->state.candidate_index++;
            }
            furi_mutex_release(app->mutex);
            if(!ok) break;
        } else {
            candidate[used] = '\0';
            if(hydra_attempt(app, candidate, used)) break;
        }
        used = 0U;
        overflow = false;
        if(!got) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->state.exhausted = true;
            furi_mutex_release(app->mutex);
            break;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

static bool hydra_run_pins(HydraApp* app) {
    char candidate[9];
    while(!app->cancel && !app->state.success && app->state.attempts < app->config.attempt_limit) {
        if(!hydra_pin_candidate(app->state.candidate_index, app->pin_digits, candidate, sizeof(candidate))) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->state.exhausted = true;
            furi_mutex_release(app->mutex);
            break;
        }
        if(hydra_attempt(app, candidate, app->pin_digits)) break;
    }
    return true;
}

static bool hydra_write_report(HydraApp* app) {
    if(!hydra_mkdir(app) || !hydra_recover_backup(app, HYDRA_REPORT)) return false;
    File* file = storage_file_alloc(app->storage);
    FuriString* report = furi_string_alloc();
    if(!file || !report) { if(file) storage_file_free(file); if(report) furi_string_free(report); return false; }
    uint32_t frequency = furi_kernel_get_tick_frequency();
    uint32_t elapsed = frequency ? (uint32_t)(((uint64_t)(app->end_tick - app->start_tick) * 1000U) / frequency) : 0U;
    HydraLabState state;
    furi_mutex_acquire(app->mutex, FuriWaitForever); state = app->state; furi_mutex_release(app->mutex);
    const char* status = app->worker_failed ? "failed" : app->cancel ? "cancelled" : state.success ? "matched" : state.exhausted ? "exhausted" : "attempt-limit";
    furi_string_printf(report,
        "Hydra FZ v%s measured offline lab report\nStatus: %s\nSource: %s\nProtection: %s\n"
        "Attempts: %llu\nCandidates read: %llu\nInvalid/overlong: %lu\nBytes considered: %llu\n"
        "Lockouts: %lu\nElapsed milliseconds: %lu\n",
        HYDRA_VERSION, status, app->source == HydraSourcePin ? "numeric PIN" : "microSD file",
        hydra_protection_names[app->config.protection], (unsigned long long)state.attempts,
        (unsigned long long)state.candidate_index, (unsigned long)app->invalid,
        (unsigned long long)state.output_bytes, (unsigned long)state.lockouts, (unsigned long)elapsed);
    if(state.success) furi_string_cat_printf(report, "Matched candidate: %s\n", state.matched);
    bool ok = storage_file_open(file, HYDRA_REPORT_TMP, FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
              hydra_write_all(file, furi_string_get_cstr(report)) && storage_file_sync(file);
    if(storage_file_is_open(file)) storage_file_close(file);
    storage_file_free(file);
    furi_string_free(report);
    if(ok) ok = hydra_replace(app, HYDRA_REPORT_TMP, HYDRA_REPORT);
    if(!ok) storage_common_remove(app->storage, HYDRA_REPORT_TMP);
    return ok;
}

static int32_t hydra_worker(void* context) {
    HydraApp* app = context;
    app->start_tick = furi_get_tick();
    app->worker_failed = false;
    app->report_saved = false;
    bool ok = app->source == HydraSourcePin ? hydra_run_pins(app) : hydra_run_file(app);
    app->worker_failed = !ok;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->state.cancelled = app->cancel;
    furi_mutex_release(app->mutex);
    app->end_tick = furi_get_tick();
    app->report_saved = hydra_write_report(app);
    if(app->cancel || app->worker_failed) hydra_save_session(app);
    app->session_loaded = false;
    view_dispatcher_send_custom_event(app->dispatcher, 1U);
    return 0;
}

static void hydra_join(HydraApp* app) {
    if(app->worker) { furi_thread_join(app->worker); furi_thread_free(app->worker); app->worker = NULL; }
}

static void hydra_secret_done(void* context) {
    HydraApp* app = context;
    app->secret[HYDRA_SECRET_MAX] = '\0';
    if(!app->secret[0]) { hydra_show(app, "Invalid secret", "Enter a local test credential. It is held in RAM only and never saved in the session.", HydraViewMenu); return; }
    if(!app->session_loaded) memset(&app->state, 0, sizeof(app->state));
    hydra_show(app, "Test secret set", "The local test credential is held in RAM only. Choose a source and select Run lab.", HydraViewMenu);
}

static void hydra_choose_file(HydraApp* app) {
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, "*", NULL);
    options.hide_ext = false;
    options.skip_assets = false;
    if(dialog_file_browser_show(app->dialogs, app->path, app->path, &options)) {
        app->source = HydraSourceFile;
        app->session_loaded = false;
        memset(&app->state, 0, sizeof(app->state));
        hydra_show(app, "Candidate file", "Selected. Each non-empty line is one real candidate; lines over 64 bytes are rejected.", HydraViewMenu);
    }
}

static void hydra_start(HydraApp* app) {
    if(!app->secret[0]) { hydra_show(app, "Secret required", "Set a local test secret first. Secrets are never loaded from or stored in sessions.", HydraViewMenu); return; }
    if(!hydra_config_valid(&app->config)) { hydra_show(app, "Invalid settings", "The lab configuration failed validation.", HydraViewMenu); return; }
    if(app->source == HydraSourceFile && !furi_string_start_with_str(app->path, "/ext/")) {
        hydra_show(app, "File required", "Choose a candidate file from microSD first.", HydraViewMenu); return;
    }
    if(!app->session_loaded) memset(&app->state, 0, sizeof(app->state));
    app->state.cancelled = app->state.exhausted = app->state.success = false;
    app->state.matched[0] = '\0';
    app->cancel = false;
    app->invalid = 0U;
    app->worker = furi_thread_alloc_ex("HydraLab", 4096U, hydra_worker, app);
    if(!app->worker) { hydra_show(app, "Allocation failed", "Could not allocate the lab worker. No operation started.", HydraViewMenu); return; }
    furi_thread_start(app->worker);
    hydra_show(app, "Offline lab running", "Testing real candidates against the RAM-only local secret.\n\nBack requests safe cancellation.", HydraViewMenu);
}

static void hydra_show_report(HydraApp* app) {
    File* file = storage_file_alloc(app->storage);
    if(!file || !storage_file_open(file, HYDRA_REPORT, FSAM_READ, FSOM_OPEN_EXISTING)) {
        if(file) storage_file_free(file);
        hydra_show(app, "Report", "No completed report is available.", HydraViewMenu);
        return;
    }
    char report[1024];
    size_t got = storage_file_read(file, report, sizeof(report) - 1U);
    bool ok = storage_file_get_error(file) == FSE_OK;
    report[got] = '\0';
    storage_file_close(file); storage_file_free(file);
    hydra_show(app, "Report", ok ? report : "Report read failed. Check the microSD card.", HydraViewMenu);
}

static void hydra_external_draw(Canvas* canvas, void* model_context) {
    HydraExternalModel* model = model_context;
    HydraExternalSnapshot state;
    char line[96];
    hydra_external_snapshot(model->app->external, &state);
    canvas_set_font(canvas, FontPrimary); canvas_draw_str(canvas, 1, 9, "External Hydra Lab");
    canvas_set_font(canvas, FontKeyboard);
    snprintf(line, sizeof(line), "%s %s", state.version[0] ? state.version : "waiting", state.state); canvas_draw_str(canvas, 1, 20, line);
    snprintf(line, sizeof(line), "Attempts %lu Found %lu", (unsigned long)state.files, (unsigned long)state.matched); canvas_draw_str(canvas, 1, 30, line);
    snprintf(line, sizeof(line), "Failures %lu Invalid %lu", (unsigned long)state.changed, (unsigned long)state.missing); canvas_draw_str(canvas, 1, 40, line);
    snprintf(line, sizeof(line), "Bytes %llu Exit %ld", (unsigned long long)state.bytes, (long)state.exit_code); canvas_draw_str(canvas, 1, 50, line);
    canvas_draw_str(canvas, 1, 60, state.error[0] ? state.error : state.target);
    canvas_draw_str(canvas, 88, 9, state.running ? "OK Stop" : "OK Run");
}

static bool hydra_external_input(InputEvent* event, void* context) {
    HydraApp* app = context;
    if(event->key != InputKeyOk || event->type != InputTypeShort) return false;
    HydraExternalSnapshot state; hydra_external_snapshot(app->external, &state);
    if(!state.connected) return false;
    return state.running ? hydra_external_cancel(app->external) : hydra_external_run(app->external, "LAB");
}

static void hydra_selected(void* context, uint32_t index) {
    HydraApp* app = context;
    if(index == HydraMenuSecret) {
        text_input_reset(app->input); text_input_set_header_text(app->input, "Local test secret");
        text_input_set_minimum_length(app->input, 1U);
        text_input_set_result_callback(app->input, hydra_secret_done, app, app->secret, sizeof(app->secret), true);
        hydra_switch(app, HydraViewInput);
    } else if(index == HydraMenuFile) hydra_choose_file(app);
    else if(index == HydraMenuRun) hydra_start(app);
    else if(index == HydraMenuSession) {
        if(hydra_load_session(app)) hydra_show(app, "Session loaded", "Progress and settings restored. Set the RAM-only test secret, then Run lab to resume.", HydraViewMenu);
        else hydra_show(app, "Session unavailable", "No valid session was found, or it could not be read safely.", HydraViewMenu);
    } else if(index == HydraMenuReport) hydra_show_report(app);
    else if(index == HydraMenuExternal) {
        if(!hydra_external_start(app->external, app->baud)) { hydra_show(app, "UART unavailable", "Close other UART apps and verify the 3.3 V wiring and baud.", HydraViewMenu); return; }
        hydra_switch(app, HydraViewExternal);
    } else if(index == HydraMenuSettings) hydra_switch(app, HydraViewSettings);
    else hydra_show(app, "About Hydra FZ",
        "Version " HYDRA_VERSION "\n\nNative mode is a real offline authentication-defense lab: it compares PIN or file candidates with a RAM-only test secret and measures rate-limit, lockout, and increasing-delay controls. It does not attack network services.\n\nOptional external mode controls genuine THC Hydra on a Raspberry Pi, Linux laptop, desktop, or VM through 3.3 V UART. The supplied bridge permits loopback lab targets only.\n\nUpstream v9.8dev commit 17b5261. GNU AGPL v3. No warranty. Use only systems you own or are authorized to test.", HydraViewMenu);
}

static void hydra_source_changed(VariableItem* item) {
    HydraApp* app = variable_item_get_context(item);
    app->source = (HydraSource)variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, app->source == HydraSourcePin ? "PIN" : "File");
    app->session_loaded = false; memset(&app->state, 0, sizeof(app->state));
}

static void hydra_digits_changed(VariableItem* item) {
    HydraApp* app = variable_item_get_context(item);
    app->pin_digits = variable_item_get_current_value_index(item) + 1U;
    char value[4]; snprintf(value, sizeof(value), "%u", (unsigned)app->pin_digits);
    variable_item_set_current_value_text(item, value);
    app->session_loaded = false; memset(&app->state, 0, sizeof(app->state));
}

static void hydra_protection_changed(VariableItem* item) {
    HydraApp* app = variable_item_get_context(item);
    app->config.protection = (HydraProtection)variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, hydra_protection_names[app->config.protection]);
    app->session_loaded = false; memset(&app->state, 0, sizeof(app->state));
}

static void hydra_limit_changed(VariableItem* item) {
    HydraApp* app = variable_item_get_context(item); uint8_t index = variable_item_get_current_value_index(item);
    app->config.attempt_limit = hydra_attempt_limits[index]; variable_item_set_current_value_text(item, hydra_attempt_names[index]);
    app->session_loaded = false; memset(&app->state, 0, sizeof(app->state));
}

static void hydra_baud_changed(VariableItem* item) {
    HydraApp* app = variable_item_get_context(item); app->baud_index = variable_item_get_current_value_index(item);
    app->baud = hydra_bauds[app->baud_index]; variable_item_set_current_value_text(item, hydra_baud_names[app->baud_index]);
}

static bool hydra_custom(void* context, uint32_t event) {
    HydraApp* app = context;
    if(event != 1U) return false;
    hydra_join(app);
    HydraLabState state; furi_mutex_acquire(app->mutex, FuriWaitForever); state = app->state; furi_mutex_release(app->mutex);
    char summary[512];
    const char* status = app->worker_failed ? "Read failed / SD removed" : app->cancel ? "Cancelled" : state.success ? "Candidate matched" : state.exhausted ? "Source exhausted" : "Attempt limit reached";
    snprintf(summary, sizeof(summary),
        "Status: %s\nAttempts: %llu\nCandidates read: %llu\nInvalid: %lu\nLockouts: %lu\nMatched: %s\n\n%s%s",
        status, (unsigned long long)state.attempts, (unsigned long long)state.candidate_index,
        (unsigned long)app->invalid, (unsigned long)state.lockouts, state.success ? state.matched : "--",
        app->report_saved ? "Report saved in app data." : "REPORT NOT SAVED: check SD card.",
        (!state.success && !state.exhausted) ? "\nA resumable session was requested." : "");
    hydra_show(app, "Lab result", summary, HydraViewMenu);
    return true;
}

static bool hydra_back(void* context) {
    HydraApp* app = context;
    if(app->worker) { app->cancel = true; return true; }
    if(app->current == HydraViewMenu) view_dispatcher_stop(app->dispatcher);
    else if(app->current == HydraViewExternal) { hydra_external_stop(app->external); hydra_switch(app, HydraViewMenu); }
    else if(app->current == HydraViewText) hydra_switch(app, app->text_back);
    else hydra_switch(app, HydraViewMenu);
    return true;
}

static void hydra_tick(void* context) {
    HydraApp* app = context;
    if(app->current == HydraViewExternal) {
        HydraExternalModel* model = view_get_model(app->external_view); model->revision++;
        view_commit_model(app->external_view, true); return;
    }
    if(!app->worker) return;
    HydraLabState state; uint32_t invalid;
    if(furi_mutex_acquire(app->mutex, 0U) != FuriStatusOk) return;
    state = app->state; invalid = app->invalid; furi_mutex_release(app->mutex);
    uint32_t frequency = furi_kernel_get_tick_frequency();
    uint32_t elapsed_ms = frequency ? (uint32_t)(((uint64_t)(furi_get_tick() - app->start_tick) * 1000U) / frequency) : 0U;
    uint64_t rate = elapsed_ms ? state.attempts * 1000U / elapsed_ms : 0U;
    furi_string_printf(app->text, "\e#Offline lab\nAttempts: %llu\nRate: %llu/s  Invalid: %lu\nDelay: %lu ms  Locks: %lu\nBack cancels safely.",
        (unsigned long long)state.attempts, (unsigned long long)rate, (unsigned long)invalid,
        (unsigned long)state.current_delay_ms, (unsigned long)state.lockouts);
    widget_reset(app->widget); widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, furi_string_get_cstr(app->text));
}

static HydraApp* hydra_alloc(void) {
    HydraApp* app = calloc(1U, sizeof(*app)); if(!app) return NULL;
    app->gui = furi_record_open(RECORD_GUI); app->storage = furi_record_open(RECORD_STORAGE); app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->dispatcher = view_dispatcher_alloc(); app->menu = submenu_alloc(); app->input = text_input_alloc();
    app->settings = variable_item_list_alloc(); app->widget = widget_alloc(); app->external_view = view_alloc();
    app->text = furi_string_alloc(); app->path = furi_string_alloc_set("/ext"); app->external = hydra_external_alloc();
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!app->gui || !app->storage || !app->dialogs || !app->dispatcher || !app->menu || !app->input || !app->settings || !app->widget || !app->external_view || !app->text || !app->path || !app->external || !app->mutex) return app;
    app->source = HydraSourcePin; app->pin_digits = 4U; app->baud = hydra_bauds[0];
    app->config = (HydraLabConfig){.protection = HydraProtectNone, .attempt_limit = 10000U, .rate_delay_ms = 100U, .lockout_threshold = 5U, .lockout_ms = 1000U, .base_delay_ms = 10U, .max_delay_ms = 2000U};
    submenu_set_header(app->menu, "Hydra FZ v" HYDRA_VERSION);
    submenu_add_item(app->menu, "Set local test secret", HydraMenuSecret, hydra_selected, app);
    submenu_add_item(app->menu, "Choose candidate file", HydraMenuFile, hydra_selected, app);
    submenu_add_item(app->menu, "Run / resume lab", HydraMenuRun, hydra_selected, app);
    submenu_add_item(app->menu, "Load saved session", HydraMenuSession, hydra_selected, app);
    submenu_add_item(app->menu, "View last report", HydraMenuReport, hydra_selected, app);
    submenu_add_item(app->menu, "External Hydra lab", HydraMenuExternal, hydra_selected, app);
    submenu_add_item(app->menu, "Settings", HydraMenuSettings, hydra_selected, app);
    submenu_add_item(app->menu, "About", HydraMenuAbout, hydra_selected, app);
    VariableItem* item = variable_item_list_add(app->settings, "Candidate source", 2U, hydra_source_changed, app);
    app->source_item = item;
    variable_item_set_current_value_index(item, 0U); variable_item_set_current_value_text(item, "PIN");
    item = variable_item_list_add(app->settings, "PIN digits", 8U, hydra_digits_changed, app);
    app->digits_item = item;
    variable_item_set_current_value_index(item, 3U); variable_item_set_current_value_text(item, "4");
    item = variable_item_list_add(app->settings, "Protection", 4U, hydra_protection_changed, app);
    app->protection_item = item;
    variable_item_set_current_value_index(item, 0U); variable_item_set_current_value_text(item, hydra_protection_names[0]);
    item = variable_item_list_add(app->settings, "Attempt limit", COUNT_OF(hydra_attempt_limits), hydra_limit_changed, app);
    app->limit_item = item;
    variable_item_set_current_value_index(item, 2U); variable_item_set_current_value_text(item, hydra_attempt_names[2]);
    item = variable_item_list_add(app->settings, "External baud", COUNT_OF(hydra_bauds), hydra_baud_changed, app);
    variable_item_set_current_value_index(item, 0U); variable_item_set_current_value_text(item, hydra_baud_names[0]);
    item = variable_item_list_add(app->settings, "Version", 1U, NULL, app); variable_item_set_current_value_text(item, HYDRA_VERSION);
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, hydra_back);
    view_dispatcher_set_custom_event_callback(app->dispatcher, hydra_custom);
    view_dispatcher_set_tick_event_callback(app->dispatcher, hydra_tick, 250U);
    view_dispatcher_add_view(app->dispatcher, HydraViewMenu, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->dispatcher, HydraViewInput, text_input_get_view(app->input));
    view_dispatcher_add_view(app->dispatcher, HydraViewSettings, variable_item_list_get_view(app->settings));
    view_dispatcher_add_view(app->dispatcher, HydraViewText, widget_get_view(app->widget)); app->views_added = true;
    view_set_context(app->external_view, app); view_set_draw_callback(app->external_view, hydra_external_draw); view_set_input_callback(app->external_view, hydra_external_input);
    view_allocate_model(app->external_view, ViewModelTypeLocking, sizeof(HydraExternalModel));
    HydraExternalModel* model = view_get_model(app->external_view); model->app = app; view_commit_model(app->external_view, false);
    view_dispatcher_add_view(app->dispatcher, HydraViewExternal, app->external_view);
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    return app;
}

static bool hydra_valid(HydraApp* app) {
    return app && app->gui && app->storage && app->dialogs && app->dispatcher && app->menu && app->input && app->settings && app->widget && app->external_view && app->text && app->path && app->external && app->mutex;
}

static void hydra_free(HydraApp* app) {
    if(!app) return;
    app->cancel = true; hydra_join(app);
    memset(app->secret, 0, sizeof(app->secret));
    if(app->external) hydra_external_free(app->external);
    if(app->dispatcher && app->views_added) {
        view_dispatcher_remove_view(app->dispatcher, HydraViewExternal); view_dispatcher_remove_view(app->dispatcher, HydraViewText);
        view_dispatcher_remove_view(app->dispatcher, HydraViewSettings); view_dispatcher_remove_view(app->dispatcher, HydraViewInput);
        view_dispatcher_remove_view(app->dispatcher, HydraViewMenu);
    }
    if(app->path) furi_string_free(app->path);
    if(app->text) furi_string_free(app->text);
    if(app->mutex) furi_mutex_free(app->mutex);
    if(app->widget) widget_free(app->widget);
    if(app->external_view) view_free(app->external_view);
    if(app->settings) variable_item_list_free(app->settings);
    if(app->input) text_input_free(app->input);
    if(app->menu) submenu_free(app->menu);
    if(app->dispatcher) view_dispatcher_free(app->dispatcher);
    if(app->dialogs) furi_record_close(RECORD_DIALOGS);
    if(app->storage) furi_record_close(RECORD_STORAGE);
    if(app->gui) furi_record_close(RECORD_GUI);
    free(app);
}

int32_t hydra_fz_app(void* p) {
    UNUSED(p);
    HydraApp* app = hydra_alloc();
    if(!hydra_valid(app)) { hydra_free(app); return -1; }
    view_dispatcher_switch_to_view(app->dispatcher, HydraViewMenu);
    view_dispatcher_run(app->dispatcher);
    hydra_free(app);
    return 0;
}
