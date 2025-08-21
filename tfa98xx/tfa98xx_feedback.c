/*
 * Copyright (C) 2015 The CyanogenMod Open Source Project
 * Copyright (C) 2020-2021 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_amplifier_tfa98xx"

#include <log/log.h>
#include <stdlib.h> // For calloc, free

#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"

/* clang-format off */
#define is_spkr_out_snd_dev(x) \
    (((x) == SND_DEVICE_OUT_SPEAKER) || \
    ((x) == SND_DEVICE_OUT_SPEAKER_REVERSE) || \
    ((x) == SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES) || \
    ((x) == SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET) || \
    ((x) == SND_DEVICE_OUT_SPEAKER_AND_HDMI) || \
    ((x) == SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET) || \
    ((x) == SND_DEVICE_OUT_VOICE_SPEAKER) || \
    ((x) == SND_DEVICE_OUT_VOICE_SPEAKER_2))
/* clang-format on */

typedef struct amp_device {
    amplifier_device_t amp_dev;
    struct audio_device* adev;
    /* This field is only valid during the enable/disable sequence */
    struct audio_usecase* current_usecase_tx;
    struct pcm* tfa98xx_out;
} tfa_t;

// NOTE: Global static variable 'tfa_dev' was removed to prevent race conditions
// and memory leaks. All state is now managed through the 'device' handle.

static struct pcm_config pcm_config_tfa98xx = {
    .channels = 2,
    .rate = 48000,
    .period_size = 256,
    .period_count = 4,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

static void cleanup_feedback_usecase(tfa_t* dev, struct audio_usecase* usecase) {
    if (!dev || !usecase) {
        return;
    }

    ALOGD("%s: Disabling tfa98xx feedback", __func__);
    list_remove(&usecase->list);
    disable_snd_device(dev->adev, usecase->in_snd_device);
    disable_audio_route(dev->adev, usecase);
    free(usecase);
}

static int amp_set_feedback(amplifier_device_t* device, void* adev, uint32_t snd_device, bool enable) {
    // The 'device' pointer is the handle to our state.
    tfa_t* dev = (tfa_t*)device;
    int pcm_dev_tx_id = 0, rc = 0;
    struct audio_usecase* usecase_tx = NULL;

    if (!dev || !adev) {
        ALOGE("%s: Invalid params: device=%p, adev=%p", __func__, (void*)dev, adev);
        return -EINVAL;
    }

    dev->adev = (struct audio_device*)adev;

    if (!is_spkr_out_snd_dev(snd_device)) {
        return 0;
    }

    if (!enable) {
        goto disable;
    }

    // Already enabled, do nothing.
    if (dev->tfa98xx_out) {
        return 0;
    }

    usecase_tx = (struct audio_usecase*)calloc(1, sizeof(struct audio_usecase));
    if (!usecase_tx) {
        ALOGE("%s: failed to allocate memory for usecase", __func__);
        return -ENOMEM;
    }

    usecase_tx->id = USECASE_AUDIO_SPKR_CALIB_TX;
    usecase_tx->type = PCM_CAPTURE;
    usecase_tx->in_snd_device = SND_DEVICE_IN_CAPTURE_VI_FEEDBACK;
    list_init(&usecase_tx->device_list);

    // Add to the head of the list to fix Use-After-Free on call answer.
    list_add_head(&dev->adev->usecase_list, &usecase_tx->list);
    enable_snd_device(dev->adev, usecase_tx->in_snd_device);
    enable_audio_route(dev->adev, usecase_tx);

    // Store the usecase pointer temporarily to ensure it can be cleaned up on failure.
    dev->current_usecase_tx = usecase_tx;

    pcm_dev_tx_id = platform_get_pcm_device_id(usecase_tx->id, usecase_tx->type);
    ALOGD("%s: pcm_dev_tx_id = %d", __func__, pcm_dev_tx_id);
    if (pcm_dev_tx_id < 0) {
        ALOGE("%s: Invalid pcm device for usecase (%d)", __func__, usecase_tx->id);
        rc = -ENODEV;
        goto disable;
    }

    dev->tfa98xx_out =
            pcm_open(dev->adev->snd_card, pcm_dev_tx_id, PCM_IN, &pcm_config_tfa98xx);
    // The check for non-NULL is redundant as pcm_open always returns a valid pointer.
    if (!pcm_is_ready(dev->tfa98xx_out)) {
        ALOGE("%s: %s", __func__, pcm_get_error(dev->tfa98xx_out));
        pcm_close(dev->tfa98xx_out);
        dev->tfa98xx_out = NULL;
        rc = -EIO;
        goto disable;
    }

    rc = pcm_start(dev->tfa98xx_out);
    if (rc < 0) {
        ALOGE("%s: pcm start for TX failed", __func__);
        rc = -EINVAL;
        goto disable;
    }

    ALOGD("%s: Started tfa98xx feedback successfully", __func__);
    dev->current_usecase_tx = NULL; // Clear temporary pointer on success.
    return 0;

disable:
    ALOGV("%s: Disabling usecase", __func__);
    if (dev->tfa98xx_out) {
        pcm_close(dev->tfa98xx_out);
        dev->tfa98xx_out = NULL;
    }

    // If we are disabling after a failure during enable, use the temporary pointer.
    // Otherwise, find the usecase in the list to disable it properly.
    usecase_tx = dev->current_usecase_tx;
    if (usecase_tx) {
        dev->current_usecase_tx = NULL;
    } else {
        // This assumes only one instance of this usecase ID exists.
        usecase_tx = get_usecase_from_list(dev->adev, USECASE_AUDIO_SPKR_CALIB_TX);
    }

    if (usecase_tx) {
        cleanup_feedback_usecase(dev, usecase_tx);
    }

    return rc;
}

static int amp_dev_close(hw_device_t* device) {
    tfa_t* dev = (tfa_t*)device;
    if (dev) {
        // Ensure resources are freed if the device is closed while active.
        if (dev->tfa98xx_out) {
            amp_set_feedback(&dev->amp_dev, dev->adev, SND_DEVICE_OUT_SPEAKER, false);
        }
        free(dev);
    }
    return 0;
}

static int amp_module_open(const hw_module_t* module, const char* name, hw_device_t** device) {
    tfa_t* dev;

    if (strcmp(name, AMPLIFIER_HARDWARE_INTERFACE)) {
        ALOGE("%s: %s does not match amplifier hardware interface name", __func__, name);
        return -ENODEV;
    }

    dev = calloc(1, sizeof(tfa_t));
    if (!dev) {
        ALOGE("%s: Unable to allocate memory for amplifier device", __func__);
        return -ENOMEM;
    }

    dev->amp_dev.common.tag = HARDWARE_DEVICE_TAG;
    dev->amp_dev.common.module = (hw_module_t*)module;
    dev->amp_dev.common.version = HARDWARE_DEVICE_API_VERSION(1, 0);
    dev->amp_dev.common.close = amp_dev_close;

    dev->amp_dev.set_feedback = amp_set_feedback;

    *device = (hw_device_t*)dev;

    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
        .open = amp_module_open,
};

/* clang-format off */
amplifier_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AMPLIFIER_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AMPLIFIER_HARDWARE_MODULE_ID,
        .name = "TFA98XX audio amplifier HAL",
        .author = "The LineageOS Open Source Project",
        .methods = &hal_module_methods,
    },
};
