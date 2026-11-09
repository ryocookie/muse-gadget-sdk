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

#include "muse_wake.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"
#include "sdkconfig.h"

#include "muse_audio.h"

static const char *TAG = "muse_wake";

static const esp_wn_iface_t *s_wn;
static model_iface_data_t *volatile s_model;   /* set last, once the rest is ready */
static int16_t *s_buf;        /* one detect() chunk, interleaved if the model wants more channels */
static size_t s_chunk;        /* frames per detect() */
static size_t s_have;
static int s_channels;

static bool load(void)
{
    srmodel_list_t *models = esp_srmodel_init("model");
    char *name = models ? esp_srmodel_filter(models, ESP_WN_PREFIX, NULL) : NULL;
    if (!name) {
        ESP_LOGW(TAG, "no wake word model in the \"model\" partition: button only");
        return false;
    }
#if CONFIG_MUSE_WAKE_WORD_AGGRESSIVE
    const det_mode_t mode = DET_MODE_95;
#else
    const det_mode_t mode = DET_MODE_90;
#endif
    s_wn = esp_wn_handle_from_name(name);
    model_iface_data_t *model = s_wn ? s_wn->create(name, mode) : NULL;
    if (!model) {
        ESP_LOGE(TAG, "couldn't load wake word model %s", name);
        return false;
    }
    int rate = s_wn->get_samp_rate(model);
    if (rate != MUSE_AUDIO_RATE) {
        ESP_LOGE(TAG, "%s wants %d Hz audio, the mic gives %d", name, rate, MUSE_AUDIO_RATE);
        s_wn->destroy(model);
        return false;
    }
    s_chunk = (size_t)s_wn->get_samp_chunksize(model);
    s_channels = s_wn->get_channel_num(model);
    s_channels = s_channels > 0 ? s_channels : 1;
    s_buf = heap_caps_malloc(s_chunk * s_channels * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_buf) {
        s_wn->destroy(model);
        return false;
    }
    ESP_LOGI(TAG, "listening for \"%s\" (%s, %u frames x %d ch per check)", s_wn->get_word_name(model, 1), name,
             (unsigned)s_chunk, s_channels);
    __atomic_store_n(&s_model, model, __ATOMIC_RELEASE);
    return true;
}

static void load_task(void *arg)
{
    (void)arg;
    load();
    vTaskDelete(NULL);
}

void muse_wake_start(void)
{
    /* Internal stack (plain xTaskCreate): see muse_wake.h. Freed once it's done. */
    if (xTaskCreate(load_task, "muse_wake", 16 * 1024, NULL, 3, NULL) != pdPASS &&
        xTaskCreate(load_task, "muse_wake", 10 * 1024, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "no internal RAM to load the wake word model: button only");
    }
}

bool muse_wake_feed(const int16_t *pcm, size_t frames)
{
    model_iface_data_t *model = __atomic_load_n(&s_model, __ATOMIC_ACQUIRE);
    if (!model) {
        return false;
    }
    bool heard = false;
    while (frames) {
        size_t take = s_chunk - s_have < frames ? s_chunk - s_have : frames;
        for (size_t i = 0; i < take; i++) {
            for (int c = 0; c < s_channels; c++) {
                s_buf[(s_have + i) * s_channels + c] = pcm[i];   /* one mic: the same audio on every channel */
            }
        }
        s_have += take;
        pcm += take;
        frames -= take;
        if (s_have == s_chunk) {
            s_have = 0;
            if (s_wn->detect(model, s_buf) > 0) {
                heard = true;
            }
        }
    }
    return heard;
}

void muse_wake_reset(void)
{
    model_iface_data_t *model = __atomic_load_n(&s_model, __ATOMIC_ACQUIRE);
    s_have = 0;
    if (model && s_wn->clean) {
        s_wn->clean(model);
    }
}
