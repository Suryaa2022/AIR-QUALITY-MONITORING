// @@@LICENSE
//
// Copyright (C) 2023, LG Electronics, All Right Reserved.
//
// No part of this source code may be communicated, distributed, reproduced
// or transmitted in any form or by any means, electronic or mechanical or
// otherwise, for any purpose, without the prior written permission of
// LG Electronics.
//
// LICENSE@@@

#include <chrono>

#include "player/audio_controller.h"
#include "logger/player_logger.h"

namespace genivimedia {

AudioController::AudioController() :
  media_type_(),
  volume_type_info_(),
  hw_volume_type_info_(),
  duration_(-1) {
    LOG_INFO("");
}

AudioController::~AudioController() {
    LOG_INFO("");
}

void AudioController::setMediaType(const std::string& type) {
    LOG_INFO("type=[%s]", type.c_str());
    media_type_ = type;
}

void AudioController::fadeIn(int duration_ms) {
    LOG_INFO("##### Fade In ######, time_ms=[%d]", duration_ms);
    if ((Conf::GetFeatures(SUPPORT_HARDWAREVOL)) &&
        ( (media_type_.compare("audio") == 0) || (media_type_.compare("video") == 0) ||
          (media_type_.compare("genie") == 0) || (media_type_.compare("melon") == 0) ||
          (media_type_.compare("qq_music") == 0) || (media_type_.compare("kaola_fm") == 0) ||
          (media_type_.compare("podbbang") == 0) || (media_type_.compare("vibe") == 0) ||
          (media_type_.compare("tencent_funaudio") == 0) ||
          (media_type_.compare("tencent_mini_app_video") == 0) ||
          (media_type_.compare("tencent_mini_app_audio") == 0) ||
          (media_type_.compare("welaaa_audio_streaming") == 0) ||
          (media_type_.compare("genesis_audio_streaming") == 0))) {
        if (hw_volume_type_info_.size() > 0) {
            std::string temp = hw_volume_type_info_ + " Duration";
            int32_t time = 0;
            if (duration_ms > 100) {
                usleep(50*1000);
            } else {
                usleep(25*1000);
            }
            time = 96 * duration_ms;
            (void)setHardwareVolume(temp, std::to_string(time));
            fadeInWithHardwareVol(hw_volume_type_info_);
        } else {
            LOG_INFO("hw_volume_type_info_ is empty");
            if (volume_type_info_.size() > 0) {
                fadeInWithSoftVol(volume_type_info_);
            } else {
                LOG_INFO("volume_type_info_ is empty");
            }
        }
    } else {
        if (volume_type_info_.size() > 0) {
          fadeInWithSoftVol(volume_type_info_);
        } else {
          LOG_INFO("volume_type_info_ is empty");
        }
    }
}

void AudioController::fadeOut(int duration_ms) {
    LOG_INFO("##### Fade Out ######, ms=[%d]", duration_ms);
    if ((Conf::GetFeatures(SUPPORT_HARDWAREVOL)) &&
        ( (media_type_.compare("audio") == 0) || (media_type_.compare("video") == 0) ||
          (media_type_.compare("genie") == 0) || (media_type_.compare("melon") == 0) ||
          (media_type_.compare("qq_music") == 0) || (media_type_.compare("kaola_fm") == 0) ||
          (media_type_.compare("podbbang") == 0) || (media_type_.compare("vibe") == 0) ||
          (media_type_.compare("tencent_funaudio") == 0) ||
          (media_type_.compare("tencent_mini_app_video") == 0) ||
          (media_type_.compare("tencent_mini_app_audio") == 0) ||
          (media_type_.compare("tencent_mini_app_audio") == 0) ||
          (media_type_.compare("welaaa_audio_streaming") == 0) ||
          (media_type_.compare("genesis_audio_streaming") == 0))) {
        if (hw_volume_type_info_.size() > 0) {
            std::string temp = hw_volume_type_info_ + " Duration";
            int32_t time = 96 * duration_ms;
            (void)setHardwareVolume(temp, std::to_string(time));
            fadeOutWithHardwareVol(hw_volume_type_info_);
            if (duration_ms > 100) {
                usleep(150*1000);
            } else {
                usleep(100*1000);
            }
        } else {
            LOG_INFO("hw_volume_type_info_ is empty");
            if (volume_type_info_.size() > 0) {
                fadeInWithSoftVol(volume_type_info_);
            } else {
                LOG_INFO("volume_type_info_ is empty");
            }
        }
    } else {
        if (volume_type_info_.size() > 0) {
            fadeOutWithSoftVol(volume_type_info_, duration_ms);
        } else {
            LOG_INFO("volume_type_info_ is empty");
        }
    }
}

gint64 AudioController::getAudioDuration(const std::string& url) {
    if (duration_ <= 0) {
      AVCodecID codec_id = AVCodecID::AV_CODEC_ID_NONE;
      int channels =  extractAudioChannel(url, &codec_id);
      LOG_INFO("Extract audio info : channel [%d], duration [%lld]", channels, duration_);
    }
    return duration_;
}

int AudioController::getAudioChannel(const std::string& url, char slot, int channel, bool* convert, AVCodecID* codec_id, double* exec_time) {
  LOG_INFO("Get actual channel info.. type=[%s], requested_ch=[%d]", media_type_.c_str(), channel);
  int ret = channel;
  bool is_6ch_2nd = *convert;

  if (media_type_.compare("mood_therapy_audio") == 0) {
    LOG_INFO("mood therapy audio file");
    if (channel == 2) {
      LOG_INFO("multi-channel slot is already acquired.. use 2ch instead");
      ret = 2;
    } else {
      ret = 6;
    }
  } else if (media_type_.compare("mood_therapy_video") == 0) {
    LOG_INFO("mood therapy video file (no audio file)");
    ret = 0;
  } else if (media_type_.compare("recording_play") == 0) {
    LOG_INFO("driving video file (no audio file)");
    ret = 0;
  } else if (media_type_.compare("dvrs_rear") == 0) {
    LOG_INFO("dvrs_rear cam video file (no audio)");
    ret = 0;
  } else if (Conf::GetFeatures(SUPPORT_MULTI_CH)) { // Multi channel handle
    if (ret == 0 && ((media_type_.compare("audio")==0) || (media_type_.compare("audio_2nd")==0) ||
                     (media_type_.compare("video")==0) || (media_type_.compare("video_2nd")==0) ||
                     (media_type_.compare("dvrs_front")==0) ||
                     (media_type_.compare("nature_sound")==0) ||
                     (media_type_.compare("manual_video")==0))) {
      // USB_VIDEO/USB_Audio (AC3/DTS) should be checked when supporting multi channels
      std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();
      ret = extractAudioChannel(url, codec_id);
      std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1);
      *exec_time = time_span.count() * 1000L; // ms

      if (*codec_id == AVCodecID::AV_CODEC_ID_AC3 ||
          *codec_id == AVCodecID::AV_CODEC_ID_EAC3 ||
          *codec_id == AVCodecID::AV_CODEC_ID_DTS) {
        *convert = false;
      } else {
        *convert = true;
      }
    }
  }

  if ((ret >= 6) && channel == 2) {
    LOG_INFO("5.1ch slot is unavailable. Need downmix");
    ret = 2;
  }

  if ((ret >= 6) && (Conf::GetFeatures(SUPPORT_MULTI_CH))) {
    volume_type_info_ = Conf::GetVolumeType(VOLUME_5_1);
    if (is_6ch_2nd) { // ccRC case - volume name is '6channel_vol2'
        volume_type_info_.append("2");
        LOG_INFO("2nd slot is used.. vol=[%s]", volume_type_info_.c_str());
    }
    hw_volume_type_info_ = ""; // We do not control 5.1 Channel Hardware volume
  } else {
    volume_type_info_ = Conf::GetVolumeType(media_type_.c_str());

      if (Conf::GetFeatures(SUPPORT_HARDWAREVOL)) {
          int32_t hardware_slot = slot + Conf::GetSpec(HARDWARE_DEFAULT_SLOT);
          hw_volume_type_info_ = Conf::GetVolumeType(VOLUME_HARDWARE);
          hw_volume_type_info_.append(1, hardware_slot);
      }
  }
  return ret;
}

static int avformatInterruptCb(void *arg)
{
    std::chrono::high_resolution_clock::time_point* start_time = (std::chrono::high_resolution_clock::time_point*)arg;
    std::chrono::high_resolution_clock::time_point cur_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(cur_time - *start_time);

    double val = time_span.count() * 1000.0; // ms
    // Adding 1 sec timeout to avoid infinite blocking
    if (val > 1000.0) {
        LOG_INFO("Exiting Interrupt_Cb : %lf msec", val);
        return 1;
    } else {
        // Continue execution
        return 0;
    }
}

int AudioController::extractAudioChannel(const std::string& url, AVCodecID* c_id) {
    LOG_INFO("extractAudioChannel called..");
    AVFormatContext *fmt_ctx = NULL;
    AVCodecID av_codec_id = AVCodecID::AV_CODEC_ID_NONE;

    int channel = 0;

    fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
        LOG_ERROR("Could not allocate context");
        return 0;
    }
    // Capture start time for avformat_open_input
    std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();

    fmt_ctx->interrupt_callback.callback = &avformatInterruptCb;
    fmt_ctx->interrupt_callback.opaque = &start_time;

    int ret = avformat_open_input(&fmt_ctx, url.c_str(), NULL, NULL);
    if (ret != 0) {
        char buffer[64];
        av_strerror(ret, buffer, sizeof(buffer));
        LOG_INFO("Cannot open file for channel config, use default, error[%s]", buffer);
        channel = 2;
    } else {
        LOG_INFO("avformat_find_stream_info was called...");
        if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
            LOG_INFO("find stream info fail, use default");
        } else {
            int audio_stream_idx = 0;
            if (fmt_ctx->streams != NULL) {
                LOG_INFO("av_find_best_stream was called...");
                audio_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);

                if (audio_stream_idx > -1 && fmt_ctx->streams[audio_stream_idx] != NULL) {
                    #ifdef FFMPEGFOUR
                    if (fmt_ctx->streams[audio_stream_idx]->codec != NULL) {
                        channel = fmt_ctx->streams[audio_stream_idx]->codec->channels;
                        av_codec_id = fmt_ctx->streams[audio_stream_idx]->codec->codec_id;
                    #else
                    if (fmt_ctx->streams[audio_stream_idx]->codecpar != NULL) {
                        channel = fmt_ctx->streams[audio_stream_idx]->codecpar->channels;
                        av_codec_id = fmt_ctx->streams[audio_stream_idx]->codecpar->codec_id;
                    #endif
                        *c_id = av_codec_id;
                        AVRational timebase = fmt_ctx->streams[audio_stream_idx]->time_base;
                        double TimeBase = av_q2d(timebase);
                        duration_ = (fmt_ctx->streams[audio_stream_idx]->duration * TimeBase);
                        if (duration_ < 0) {
                          duration_ = 0;
                        }
                        LOG_INFO("acquired channel=[%d], codec_id=[%u], duration=[%lld]", channel, av_codec_id, duration_);
                    } else {
                        LOG_ERROR("codec is null, use default");
                    }
                } else {
                    LOG_ERROR("audio_stream_idx=[%d] fail, use default", audio_stream_idx);
                }
            } else {
                LOG_ERROR("streams is null, use default");
            }
        }
    }

    if (fmt_ctx) {
        LOG_INFO("avformat_close_input was called...");
        avformat_close_input(&fmt_ctx);
        fmt_ctx = NULL;
    }
    return channel;
}

void AudioController::fadeInWithSoftVol(const std::string softvol_dev) {
    usleep(25*1000);
    int ret = setSoftVolume(softvol_dev, MAX_VOLUME);
    LOG_INFO("setSoftVolume(MAX_VOLUME), ret=[%d]", ret);
}

void AudioController::fadeOutWithSoftVol(const std::string softvol_dev, int wait_ms) {
    int ret = setSoftVolume(softvol_dev, MIN_VOLUME);
    LOG_INFO("setSoftVolume(MIN_VOLUME), ret=[%d], wait=[%d]", ret, wait_ms);
    if (wait_ms > 0) {
        usleep(wait_ms*1000);
    } else {
        LOG_WARN("wait_ms has a negative value");
    }
}

int AudioController::setSoftVolume(const std::string softvol_dev, int vol) {
    int ret;
    long min, max;

    snd_mixer_t *handle;
    snd_mixer_selem_id_t *sid;

    if ((ret = snd_mixer_open(&handle, 0)) < 0) {
        LOG_ERROR("snd_mixer_open fail(%d)", ret);
        return 0;
    }

    if ((ret = snd_mixer_attach(handle, SND_CARD)) < 0) {
        LOG_ERROR("snd_mixer_attach fail(%d)", ret);
        snd_mixer_close(handle);
        return 0;
    }

    if ((ret = snd_mixer_selem_register(handle, NULL, NULL)) < 0) {
        LOG_ERROR("snd_mixer_selem_register fail(%d)", ret);
        snd_mixer_close(handle);
        return 0;
    }

    if ((ret = snd_mixer_load(handle)) < 0) {
        LOG_ERROR("snd_mixer_load fail(%d)", ret);
        snd_mixer_close(handle);
        return 0;
    }

    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, softvol_dev.c_str());

    snd_mixer_elem_t* elem = snd_mixer_find_selem(handle, sid);
    if (elem == NULL) {
        LOG_ERROR("snd_mixer_find_selem NULL");
        snd_mixer_close(handle);
        return 0;
    }
    snd_mixer_selem_get_playback_volume_range(elem, &min, &max);

    ret = snd_mixer_selem_set_playback_volume_all(elem, vol);
    LOG_INFO("snd_mixer_selem_set_playback_volume_all (%d), (%d)", ret, vol);

    snd_mixer_close(handle);
    return 0;
}

void AudioController::fadeInWithHardwareVol(const std::string hw_dev) {
    std::string temp = hw_dev + " Gain";
    std::string gain = "65536";

    int ret = setHardwareVolume(temp, gain);
    LOG_INFO("setHardwareVolume(Dev=[%s] , Gain=[%s], ret=[%d]", hw_dev.c_str(), gain.c_str(), ret);
}

void AudioController::fadeOutWithHardwareVol(const std::string hw_dev) {
    std::string temp = hw_dev + " Gain";
    std::string gain = "0";

    int ret = setHardwareVolume(temp, gain);
    LOG_INFO("setHardwareVolume(Dev=[%s] , Gain=[%s], ret=[%d]", hw_dev.c_str(), gain.c_str(), ret);
}

int AudioController::setHardwareVolume(const std::string hw_dev, const std::string value) {
    int32_t err = 0;

    snd_ctl_t* handle = NULL;
    snd_ctl_elem_info_t* info = NULL;
    snd_ctl_elem_id_t* id = NULL;
    snd_ctl_elem_value_t* control = NULL;

    snd_ctl_elem_info_alloca(&info);
    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_value_alloca(&control);

    if ((err = snd_ctl_open(&handle, SND_CARD, 0)) < 0) {
        LOG_ERROR("snd_ctl_open failed, err=%d", err);
        return err;
    }

    if ((err = snd_ctl_ascii_elem_id_parse(id, hw_dev.c_str())) < 0) {
        LOG_ERROR("snd_ctl_ctl_ascii_elem_id_parse failed, err=%d", err);
        goto EXIT;
    }

    snd_ctl_elem_info_set_id(info, id);
    if ((err = snd_ctl_elem_info(handle, info)) < 0) {
        LOG_ERROR("snd_ctt_elem_info failed, err=%d", err);
        goto EXIT;
    }

	snd_ctl_elem_value_set_id(control, id);
    if ((err = snd_ctl_elem_read(handle, control)) < 0) {
        LOG_ERROR("snd_ctt_elem_read failed, err=%d", err);
        goto EXIT;
    }

    if ((err = snd_ctl_ascii_value_parse(handle, control, info, value.c_str())) < 0) {
        LOG_ERROR("snd_ctl_ascii_value_parse failed, err=%d", err);
        goto EXIT;
    }

    if ((err = snd_ctl_elem_write(handle, control)) < 0) {
        LOG_ERROR("snd_ctl_elem_write, err=%d", err);
        goto EXIT;
    }

EXIT:
    snd_ctl_close(handle);
    return err;
}

int AudioController::setHardwareMixer(const std::string dev, const std::string value) {
  int32_t err = 0;
  snd_ctl_t* handle = NULL;
  snd_ctl_elem_info_t* info = NULL;
  snd_ctl_elem_id_t* id = NULL;
  snd_ctl_elem_value_t* control = NULL;

  snd_ctl_elem_info_alloca(&info);
  snd_ctl_elem_id_alloca(&id);
  snd_ctl_elem_value_alloca(&control);

  if ((err = snd_ctl_ascii_elem_id_parse(id, dev.c_str())) < 0) {
    LOG_ERROR("[MIXER] snd_ctl_ascii_elem_id_parse failed, error=%d", err);
    return err;
  }

  if(handle == NULL &&
      (err = snd_ctl_open(&handle, SND_CARD, 0)) < 0) {
    LOG_ERROR("[MIXER] snd_ctl_open failed, error=%d", err);
    return err;
  }

  snd_ctl_elem_info_set_id(info, id);
  if ((err = snd_ctl_elem_info(handle, info)) < 0) {
    LOG_ERROR("[MIXER] snd_ctl_elem_info failed, error=%d", err);
    goto EXIT;
  }

  snd_ctl_elem_info_get_id(info, id);
  snd_ctl_elem_value_set_id(control, id);

  if ((err = snd_ctl_elem_read(handle, control)) < 0) {
    LOG_ERROR("[MIXER] snd_ctl_elem_read failed, error=%d", err);
    goto EXIT;
  }

  if ((err = snd_ctl_ascii_value_parse(handle, control, info, value.c_str())) < 0) {
    LOG_ERROR("[MIXER] snd_ctl_ascii_value_parse failed, err=%d", err);
    goto EXIT;
  }

  if ((err = snd_ctl_elem_write(handle, control)) < 0) {
    LOG_ERROR("[MIXER] snd_ctl_elem_write, err=%d", err);
    goto EXIT;
  }

EXIT:
  snd_ctl_close(handle);
  handle = NULL;
  return err;
}

}  // namespace genivimedia

