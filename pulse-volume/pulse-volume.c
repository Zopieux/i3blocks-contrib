#include "pulse-volume.h"
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>


int main(int argc, char *const *argv) {
    static const char *context_name = "i3blocks-pulse-volume";
    static const char *usage_str = "Usage: %s [-hduji] [-s INDEX] [-m FUNC]\n"
            "Options:\n"
            "  -s INDEX: pulseaudio sink index on which to wait for "
            "changes (default: 0)\n"
            "  -m FUNC : function used to compute the displayed volume value\n"
            "            in there are multiple channels (eg. left/right):\n"
            "             * avg: use average volume of all channels (default)\n"
            "             * min: use minimum volume of all channels\n"
            "             * max: use maximum volume of all channels\n"
            "  -c COLOR: use the specified color for display when muted. Only makes\n"
	    "            sense if -j is used. Format: #RRGGBB (HTML) (default: #FF7F00)\n"
	    "  -h      : display this message\n"
            "  -d      : use decibel notation instead of 0-100 percentage; "
            "the sink may\n"
            "            not support this feature\n"
            "  -u      : include measurement units in the output (%% or dB)\n"
            "  -j      : use JSON output. When muted, output is colored. See -c\n"
            "  -i      : Prepend 'M' to the volume when muted instead of "
	    "displaying 0\n";


    options_t options;
    options.mute_color = malloc(16);
    options.calculator = pa_cvolume_avg;
    options.use_decibel = 0;
    options.observed_index = 0;
    options.display_units = 0;
    options.display_muted = 0;
    options.output_json = 0;
    strcpy(options.mute_color, "#FF7F00");

    sink_info_data_t sink_info_data;
    sink_info_data.sink_ready = 0;
    sink_info_data.sink_changed = 0;

    pa_mainloop *pa_ml = NULL;
    pa_mainloop_api *pa_ml_api = NULL;
    pa_operation *pa_op = NULL;
    pa_context *pa_ctx = NULL;

    enum state_t state = FIRST_SINK_INFO;
    int pa_ready = CONN_WAIT;

    int opt;
    while ((opt = getopt(argc, argv, "m:s:c:dhuij")) != -1) {
        if (opt == 'm') {
            if (strcmp(optarg, "min") == 0) {
                options.calculator = pa_cvolume_min;
            } else if (strcmp(optarg, "max") == 0) {
                options.calculator = pa_cvolume_max;
            } else if (strcmp(optarg, "avg") == 0) {
                options.calculator = pa_cvolume_avg;
            } else {
                fprintf(stderr, usage_str, argv[0]);
                return 1;
            }
        } else if (opt == 's') {
            // Parse observed sink index
            errno = 0;
            char *endptr;
            uint32_t *oind = &options.observed_index;
            *oind = strtoul(optarg, &endptr, 10);
            if ((errno == ERANGE) || (errno != 0 && *oind == 0) ||
                endptr == optarg) {
                fprintf(stderr, "%s: invalid sink index: %s\n", argv[0],
                        optarg);
                fprintf(stderr, usage_str, argv[0]);
                return 1;
            }
	} else if (opt == 'c') {
	    strncpy(options.mute_color, optarg, 8);
        } else if (opt == 'd') {
            options.use_decibel = 1;
        } else if (opt == 'u') {
            options.display_units = 1;
        } else if (opt == 'i') {
            options.display_muted = 1;
        } else if (opt == 'j') {
            options.output_json = 1;
        } else if (opt == 'h') {
            fprintf(stderr, usage_str, argv[0]);
            return 0;
        } else {
            fprintf(stderr, usage_str, argv[0]);
            return 1;
        }
    }

    // Needed to filter out sink in callbacks
    sink_info_data.observed_index = options.observed_index;

    if ((pa_ml = pa_mainloop_new()) == NULL) {
        fprintf(stderr, "error: failed to allocate pulseaudio mainloop.\n");
        return 2;
    }
    if ((pa_ml_api = pa_mainloop_get_api(pa_ml)) == NULL) {
        fprintf(stderr, "error: failed to allocate pulseaudio mainloop API.\n");
        return 3;
    }
    if ((pa_ctx = pa_context_new(pa_ml_api, context_name)) == NULL) {
        fprintf(stderr, "error: failed to allocate pulseaudio context.\n");
        return 4;
    }

    if (pa_context_connect(pa_ctx, NULL, PA_CONTEXT_NOFAIL, NULL) < 0) {
        fprintf(stderr, "error: failed to connect to pulseaudio context: %s\n",
                pa_strerror(pa_context_errno(pa_ctx)));
        return 5;
    }

    pa_context_set_state_callback(pa_ctx, state_cb, &pa_ready);
    pa_context_set_subscribe_callback(pa_ctx, subscribe_cb, &sink_info_data);

    while (1) {
        if (pa_ready == CONN_WAIT) {
            pa_mainloop_iterate(pa_ml, 1, NULL);
            continue;
        }

        if (pa_ready == CONN_FAILED) {
            pa_context_disconnect(pa_ctx);
            pa_context_unref(pa_ctx);
            pa_mainloop_free(pa_ml);
            fprintf(stderr,
                    "error: failed to connect to pulseaudio context.\n");
            return 6;
        }

        // Main loop automaton
        switch (state) {
            case FIRST_SINK_INFO:
                // First run
                pa_op = pa_context_get_sink_info_by_index(pa_ctx,
                            options.observed_index,
                            sink_info_cb, &sink_info_data);
                state = SUBSCRIBE;
                break;
            case SUBSCRIBE:
                if (pa_operation_get_state(pa_op) == PA_OPERATION_DONE) {
                    pa_operation_unref(pa_op);
                    if (!sink_info_data.sink_ready) {
                        fprintf(stderr, "error: sink %u does not exist.\n",
                                options.observed_index);
                        pa_context_disconnect(pa_ctx);
                        pa_context_unref(pa_ctx);
                        pa_mainloop_free(pa_ml);
                        return 7;
                    }
                    if (options.use_decibel &&
                        !sink_info_data.sink.can_decibel) {
                        fprintf(stderr,
                                "error: sink %u does not support decibel; "
                                "try without `-d`.\n",
                                options.observed_index);
                        pa_context_disconnect(pa_ctx);
                        pa_context_unref(pa_ctx);
                        pa_mainloop_free(pa_ml);
                        return 8;
                    }
                    // Show volume once at start
                    show_volume(&sink_info_data.sink, &options);
                    // Subsequent runs: wait for changes
                    pa_op = pa_context_subscribe(pa_ctx,
                                                 PA_SUBSCRIPTION_MASK_SINK,
                                                 NULL, &sink_info_data);
                    state = SUBSCRIBED;
                }
                break;
            case SUBSCRIBED:
                if (pa_operation_get_state(pa_op) == PA_OPERATION_DONE) {
                    pa_operation_unref(pa_op);
                    state = WAIT_FOR_CHANGE;
                }
                break;
            case WAIT_FOR_CHANGE:
                if (sink_info_data.sink_changed) {
                    sink_info_data.sink_changed = 0;
                    pa_op = pa_context_get_sink_info_by_index(pa_ctx,
                                options.observed_index, sink_info_cb,
                                &sink_info_data);
                    state = SHOW_CHANGE;
                }
                break;
            case SHOW_CHANGE:
                if (pa_operation_get_state(pa_op) == PA_OPERATION_DONE) {
                    pa_operation_unref(pa_op);
                    show_volume(&sink_info_data.sink, &options);
                    state = WAIT_FOR_CHANGE;
                }
                break;
            default:
                return 7;
        }
        pa_mainloop_iterate(pa_ml, 1, NULL);
    }
    free(options.mute_color);
}

/**
 * Callback for context state.
 * Sets *user_data to one of conn_state_t.
 */
void state_cb(pa_context *ctx, void *user_data) {
    pa_context_state_t state;
    enum conn_state_t *pa_ready = user_data;

    state = pa_context_get_state(ctx);
    switch (state) {
        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
        default:
            *pa_ready = CONN_WAIT;
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            *pa_ready = CONN_FAILED;
            break;
        case PA_CONTEXT_READY:
            *pa_ready = CONN_READY;
            break;
    }
}

/**
 * Callback for sink info.
 * Fill *user_data with data about the observed sink.
 */
void sink_info_cb(pa_context *ctx, const pa_sink_info *info, int eol,
                  void *user_data) {
    sink_info_data_t *sink_info_data = user_data;

    if (eol)
        return;

    if (info->index != sink_info_data->observed_index)
        return;

    sink_info_data->sink_ready = 1;
    sink_info_data->sink.can_decibel = !!(info->flags &&
                                          PA_SINK_DECIBEL_VOLUME);
    sink_info_data->sink.mute = info->mute;
    sink_info_data->sink.volume = info->volume;
}

/**
 * Callback for subscribe.
 * Set the sink_changed flag to true if the observed sink changed.
 */
void subscribe_cb(pa_context *ctx, pa_subscription_event_type_t type,
                  uint32_t index, void *user_data) {
    sink_info_data_t *sink_info_data = user_data;

    pa_subscription_event_type_t type_mask =
            type & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
    pa_subscription_event_type_t facility_mask =
            type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;

    if (type_mask == PA_SUBSCRIPTION_EVENT_CHANGE
        && facility_mask == PA_SUBSCRIPTION_EVENT_SINK &&
        index == sink_info_data->observed_index) {
        sink_info_data->sink_changed = 1;
    }
}

/*
 * Outputs the volume to stdout.
 */
void show_volume(const sink_info_t *sink_info, const options_t *options) {
    pa_volume_t volume;
    double v;
    char num[32], txt[48];
    int flag = 1;
    txt[0] = 0;

    if (sink_info->mute) {
        if (options->display_muted) {
            strcpy(txt, "M ");
            flag = 1;
        } else {
            volume = flag = 0;
        }
    }
    if (flag) volume = options->calculator(&sink_info->volume);

    v = (volume * 100) / PA_VOLUME_NORM;
    if (options->use_decibel) {
        double dB = pa_sw_volume_to_dB(volume);
        if (dB > PA_DECIBEL_MININFTY)
            snprintf(num, sizeof(txt), "%0.2f", dB);
        else
            snprintf(num, sizeof(txt), "-inf");
    } else {
        snprintf(num, sizeof(txt), "%0.0f", v);
    }
    strncat(txt, num, sizeof(num));
    if (options->display_units) {
        strcat(txt, options->use_decibel ? " dB" : "%");
    }


    if (options->output_json) {
        if (sink_info->mute) {
            printf("{\"full_text\":\"%s\",\"color\":\"%s\"}\n", txt, options->mute_color);
        } else {
            printf("{\"full_text\":\"%s\"}\n", txt);
        }
    } else {
        printf("%s\n", txt);
    }
    fflush(stdout);
}
