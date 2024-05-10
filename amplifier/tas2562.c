/*
 * Copyright (C) 2015 The CyanogenMod Open Source Project
 * Copyright (C) 2024 The LineageOS Project
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

#define LOG_TAG "amplifier_tas2562"
#define LOG_NDEBUG 0

#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>

#include <hardware/audio_amplifier.h>
#include <hardware/hardware.h>

#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"

#define TAS2562_ALGO_PROFILE "TAS25XX_ALGO_PROFILE"
#define TAS2562_SMARTPA_ENABLE "TAS25XX_SMARTPA_ENABLE"

typedef enum tas2562_profile {
    PROFILE_NONE = -1,
    PROFILE_MUSIC = 0,
    PROFILE_RING,
    PROFILE_VOICE,
    PROFILE_MAX = PROFILE_VOICE,
} tas2562_profile_t;

#define TAS2562_PROFILE(x) [PROFILE_##x] = #x
static const char* tas2562_profile_names[] = {
        TAS2562_PROFILE(MUSIC),
        TAS2562_PROFILE(RING),
        TAS2562_PROFILE(VOICE),
};

static const struct pcm_config tas2562_pcm_config = {
        .channels = 2,
        .rate = 48000,
        .period_size = 256,
        .period_count = 4,
        .format = PCM_FORMAT_S24_LE,
        .start_threshold = 0,
        .stop_threshold = INT_MAX,
        .avail_min = 40,
};

typedef struct tas2562_amp {
    amplifier_device_t amp_dev;
    tas2562_profile_t profile;
    struct audio_device* adev;
    struct pcm* pcm;
} tas2562_amp_t;

static int tas2562_mixer_set_enum_by_string(struct mixer* mixer, const char* name,
                                            const char* value) {
    struct mixer_ctl* ctl;
    int ret = 0;

    ctl = mixer_get_ctl_by_name(mixer, name);
    if (!ctl) {
        ALOGE("%s: Could not get mixer ctl '%s'", __func__, name);
        return -EINVAL;
    }

    ret = mixer_ctl_set_enum_by_string(ctl, value);
    if (ret < 0) {
        ALOGE("%s: Failed to set mixer ctl '%s' to enum '%s'", __func__, name, value);

        return ret;
    }

    ALOGI("%s: Set mixer ctl '%s' to enum '%s'", __func__, name, value);

    return ret;
}

static bool tas2562_is_speaker(uint32_t device) {
    bool is_speaker;
    switch (device) {
        case SND_DEVICE_OUT_SPEAKER:
        case SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET:
        case SND_DEVICE_OUT_SPEAKER_AND_BT_A2DP:
        case SND_DEVICE_OUT_SPEAKER_AND_BT_SCO:
        case SND_DEVICE_OUT_SPEAKER_AND_BT_SCO_WB:
        case SND_DEVICE_OUT_SPEAKER_AND_DISPLAY_PORT:
        case SND_DEVICE_OUT_SPEAKER_AND_HDMI:
        case SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES:
        case SND_DEVICE_OUT_SPEAKER_AND_LINE:
        case SND_DEVICE_OUT_SPEAKER_REVERSE:
        case SND_DEVICE_OUT_VOICE_SPEAKER:
        case SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_ANC_HEADSET:
        case SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_HEADPHONES:
        case SND_DEVICE_OUT_VOICE_SPEAKER_2:
            is_speaker = true;
            break;
        default:
            is_speaker = false;
            break;
    }
    return is_speaker;
}

static int tas2562_set_mode(amplifier_device_t* device, audio_mode_t mode) {
    tas2562_amp_t* tas2562 = (tas2562_amp_t*)device;

    if (!tas2562) {
        ALOGE("%s: Invalid params", __func__);
        return -EINVAL;
    }

    switch (mode) {
        case AUDIO_MODE_NORMAL:
            tas2562->profile = PROFILE_MUSIC;
            break;
        case AUDIO_MODE_RINGTONE:
            tas2562->profile = PROFILE_RING;
            break;
        case AUDIO_MODE_IN_CALL:
        case AUDIO_MODE_IN_COMMUNICATION:
            tas2562->profile = PROFILE_VOICE;
            break;
        default:
            break;
    }

    ALOGI("%s: Setting profile to %s", __func__, tas2562_profile_names[tas2562->profile]);

    return 0;
}

static int tas2562_start_feedback(tas2562_amp_t* tas2562, uint32_t device) {
    struct pcm_config pcm_config = tas2562_pcm_config;
    struct audio_device* adev = tas2562->adev;
    struct mixer* mixer = adev->mixer;
    struct audio_usecase* usecase;
    const char* profile;
    struct pcm* pcm;
    int pcm_id, rc = 0;

    if (!tas2562_is_speaker(device)) return 0;

    if (tas2562->pcm) {
        ALOGE("%s: Invalid state", __func__);
        return -EINVAL;
    }

    usecase = calloc(1, sizeof(*usecase));
    if (!usecase) {
        ALOGE("%s: Failed to allocate memory for usecase", __func__);
        return -ENOMEM;
    }

    usecase->id = USECASE_AUDIO_SPKR_CALIB_TX;
    usecase->type = PCM_CAPTURE;
    usecase->in_snd_device = SND_DEVICE_IN_CAPTURE_VI_FEEDBACK;
    usecase->out_snd_device = SND_DEVICE_NONE;
    list_init(&usecase->device_list);
    list_add_head(&adev->usecase_list, &usecase->list);

    enable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);
    enable_audio_route(adev, usecase);

    profile = tas2562_profile_names[tas2562->profile];
    ALOGI("%s: Using profile %s", __func__, profile);
    tas2562_mixer_set_enum_by_string(mixer, TAS2562_ALGO_PROFILE, profile);

    tas2562_mixer_set_enum_by_string(mixer, TAS2562_SMARTPA_ENABLE, "ENABLE");

    pcm_id = platform_get_pcm_device_id(usecase->id, PCM_CAPTURE);
    if (pcm_id < 0) {
        ALOGE("%s: Invalid PCM device for usecase %d", __func__, usecase->id);
        rc = -ENODEV;
        goto err_no_pcm;
    }

    pcm = pcm_open(adev->snd_card, pcm_id, PCM_IN, &pcm_config);
    if (!pcm || !pcm_is_ready(pcm)) {
        ALOGE("%s: Failed to open PCM device: %s", __func__, pcm_get_error(pcm));
        rc = -EIO;
        goto err_no_pcm;
    }

    rc = pcm_start(pcm);
    if (rc < 0) {
        ALOGE("%s: Failed to start PCM: %s", __func__, pcm_get_error(pcm));
        goto err_pcm_start;
    }

    tas2562->pcm = pcm;

    ALOGI("%s: Feedback enabled successfully", __func__);

    return 0;

err_pcm_start:
    if (pcm) pcm_close(pcm);
err_no_pcm:
    tas2562_mixer_set_enum_by_string(mixer, TAS2562_SMARTPA_ENABLE, "DISABLE");
    disable_audio_route(adev, usecase);
    disable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);
    list_remove(&usecase->list);
    free(usecase);

    return rc;
}

static int tas2562_stop_feedback(tas2562_amp_t* tas2562, uint32_t device) {
    struct audio_device* adev = tas2562->adev;
    struct mixer* mixer = adev->mixer;
    struct audio_usecase* usecase;

    if (!tas2562_is_speaker(device)) return 0;

    if (!tas2562->pcm) {
        ALOGI("%s: Invalid state", __func__);
        return -EINVAL;
    }

    pcm_close(tas2562->pcm);
    tas2562->pcm = NULL;

    tas2562_mixer_set_enum_by_string(mixer, TAS2562_SMARTPA_ENABLE, "DISABLE");

    disable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);

    usecase = get_usecase_from_list(adev, USECASE_AUDIO_SPKR_CALIB_TX);
    if (usecase) {
        disable_audio_route(adev, usecase);
        list_remove(&usecase->list);
        free(usecase);
    }

    return 0;
}

static int tas2562_set_feedback(struct amplifier_device* device, void* adev, uint32_t devices,
                                bool enable) {
    tas2562_amp_t* tas2562 = (tas2562_amp_t*)device;

    if (!adev || !tas2562) {
        ALOGE("%s: Invalid parameters", __func__);
        return -EINVAL;
    }

    tas2562->adev = adev;

    if (enable)
        return tas2562_start_feedback(tas2562, devices);
    else
        return tas2562_stop_feedback(tas2562, devices);
}

static int tas2562_dev_close(hw_device_t* device) {
    if (device) free(device);

    return 0;
}

static int tas2562_module_open(const hw_module_t* module, const char* name, hw_device_t** device) {
    tas2562_amp_t* tas2562;

    if (strcmp(name, AMPLIFIER_HARDWARE_INTERFACE)) {
        ALOGE("%s:%d: %s does not match amplifier hardware interface name\n", __func__, __LINE__,
              name);
        return -ENODEV;
    }

    tas2562 = calloc(1, sizeof(*tas2562));
    if (!tas2562) {
        ALOGE("%s:%d: Unable to allocate memory for amplifier device\n", __func__, __LINE__);
        return -ENOMEM;
    }

    tas2562->amp_dev.common.tag = HARDWARE_DEVICE_TAG;
    tas2562->amp_dev.common.module = (hw_module_t*)module;
    tas2562->amp_dev.common.version = HARDWARE_DEVICE_API_VERSION(1, 0);
    tas2562->amp_dev.common.close = tas2562_dev_close;

    tas2562->amp_dev.set_mode = tas2562_set_mode;
    tas2562->amp_dev.set_feedback = tas2562_set_feedback;

    tas2562->profile = PROFILE_MUSIC;

    *device = (hw_device_t*)tas2562;

    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
        .open = tas2562_module_open,
};

/* clang-format off */
amplifier_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AMPLIFIER_DEVICE_API_VERSION_CURRENT,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AMPLIFIER_HARDWARE_MODULE_ID,
        .name = "TAS2562 audio amplifier HAL",
        .author = "Ivan Vecera <ivan@cera.cz>",
        .methods = &hal_module_methods,
    },
};
