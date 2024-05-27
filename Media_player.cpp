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

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <glib.h>
#include <gst/gst.h>
#include <map>
#include <unistd.h>

#include "logger/player_logger.h"
#include "player/pipeline/conf.h"
#include "player/creator.h"
#include "player/pipeline/info.h"
#include "player/pipeline/common.h"
#include "player/pipeline/keep_alive.h"
#include "player/pipeline/pipeline.h"

#include "player/media_player.h"

namespace genivimedia {

static std::map<std::string, int> kAspectRatio = {
  {"FitToScreen", AR_FIT_TO_SCREEN},
  {"21:9",        AR_21_9},
  {"16:9",        AR_16_9},
  {"4:3",         AR_4_3}
};

static std::map<std::string, int> kMediaType = {
  {"audio",              TYPE_AUDIO},
  {"video",              TYPE_VIDEO},
  {"audio_2nd",          TYPE_AUDIO},
  {"video_2nd",          TYPE_VIDEO},
  {"nature_sound",       TYPE_AUDIO},
  {"manual_video",       TYPE_VIDEO},
  {"kaola_fm",           TYPE_3RD_AUDIO},
  {"melon",              TYPE_3RD_AUDIO},
  {"qq_music",           TYPE_3RD_AUDIO},
  {"kakao_i",            TYPE_STREAMING},
  {"kakao_i2",           TYPE_STREAMING},
  {"kakao_i3",           TYPE_STREAMING},
  {"genie",              TYPE_3RD_AUDIO},
  {"ximalaya",           TYPE_3RD_AUDIO},
  {"transcode",          TYPE_TRANSCODE},
  {"golf_video",         TYPE_STREAMING},
  {"mood_therapy_audio", TYPE_AUDIO},
  {"mood_therapy_video", TYPE_VIDEO},
  {"kids_video",         TYPE_VIDEO},
  {"recording_play",     TYPE_VIDEO},
  {"podbbang",           TYPE_STREAMING},
  {"dvrs_front",         TYPE_DVRS},
  {"dvrs_rear",          TYPE_DVRS},
  {"face_detection",     TYPE_AUDIO},
  {"vibe",               TYPE_STREAMING},
  {"tencent_funaudio",      TYPE_STREAMING},
  {"tencent_mini_app_audio",TYPE_STREAMING},
  {"tencent_mini_app_video",TYPE_STREAMING},
  {"welaaa_audio_streaming",TYPE_STREAMING},
  {"genesis_audio_streaming",TYPE_3RD_AUDIO}
};

MediaPlayer::MediaPlayer()
  : surface_info_(),
    media_type_str_(),
    pipeline_(),
    creator_(std::make_shared<PipelineCreator>()),
    audio_controller_(std::make_shared<AudioController>()),
    start_timer_(new Timer()),
    callback_(),
    need_fade_out_(true),
    need_fade_in_(false),
    media_init_flag_(true),
    aspect_ratio_(AR_FIT_TO_SCREEN),
    media_type_(TYPE_UNSUPPORTED) {

  LOG_INFO("");
  CreatePipeline(TYPE_UNSUPPORTED);

  std::string def_path = "/usr/bin";
  std::string conf_file;
  char* conf_path = getenv("PLAYER_ENGINE_CONF_PATH");
  if (!conf_path) {
    conf_file.append(def_path);
  } else {
    conf_file.append(conf_path);
  }
  conf_file.append("/playerengine.conf");
  Conf::ParseFile(conf_file);

  KeepAlive::Instance();

  LOG_INFO("Created MediaPlayer");
}

MediaPlayer::~MediaPlayer() {
  LOG_INFO("");
  if (start_timer_)
    delete start_timer_;
  KeepAlive::Exit();
}

void MediaPlayer::MediaPlayerInit() {
  if(media_init_flag_){

    GError* error = nullptr;
    if (!gst_init_check(nullptr, nullptr, &error)) {
      LOG_ERROR("gst_init_check error-%s", error->message);
      g_error_free(error);
    }
    LOG_INFO("gst_init_check success");

    // Enable Gstreaemr Log
    //gst_debug_set_default_threshold(GST_LEVEL_WARNING);
    const gchar* gst_debug = Conf::GetGstDebug();
    if (gst_debug != nullptr) {
      gst_debug_set_threshold_from_string(gst_debug, true);
      gst_debug_add_log_function(MediaPlayer::PrintGstLog,nullptr,nullptr);
    }
    LOG_INFO("Set gst_debug successfully");

    Conf::LoadSink();
    Conf::LoadRank();

    if (Conf::GetFeatures(SUPPORT_DOLBY_ATMOS)) {
      bool is_dlbdec_exist = true;
      (void)Conf::GetRank("dlbdec",is_dlbdec_exist);
      if(is_dlbdec_exist) {
      //If rank of ac3parse or acac3decoder is maximum of uint, it should be adjusted
        Conf::SetRank("ac3parse", 0);
        Conf::SetRank("ocac3decoder", 0);
      } else {
        Conf::SetRank("dlbparse", 0);
      }
    } else {
      Conf::SetRank("dlbparse", 0);
    }

    media_init_flag_ = false;
  }
}

void MediaPlayer::PrintGstLog(GstDebugCategory* category, GstDebugLevel level,
                                const char* file, const char* function,
                                gint line, GObject* object, GstDebugMessage *message,
                                gpointer pointer) {
    LOG_INFO("[%s:%d  %s] %s", file, line, function, gst_debug_message_get(message));
}

bool MediaPlayer::RegisterCallback(std::function <void (const std::string& data)> callback) {
  callback_ = callback;

  if (pipeline_) {
    pipeline_->RegisterCallback(callback_);
  } else {
    LOG_ERROR("Failed to register callback to pipeline");
    return false;
  }
  return true;
}

bool MediaPlayer::QuitPlayerEngine() {
  return ReleasePipeline();
}

bool MediaPlayer::Play() {
  bool ret = false;
  MediaPlayerInit();

  if (media_type_ == TYPE_3RD_AUDIO) {
    if (need_fade_out_) {
      LOG_INFO("### AVOID POP NOISE ###");
      fadeOut();
      need_fade_out_ = false;
    }
    ret = pipeline_->Play();

    if ( (Conf::GetFeatures(SUPPORT_HARDWAREVOL)) &&
        ((media_type_str_.compare("genie") == 0) || (media_type_str_.compare("melon") == 0) ||
         (media_type_str_.compare("qq_music") == 0) || (media_type_str_.compare("kaola_fm") == 0) ||
         (media_type_str_.compare("podbbang") == 0) || (media_type_str_.compare("vibe") == 0) ||
         (media_type_str_.compare("tencent_funaudio") == 0) ||
         (media_type_str_.compare("tencent_mini_app_audio") == 0) ||
         (media_type_str_.compare("welaaa_audio_streaming") == 0) ||
         (media_type_str_.compare("genesis_audio_streaming") == 0))) {
      if (need_fade_in_ == true) {
        fadeIn(2000); // 2000ms
        need_fade_in_ = false;
      } else {
        fadeIn(); // 100ms
      }
    } else {
      fadeIn();
    }

    return ret;
  }

  ret = pipeline_->Play();
  if ( (Conf::GetFeatures(SUPPORT_HARDWAREVOL)) &&
        ((media_type_str_.compare("audio") == 0) || (media_type_str_.compare("video") == 0)) ){
    if (need_fade_in_ == true) {
      fadeIn(800); // 800ms
      need_fade_in_ = false;
    } else {
      fadeIn(); // 100ms
    }
  } else {
    fadeIn(); // 100ms

  }
  return ret;
}

bool MediaPlayer::Pause() {
  MediaPlayerInit();
  fadeOut(80);
  bool ret = pipeline_->Pause();
  usleep(20 * 1000);
  return ret;
}

bool MediaPlayer::Stop() {
  MediaPlayerInit();
  start_timer_->Stop();

  if ((media_type_str_.compare("audio") == 0) || (media_type_str_.compare("video") == 0)
   || (media_type_str_.compare("audio_2nd") == 0) || (media_type_str_.compare("video_2nd") == 0)
     ) {
    fadeOut(80);
  } else if ( (media_type_str_.compare("melon") == 0) || (media_type_str_.compare("genie") == 0) ||
              (media_type_str_.compare("qq_music") == 0) || (media_type_str_.compare("kaola_fm") == 0) ) {
    fadeOut(280);
  } else {
    // none
  }

  bool ret = pipeline_->Unload(TRUE, TRUE);
  usleep(20 * 1000);
  CreatePipeline(TYPE_UNSUPPORTED);
  pipeline_->RegisterCallback(callback_);
  LOG_INFO("Stop() called, ret=[%d]", (int)ret);
  return ret;
}

bool MediaPlayer::FastForward(double step) {
  MediaPlayerInit();
  return pipeline_->FastForward((gdouble)step);
}

bool MediaPlayer::Rewind(double step) {
  MediaPlayerInit();
  return pipeline_->Rewind((gdouble)step);
}

bool MediaPlayer::SetURI(const std::string& uri, int media_type) {
  MediaPlayerInit();
  SetURIInternal(uri, media_type);
  return pipeline_->Load(uri);
}

bool MediaPlayer::SetURI(const std::string& uri, const std::string& option) {
  MediaPlayerInit();
  using boost::property_tree::ptree;

  bool need_convert = false; // need audioconvert or ccRC 5.1ch 2nd slot
  bool duration_check = false;
  char slot_6ch = '0';
  char slot = '0';
  int channel = 0;
  int media_type = TYPE_UNSUPPORTED;
  double exec_time = 0;
  AVCodecID codec_id = AVCodecID::AV_CODEC_ID_NONE;

  ptree tree;
  std::string media_type_str;
  std::stringstream stream(option);
  try {
    boost::property_tree::read_json(stream, tree);
  } catch (const boost::property_tree::ptree_error& exception) {
    LOG_INFO("Invalid JSON Format - %s", exception.what());
    return false;
  }

  auto iter = tree.begin();
  ptree info = iter->second;
  if (info.get_optional<std::string>("mediatype")) {
    media_type_str = info.get<std::string>("mediatype");
    auto iteration = kMediaType.find(media_type_str);
    if (iteration != kMediaType.end()) {
      media_type = iteration->second;
      media_type_str_ = media_type_str;
      if (audio_controller_)
        audio_controller_->setMediaType(media_type_str);
    } else {
      LOG_WARN("unknown media type=[%s]", media_type_str.c_str());
    }
  } else {
    LOG_WARN("there is no media type..");
  }

  if (info.get_optional<std::string>("channel")) {
    channel = std::stoi(info.get<std::string>("channel"));
    LOG_INFO("#### Received channel = [%d]", channel);
  }
  if (info.get_optional<char>("2ch_slot")) {
    slot = info.get<char>("2ch_slot");
    LOG_INFO("#### Received 2ch_slot = [%c]", slot);
  }
  if (info.get_optional<char>("6ch_slot")) {
    slot_6ch = info.get<char>("6ch_slot");
    LOG_INFO("#### Received 6ch_slot = [%c]", slot_6ch);
    need_convert = (slot_6ch == '1') ? true : false;
  }

  // KAKAO_I
  if ((media_type_str.compare("kakao_i") == 0) || (media_type_str.compare("kakao_i2") == 0)
       || (media_type_str.compare("kakao_i3") == 0)) {
    if (std::string::npos != uri.find("file://")) {
      LOG_INFO("kakao_i plays local file");
      media_type = TYPE_AUDIO;
    } else if (std::string::npos != uri.find(".m3u8")) {
      LOG_INFO("kakao_i plays HLS contents");
      media_type = TYPE_3RD_AUDIO;
    }
  // Kidscare
  } else if (media_type_str.compare("kids_video") == 0) {
    if (std::string::npos != uri.find(".mp3")) {
      LOG_INFO("kids_video, but plays audio format file");
      media_type = TYPE_AUDIO;
    } else if (std::string::npos != uri.find(".m3u")) {
      LOG_INFO("kids_video, but plays audio playlist file");
      media_type = TYPE_AUDIO;
    }
  // DVRS front&rear
  } else if (media_type == TYPE_DVRS) {
    LOG_INFO("dvrs media, use static pipeline - but need to check audio track");
    duration_check = false;
    channel = 0;
    if (audio_controller_) {
      channel = audio_controller_->getAudioChannel(uri, slot, channel, &need_convert, &codec_id, &exec_time);
    }
  // Other media
  } else {
    if (std::string::npos != uri.find("file://")) {
      duration_check = true;
    }
    if (audio_controller_) {
      channel = audio_controller_->getAudioChannel(uri, slot, channel, &need_convert, &codec_id, &exec_time);
    }
  }

  if (!need_fade_out_) {
    fadeOut((int)(100L - exec_time));
  } else {
    fadeOut(100L);
  }
  need_fade_out_ = false;
  need_fade_in_ = true;

  LOG_INFO("media type[%d][%s], channel=[%d], slot=[%c]", media_type, media_type_str_.c_str(), channel, slot);
  SetURIInternal(uri, media_type);

  if (audio_controller_ && duration_check) {
      pipeline_->SetAudioDuration(audio_controller_->getAudioDuration(uri));
  }

  if ((media_type_str.compare("kakao_i") != 0) && (media_type_str.compare("kakao_i2") != 0)
      && (media_type_str.compare("kakao_i3") != 0)) { // kakao_i doesn't have next/prev usecase
    TimerCallback start_callback = std::bind(&MediaPlayer::updateTimerFlag, this);
    start_timer_->AddCallback(start_callback, 1000);
    start_timer_->Start();
  }

  return pipeline_->Load(uri, option, channel, need_convert, slot);
}

bool MediaPlayer::SetPosition(gint64 position) {
  MediaPlayerInit();
  bool ret = false;

  fadeOut();
  ret = pipeline_->Seek(position);
  fadeIn();

  if (media_type_ == TYPE_3RD_AUDIO && position == 0LL) {
    need_fade_out_ = true;
  }
  return ret;
}

bool MediaPlayer::StopRateChange() {
  MediaPlayerInit();
  return pipeline_->StopRateChange();
}

bool MediaPlayer::SetSubtitleEnable(bool show) {
  MediaPlayerInit();
  return pipeline_->SetSubtitleEnable(show);
}

bool MediaPlayer::SetSubtitleLanguage(const std::string& language) {
  MediaPlayerInit();
  return pipeline_->SetSubtitleLanguage(language);
}

bool MediaPlayer::SetSubtitleLanguageIndex(int language) {
  MediaPlayerInit();
  return pipeline_->SetSubtitleLanguageIndex(language);
}

bool MediaPlayer::SetAudioLanguage(int index) {
  MediaPlayerInit();
  return pipeline_->SetAudioLanguage(index);
}

bool MediaPlayer::SetPlaybackSpeed (double rate) {
  MediaPlayerInit();
  return pipeline_->SetPlaybackSpeed(rate);
}

bool MediaPlayer::SetAudioMute(bool mute) {
  MediaPlayerInit();
  return pipeline_->SetAudioMute(mute);
}

bool MediaPlayer::SetAVoffset(int delay) {
  MediaPlayerInit();
  return pipeline_->SetAVoffset(delay);
}

bool MediaPlayer::SetAudioVolume(double volume) {
  MediaPlayerInit();
  return pipeline_->SetAudioVolume(volume);
}

bool MediaPlayer::SetVideoWindow(const std::string& info) {
  MediaPlayerInit();
  using boost::property_tree::ptree;

  VideoWindowInfo video_info_;
  ptree tree;
  std::string aspect_ratio;
  std::stringstream stream(info);
  try {
    boost::property_tree::read_json(stream, tree);
  } catch (const boost::property_tree::ptree_error& exception) {
    LOG_ERROR("Invalid JSON Format - %s", exception.what());
    return false;
  }

  if (tree.get_optional<std::string>("ivi-surface-info"))
    video_info_.surface_info_ = tree.get<std::string>("ivi-surface-info");

  video_info_.aspect_ratio_ = AR_FIT_TO_SCREEN;
  if (tree.get_optional<std::string>("aspect-ratio")) {
    aspect_ratio = tree.get<std::string>("aspect-ratio");
    auto iter = kAspectRatio.find(aspect_ratio);
    if (iter != kAspectRatio.end())
      video_info_.aspect_ratio_ = iter->second;
  }

  surface_info_ = video_info_.surface_info_;
  aspect_ratio_ = video_info_.aspect_ratio_;
  LOG_INFO("SetVideoWindow ivi-surface-info[%s]",video_info_.surface_info_.c_str());
  LOG_INFO("SetVideoWindow aspect-ratio[%d]",video_info_.aspect_ratio_);
  return pipeline_->SetVideoWindow(video_info_);
}

bool MediaPlayer::SetVideoBrightness(float brightness) {
    return pipeline_->SetVideoBrightness(brightness);
}

bool MediaPlayer::SetVideoSaturation(float saturation) {
    return pipeline_->SetVideoSaturation(saturation);

}

bool MediaPlayer::SetVideoContrast(float contrast) {
    return pipeline_->SetVideoContrast(contrast);
}

void MediaPlayer::fadeIn(const int ms) {
  if (audio_controller_)
    audio_controller_->fadeIn(ms);
}

void MediaPlayer::fadeOut(const int ms) {
  if (audio_controller_)
    audio_controller_->fadeOut(ms);
}

bool MediaPlayer::SwitchChannel(bool downmix) {
  LOG_INFO("SwitchChannel");
  return pipeline_->SwitchChannel(downmix);
}

int MediaPlayer::GetChannelInfo(const std::string& uri, const std::string& option) {
  LOG_INFO("GetChannelInfo");
  MediaPlayerInit();
  using boost::property_tree::ptree;

  bool need_convert = false; // need audioconvert or ccRC 5.1ch 2nd slot
  char slot_6ch = '0';
  char slot = '0';
  int channel = 0;
  double exec_time = 0;
  AVCodecID codec_id = AVCodecID::AV_CODEC_ID_NONE;

  ptree tree;
  std::string media_type_str;
  std::stringstream stream(option);
  try {
    boost::property_tree::read_json(stream, tree);
  } catch (const boost::property_tree::ptree_error& exception) {
    LOG_INFO("Invalid JSON Format - %s", exception.what());
    return false;
  }

  auto iter = tree.begin();
  ptree info = iter->second;

  if (info.get_optional<std::string>("mediatype")) {
    media_type_str = info.get<std::string>("mediatype");
    auto iteration = kMediaType.find(media_type_str);
    if (iteration != kMediaType.end()) {
      media_type_ = iteration->second;
      media_type_str_ = media_type_str;
      if (audio_controller_)
        audio_controller_->setMediaType(media_type_str);
    } else {
      LOG_WARN("unknown media type=[%s]", media_type_str.c_str());
    }
  } else {
    LOG_WARN("there is no media type..");
  }

  if (info.get_optional<std::string>("channel")) {
    channel = std::stoi(info.get<std::string>("channel"));
    LOG_INFO("#### Received channel = [%d]", channel);
  }
  if (info.get_optional<char>("2ch_slot")) {
    slot = info.get<char>("2ch_slot");
    LOG_INFO("#### Received 2ch_slot = [%c]", slot);
  }
  if (info.get_optional<char>("6ch_slot")) {
    slot_6ch = info.get<char>("6ch_slot");
    LOG_INFO("#### Received 6ch_slot = [%c]", slot_6ch);
    need_convert = (slot_6ch == '1') ? true : false;
  }

  if (audio_controller_) {
    channel = audio_controller_->getAudioChannel(uri, slot, channel, &need_convert, &codec_id, &exec_time);
  }

  return channel;
}

gboolean MediaPlayer::updateTimerFlag() {
    LOG_INFO();
    need_fade_out_ = true;
    start_timer_->Stop();
    return true;
}

void MediaPlayer::CreatePipeline(int media_type, const std::string& uri) {
  Pipeline* pipeline = creator_->CreatePipeline(media_type, uri);
  media_type_ = media_type;
  pipeline_.reset(pipeline);
}

bool MediaPlayer::ReleasePipeline(){
  LOG_INFO("");
  bool destroy_pipeline = true;

  bool ret = pipeline_->Unload(FALSE, destroy_pipeline);
  pipeline_.reset();

  return ret;
}

void MediaPlayer::SetURIInternal(const std::string& uri, int media_type) {
  bool destroy_pipeline = true;
  if (media_type == media_type_)
    destroy_pipeline = false;

  LOG_INFO("destroy_pipeline[%d]", destroy_pipeline);
#if defined(PLATFORM_GEN6)
  if (media_type_str_.compare("nature_sound") == 0) // GENSIX-55554 : nature_sound needs 'stop' event when changing track
    pipeline_->Unload(TRUE, destroy_pipeline);
  else
    pipeline_->Unload(FALSE, destroy_pipeline);
#else
    pipeline_->Unload(FALSE, destroy_pipeline);
#endif
  pipeline_.reset();

  MI::Clear();
  CreatePipeline(media_type, uri);
  MI::Get()->error_reason_ = creator_->GetErrorReason();
  pipeline_->RegisterCallback(callback_);

  VideoWindowInfo video_info_;
  video_info_.surface_info_ = surface_info_;
  video_info_.aspect_ratio_ = aspect_ratio_;
  pipeline_->SetVideoWindow(video_info_);
}

} // namespace genivimedia

