/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

/*
 * Wake word (CONFIG_MUSE_WAKE_WORD): Espressif's WakeNet listens to the idle
 * microphone for the word in the "model" partition (esp-sr's srmodels.bin,
 * e.g. CONFIG_SR_WN_WN10_HIMUSE for "Hi/Hey Muse"). The voice task feeds it
 * the 20 ms chunks it already reads for the pre-roll.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Loads the model. False if there is none (the board then works by button only). */
bool muse_wake_init(void);

/* Feeds 16 kHz mono PCM; true when the wake word has just been heard. */
bool muse_wake_feed(const int16_t *pcm, size_t frames);

/* Forgets buffered audio, e.g. after Muse played something. */
void muse_wake_reset(void);

#ifdef __cplusplus
}
#endif
