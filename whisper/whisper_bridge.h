#ifndef WHISPER_BRIDGE_H
#define WHISPER_BRIDGE_H

#include <stdint.h>

// Opaque state wrapping a whisper_context and its full_params.
// Avoids passing large structs through CGO.
typedef struct whisper_state whisper_state_t;

whisper_state_t *whisper_state_create(const char *model_path);
void             whisper_state_destroy(whisper_state_t *s);

int          whisper_state_process(whisper_state_t *s, const float *samples, int n_samples);
int          whisper_state_n_segments(whisper_state_t *s);
const char  *whisper_state_segment_text(whisper_state_t *s, int i);
int64_t      whisper_state_segment_t0(whisper_state_t *s, int i);
int64_t      whisper_state_segment_t1(whisper_state_t *s, int i);

void whisper_state_set_language(whisper_state_t *s, const char *lang);
void whisper_state_set_translate(whisper_state_t *s, int translate);
void whisper_state_set_n_threads(whisper_state_t *s, int n);

#endif
