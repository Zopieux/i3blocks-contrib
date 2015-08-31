#pragma once

#include <pulse/pulseaudio.h>

typedef pa_volume_t(*calculator_t)(const pa_cvolume *vol);

enum conn_state_t {
    CONN_WAIT, CONN_READY, CONN_FAILED
};

enum state_t {
    FIRST_SINK_INFO,SUBSCRIBE, SUBSCRIBED, WAIT_FOR_CHANGE, SHOW_CHANGE
};

typedef struct options {
    uint32_t observed_index;
    calculator_t calculator;
    int use_decibel;
} options_t;

typedef struct sink_info {
    int mute;
    int can_decibel;
    pa_cvolume volume;
} sink_info_t;

typedef struct sink_info_data {
    int sink_ready;
    int sink_changed;
    uint32_t observed_index;
    sink_info_t sink;
} sink_info_data_t;

void state_cb(pa_context *ctx, void *user_data);

void subscribe_cb(pa_context *ctx, pa_subscription_event_type_t type,
                  uint32_t index, void *user_data);

void sink_info_cb(pa_context *ctx, const pa_sink_info *info, int eol,
                  void *user_data);

void show_volume(const sink_info_t *sink_info, const options_t *options);