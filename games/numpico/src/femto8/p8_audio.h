/*
 * p8_audio.h
 *
 *  Created on: Dec 13, 2023
 *      Author: bbaker
 *
 * Changed for NumPico: the calculator has no speaker, so sound and music do nothing and nothing
 * is ever playing.
 */

#include <stdint.h>

#ifndef P8_AUDIO_H
#define P8_AUDIO_H

static inline void audio_sound(int32_t index, int32_t channel, uint32_t start, uint32_t end) {}
static inline void audio_music(int32_t index, int32_t fade_ms, int32_t mask) {}
static inline void audio_stop(void) {}
/* stat(16..26) and stat(46..57): no sound, no music */
static inline int32_t audio_stat(int32_t index) { return index == 57 ? 0 : -1; }

#endif
