#include "whisper_bridge.h"
#include "whisper.h"
#include <stdlib.h>
#include <stdbool.h>

struct whisper_state {
    struct whisper_context    *ctx;
    struct whisper_full_params params;
};

whisper_state_t *whisper_state_create(const char *model_path) {
    whisper_state_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    struct whisper_context_params cparams = whisper_context_default_params();
    s->ctx = whisper_init_from_file_with_params(model_path, cparams);
    if (!s->ctx) {
        free(s);
        return NULL;
    }

    s->params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    s->params.print_progress   = false;
    s->params.print_timestamps = false;
    s->params.print_special    = false;
    s->params.single_segment   = false;
    s->params.no_context       = true;
    s->params.language         = "en";
    return s;
}

void whisper_state_destroy(whisper_state_t *s) {
    if (!s) return;
    if (s->ctx) whisper_free(s->ctx);
    free(s);
}

int whisper_state_process(whisper_state_t *s, const float *samples, int n_samples) {
    return whisper_full(s->ctx, s->params, samples, n_samples);
}

int whisper_state_n_segments(whisper_state_t *s) {
    return whisper_full_n_segments(s->ctx);
}

const char *whisper_state_segment_text(whisper_state_t *s, int i) {
    return whisper_full_get_segment_text(s->ctx, i);
}

int64_t whisper_state_segment_t0(whisper_state_t *s, int i) {
    return whisper_full_get_segment_t0(s->ctx, i);
}

int64_t whisper_state_segment_t1(whisper_state_t *s, int i) {
    return whisper_full_get_segment_t1(s->ctx, i);
}

void whisper_state_set_language(whisper_state_t *s, const char *lang) {
    s->params.language = lang;
}

void whisper_state_set_translate(whisper_state_t *s, int translate) {
    s->params.translate = translate;
}

void whisper_state_set_n_threads(whisper_state_t *s, int n) {
    s->params.n_threads = n;
}
