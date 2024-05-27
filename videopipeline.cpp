// @@@LICENSE
//
// Copyright (C) 2015, LG Electronics, All Right Reserved.
//
// No part of this source code may be communicated, distributed, reproduced
// or transmitted in any form or by any means, electronic or mechanical or
// otherwise, for any purpose, without the prior written permission of
// LG Electronics.
//
// LICENSE@@@

#include "player/pipeline/video_pipeline.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <math.h>

#include "logger/player_logger.h"
#include "player/pipeline/conf.h"
#include "player/pipeline/support_media_creator.h"

namespace genivimedia {

VideoPipeline::VideoPipeline()
  : gst_media_(GstMedia::Instance()),
    video_sink_(),
    audio_sink_(),
    video_filter_(),
    audio_filter_(),
    video_balance_(),
    playsink_(),
    position_timer_(new Timer()),
    trick_timer_(new Timer()),
    check_playback_timer_(new Timer()),
    loading_timer_(new Timer()),
    event_(new Event()),
    pb_info_(),
    source_info_(),
    media_type_(),
    subtitle_path_(),
#if defined (USE_SUBTITLE) || defined(USE_LGE_SUBTITLE)
    subtitle_controller_(new SubtitleController(gst_media_, event_)),
    subtitle_index_type_map_(),
    current_subtitle_index_(0),
    subtitle_status_(true),
#endif
    video_info_(),
    last_seek_pos_(-1),
    video_count_(0),
    audio_channel_(0),
    audio_slot_(-1),
    audio_6ch_slot_('0'),
    no_audio_mode_(false),
    use_atmos_(false),
    show_preroll(true),
    provide_global_clock_(true),
    bIsDolbyAtmosEacJoc(false) {
  LOG_INFO("");
}

VideoPipeline::~VideoPipeline() {
  LOG_INFO("");

#if defined (USE_SUBTITLE) || defined(USE_LGE_SUBTITLE)
  if (subtitle_controller_)
    delete subtitle_controller_;
#endif
  if (position_timer_)
    delete position_timer_;
  if (trick_timer_)
    delete trick_timer_;
  if (check_playback_timer_)
    delete check_playback_timer_;
  if (loading_timer_)
    delete loading_timer_;
  if (event_)
    delete event_;
}

bool VideoPipeline::RegisterCallback(EventHandler callback) {
  return event_->RegisterCallback(callback);
}

bool VideoPipeline::Load(const std::string& uri)  {
  LOG_INFO("Load with URI[%s]", uri.c_str());
  bool ret = false;
  //static std::string prevURI;
  std::string raw_uri = gst_media_->GetRawURI(uri);
  if (raw_uri.empty()) {

    LOG_INFO("Fail to convert raw uri");
    return ret;
  }
  pb_info_.is_mtp_ = gst_media_->IsMtpFile(uri);

  gst_media_->CreateGstPlaybin("video_pipeline");

  ElementAddCallback elementadd_callback = std::bind(&VideoPipeline::HandleElementAdd, this,
                                                     std::placeholders::_1,
                                                     std::placeholders::_2,
                                                     std::placeholders::_3);
  gst_media_->RegisterElementAddBin(elementadd_callback);
  BusCallback callback = std::bind(&VideoPipeline::BusMessage, this,
                                   std::placeholders::_1,
                                   std::placeholders::_2,
                                   std::placeholders::_3);
  gst_media_->RegisterWatchBus(callback);

  ControlProperties(const_cast<char*>(raw_uri.c_str()));

  TimerCallback position_callback = std::bind(&VideoPipeline::UpdatePositionInfo, this);
  position_timer_->AddCallback(position_callback, 1000);
  TimerCallback trick_callback = std::bind(&VideoPipeline::HandleTrickPlay, this);
  trick_timer_->AddCallback(trick_callback, 500);

  SeekControlCallback seekcallback = std::bind(&VideoPipeline::HandleSeekControl, this, std::placeholders::_1);
  gst_media_->RegisterSeekControl(seekcallback);

  ret = gst_media_->ChangeStateToPause();
#if defined (USE_SUBTITLE) || defined(USE_LGE_SUBTITLE)
  subtitle_controller_->SetUri(subtitle_path_);
#endif

  return ret;
}

bool VideoPipeline::Load(const std::string& uri, const std::string& option, int channel, bool convert, char slot) {
  using boost::property_tree::ptree;

  ptree tree;
  std::stringstream stream(option);
  try {
    boost::property_tree::read_json(stream, tree);
  } catch (const boost::property_tree::ptree_error& exception) {
    LOG_INFO("Invalid JSON Format - %s", exception.what());
    return false;
  }

  audio_slot_ = slot;
  audio_channel_ = channel;

  auto iter = tree.begin();
  ptree info = iter->second;

  if (info.get_optional<bool>("show-preroll-frame"))
    show_preroll = info.get<bool>("show-preroll-frame");
  if (info.get_optional<bool>("provide-global-clock"))
    provide_global_clock_ = info.get<bool>("provide-global-clock");
  if (info.get_optional<double>("saturation"))
    video_info_.saturation_ = info.get<double>("saturation");
  if (info.get_optional<double>("contrast"))
    video_info_.contrast_ = info.get<double>("contrast");
  if (info.get_optional<double>("brightness"))
    video_info_.brightness_ = info.get<double>("brightness");
  if (info.get_optional<std::string>("mediatype"))
    media_type_ = info.get<std::string>("mediatype");
  if (info.get_optional<char>("6ch_slot")) {
    audio_6ch_slot_ = info.get<char>("6ch_slot");
    LOG_INFO("Received 6ch_slot = [%c]", audio_6ch_slot_);
  }
  if (info.get_optional<std::string>("subtitle-path"))
    subtitle_path_ = info.get<std::string>("subtitle-path");
  if ((media_type_.compare("mood_therapy_video") == 0) ||
      (media_type_.compare("recording_play") == 0) ||
      (media_type_.compare("dvrs_rear") == 0)) {
    no_audio_mode_ = true;
  }

  if (media_type_.compare("mood_therapy_video") == 0) {
    pb_info_.mode = PLAYBACK_REPEAT_GAPLESS;
  } else {
    pb_info_.mode = PLAYBACK_NORMAL;
  }
  LOG_INFO("preroll=[%d], clock=[%d], ch=[%d], convert=[%d], type=[%s]",
            show_preroll, provide_global_clock_, audio_channel_, convert, media_type_.c_str());
  LOG_INFO("saturation[%lf] contrast[%lf] brightness[%lf] mode[%d]",
            video_info_.saturation_, video_info_.contrast_, video_info_.brightness_, pb_info_.mode);
  return Load(uri);
}

bool VideoPipeline::Unload(bool send_event, bool destroy_pipeline) {
  LOG_DEBUG("Unload");

  bool ret = UnloadInternal(destroy_pipeline);
  if (send_event)
    event_->NotifyEventPlaybackStatus(STATE_STOPPED);
  return ret;
}

bool VideoPipeline::UnloadInternal(bool destroy_pipeline) {
  LOG_INFO("UnloadInternal(%d)", destroy_pipeline);

  bool ret = false;
  bool use_keep_alive = true;
  GstState cur_state = GST_STATE_NULL;

#if defined (USE_SUBTITLE) || defined(USE_LGE_SUBTITLE)
  subtitle_controller_->Stop();
#endif
  trick_timer_->Stop();
  position_timer_->Stop();
  check_playback_timer_->Stop();
  loading_timer_->Stop();
  if (event_->ErrorOccurred()) {
    destroy_pipeline = true;
    LOG_ERROR("UnloadInternal(%d)", destroy_pipeline);
  }

  if (gst_media_ != nullptr && gst_media_->GetCurPipelineState(&cur_state)) {
    LOG_INFO("cur_state is [%u]", cur_state);
    if (cur_state >= GST_STATE_PAUSED) {
      ret = gst_media_->ChangeStateToReady();
      if (gst_media_->GetCurPipelineState(&cur_state))
        LOG_INFO("After changing state to READY(2)-[%u]", cur_state);
      else
        LOG_ERROR("changing state to READY fail");
    }
    if (cur_state == GST_STATE_READY) {
      destroy_pipeline = true;
      use_keep_alive = false;
      goto EXIT;
    }
  } else if (gst_media_ != nullptr) {
    LOG_INFO("pipeline has invalid state. try to destroy");
    destroy_pipeline = true;
  }

EXIT:
  if (gst_media_ != nullptr)
    ret = gst_media_->StopGstPipeline(destroy_pipeline, use_keep_alive);

  if (destroy_pipeline) {
    GstMedia::Destroy();
    gst_media_ = nullptr;
    video_sink_ = nullptr;
    audio_sink_ = nullptr;
    video_filter_ = nullptr;
    audio_filter_ = nullptr;
    playsink_ = nullptr;
    video_balance_ = nullptr;
  }
  return ret;
}

bool VideoPipeline::Play() {
  LOG_INFO("Play");

  bool ret = false;
  ret = gst_media_->ChangeStateToPlay();
#if defined (USE_SUBTITLE) || defined(USE_LGE_SUBTITLE)
  subtitle_controller_->Play();
#endif
  return ret;
}

bool VideoPipeline::Pause() {
  LOG_INFO("Pause");

  bool ret = false;
  if (!pb_info_.is_load_completed_)
    return false;

  if (gst_media_->ChangeStateToPause()) {
    pb_info_.is_external_paused_ = true;
    ret = true;
  }
  return ret;
}

bool VideoPipeline::FastForward(gdouble rate) {
  LOG_INFO("FastForward-%g", rate);
  bool ret = false;
  if (!CheckSeekNRateControl()) {
    LOG_ERROR("Failed to fast forward operation");
    return ret;
  }

  ControlPlaybackRate(rate);
  event_->NotifyEventTrickPlaying();
  return true;
}

bool VideoPipeline::Rewind(gdouble rate) {
  LOG_INFO("Rewind-%g", rate);
  bool ret = false;
  if (!CheckSeekNRateControl()) {
    LOG_ERROR("Failed to fast rewind operation");
    return ret;
  }

  ControlPlaybackRate(rate);
  event_->NotifyEventTrickPlaying();
  return true;
}

bool VideoPipeline::Seek(gint64 position) {
  LOG_INFO("Seek-%lld", position);
  bool ret = false;

  if (!CheckSeekNRateControl()) {
    LOG_ERROR("failed to seek operation");
    return ret;
  }

  if (last_seek_pos_ > 0 && last_seek_pos_ == position) {
    LOG_ERROR("skip seek as matching last value");
    return ret;
  }

  if (gst_media_->Seek(position)) {
    pb_info_.current_position_ = position*GST_MSECOND;
    pb_info_.is_seeking_ = true;
    ret = true;
    event_->NotifyEventSeeking();
    last_seek_pos_ = position;
    gst_media_->SetSeekStatus(true);
    LOG_INFO("Success to seek");
  }
  return ret;
}

bool VideoPipeline::StopRateChange() {
  LOG_INFO("");
  bool ret = false;
  if (!CheckSeekNRateControl()) {
    LOG_ERROR("Failed to stopratechange operation");
    return ret;
  }

  trick_timer_->Stop();
  pb_info_.playback_rate_ = 1.0;
  event_->NotifyEventAsyncDone(false);
  ret = true;
  return ret;
}

bool VideoPipeline::SetSubtitleEnable(bool show) {
  LOG_INFO("SetSubtitleEnable");
#if defined (USE_SUBTITLE)
  return subtitle_controller_->SetSubtitleEnable(show);
#elif defined(USE_LGE_SUBTITLE)
  subtitle_status_ = show;
  if (!pb_info_.is_load_completed_)
    return false;
  std::map<int, int>::iterator it = subtitle_index_type_map_.find(current_subtitle_index_);
  if (it != subtitle_index_type_map_.end()) {
    if (it->second == INTERNAL_SUBTITLE) {
      LOG_INFO("Internal subtitle running");
      return gst_media_->SetSubtitleEnable(show);
    } else if (it->second == EXTERNAL_SUBTITLE) {
      LOG_INFO("External subtitle running");
      return subtitle_controller_->SetSubtitleEnable(show);
    } else {
      LOG_INFO("No subtitle");
      return false;
    }
  } else {
      LOG_INFO("Empty subtitle list");
      return false;
  }
#else
  return false;
#endif
}

bool VideoPipeline::SetSubtitleLanguage(const std::string& language) {
//  LOG_INFO("SetSubtitleLanguage [%s]", language.c_str());
#if defined (USE_SUBTITLE)
  return subtitle_controller_->SetSubtitleLanguage(language);
#elif defined(USE_LGE_SUBTITLE)
  LOG_WARN("SetSubtitleLanguage[%s]: Not supported API in LGE subtitle solution", language.c_str());
  return false;
#else
  return false;
#endif
}

bool VideoPipeline::SetSubtitleLanguageIndex(int language) {
  LOG_INFO("SetSubtitleLanguageIndex");
#ifdef USE_SUBTITLE
  return subtitle_controller_->SetSubtitleLanguageIndex(language);
#elif defined(USE_LGE_SUBTITLE)
  current_subtitle_index_ = language;

  if (!pb_info_.is_load_completed_)
    return false;
  std::map<int, int>::iterator it = subtitle_index_type_map_.find(language);
  if (it != subtitle_index_type_map_.end()) {
    if (it->second == INTERNAL_SUBTITLE) {
      LOG_INFO("Setting internal subtitle");
      subtitle_controller_->SetSubtitleEnable(false);
      return gst_media_->SetSubtitleLanguageIndex(language);
    } else if (it->second == EXTERNAL_SUBTITLE) {
      LOG_INFO("Setting external subtitle");
      gst_media_->SetSubtitleEnable(false);
      return subtitle_controller_->SetSubtitleLanguageIndex(language);
    } else {
      LOG_INFO("Invalid Index");
      return false;
    }
  } else {
      LOG_INFO("Empty subtitle list");
      return false;
  }
#else
  return false;
#endif
}

bool VideoPipeline::SetAudioLanguage(int index) {
  LOG_INFO("SetAudioLanguage");
  if (!pb_info_.is_load_completed_)
    return false;
  return gst_media_->SetAudioLanguage(index);
}

bool VideoPipeline::SetPlaybackSpeed(double rate) {
  LOG_INFO("SetPlaybackSpeed");
  if (!pb_info_.is_load_completed_)
    return false;
  return gst_media_->SetPlaybackSpeed(rate);
}

bool VideoPipeline::SetAudioMute(bool mute) {
  LOG_INFO("SetAudioMute");
  if (MI::Get()->warning_reason_ == WARNING_AUDIO_CODEC_NOT_SUPPORTED) {
    LOG_WARN("Not supported audio codec, so ignore mute=[%d]", mute);
    return false;
  }
  return gst_media_->SetAudioMute(mute);
}

bool VideoPipeline::SetAVoffset(int delay) {
  LOG_INFO("SetAVoffset");
  if (!pb_info_.is_load_completed_)
    return false;
  return gst_media_->SetAVoffset(delay);
}

bool VideoPipeline::SetAudioVolume(double volume) {
  LOG_INFO("SetAudioVolume");
  if (!pb_info_.is_load_completed_)
    return false;
  return gst_media_->SetAudioVolume(volume);
}

bool VideoPipeline::SwitchChannel(bool downmix) {
  LOG_INFO("SwitchChannel[%d], current channel=[%d]", downmix, audio_channel_);
  if (!pb_info_.is_load_completed_)
    return false;
  int new_channel = (downmix) ? 2 : 6;
  if(audio_channel_ == new_channel)
    return false;
  else {
    audio_channel_ = new_channel;
  }
  if(gst_media_->SwitchChannel(downmix, audio_slot_, audio_6ch_slot_, false, media_type_, provide_global_clock_, audio_sink_, gst_media_->GetPipeline())) {
    if(!use_atmos_)
      event_->NotifyEventChannel(media_type_, audio_channel_);
  } else {
    return false;
  }
  return true;
}

bool VideoPipeline::SetVideoWindow(VideoWindowInfo &info) {
  LOG_INFO("SetVideoWindow with %s, %d", info.surface_info_.c_str(), info.aspect_ratio_);
  video_info_ = info;
  if (video_sink_) {
   // gst_media_->SetProperty<gchar*>(video_sink_, "ivi-surface-info", const_cast<gchar*>(info.surface_info_.c_str()));
   // gboolean force_aspect_ratio = (video_info_.aspect_ratio_)? false:true;
    //gst_media_->SetProperty<gboolean>(video_sink_, "force-aspect-ratio", force_aspect_ratio);
  }
  return true;
}

#ifdef PLATFORM_TELECHIPS
bool VideoPipeline::SetVideoSaturation(float saturation){
    if(video_balance_ != nullptr){
        gst_media_->SetProperty<gfloat>(video_balance_, "saturation", saturation);
        LOG_INFO("SetVideoSaturation videobalance: %f", saturation);
        return true;
    } else {
        LOG_ERROR("Error : can't find video balance element");
    }

    return false;
}

bool VideoPipeline::SetVideoBrightness(float brightness){
    if(video_balance_ != nullptr){
        brightness = (brightness < -0.3f) ? -0.25f :
                     (brightness < -0.25f) ? -0.2f :
                     (brightness < -0.2f) ? -0.15f : 
                     (brightness < -0.1f) ? -0.1f :
                     (brightness < 0) ? -0.05f : brightness;
        gst_media_->SetProperty<gfloat>(video_balance_, "brightness", brightness);
        LOG_INFO("SetVideoBrightness videobalance:%f", brightness);
        return true;
    } else {
        LOG_ERROR("Error : can't find video balance element");
    }

    return false;
}

bool VideoPipeline::SetVideoContrast(float contrast){
    if(video_balance_ != nullptr){
        if(contrast < 1.0f) {
          contrast += 0.05f;
        }
        gst_media_->SetProperty<gfloat>(video_balance_, "contrast", contrast);
        LOG_INFO("SetVideoContrast videobalance: %f", contrast);
        return true;
    } else {
        LOG_ERROR("Error : can't find video balance element");
    }

    return false;
}

//bool VideoPipeline::SetVideoContrast(float contrast) {
  if(video_balance_ != nullptr) }{
      if(contrast < 1.0f){
        contrast += 0.05f;
      }
      gst_media_->SetProperty<gfloat>(video_balance_, "contrast", contrast);
      LOG_INFO("SetVideoContrast videobalance: %f", contrast);
      return true;
    else {
      LOG_ERROR("ERROE : can't find video balance element");
    }
    
    return false;
}
#else
bool VideoPipeline::SetVideoSaturation(float saturation){
    if(video_sink_ != nullptr){
        gst_media_->SetProperty<gfloat>(video_sink_, "saturation", saturation);
        LOG_DEBUG("SetVideoSaturation: %f", saturation);
        return true;
    } else {
        LOG_ERROR("Error : can't find video sink element");
    }

    return false;
}


bool VideoPipeline::SetVideoBrightness(float brightness){
    if(video_sink_ != nullptr){
        gst_media_->SetProperty<gfloat>(video_sink_, "brightness", brightness);
        LOG_DEBUG("SetVideoBrightness:%f", brightness);
        return true;
    } else {
        LOG_ERROR("Error : can't find video sink element");
    }

    return false;
}

bool VideoPipeline::SetVideoContrast(float contrast){
    if(video_sink_ != nullptr){
        gst_media_->SetProperty<gfloat>(video_sink_, "contrast", contrast);
        LOG_DEBUG("SetVideoContrast: %f", contrast);
        return true;
    } else {
        LOG_ERROR("Error : can't find video sink element");
    }

    return false;
}
#endif

gboolean VideoPipeline::UpdatePositionInfo() {
  gint64 position = 0;

  if (pb_info_.is_eos_ || pb_info_.is_seeking_ || fabs(pb_info_.playback_rate_ - 1.0) > DBL_EPSILON)
    return true;

  GstState cur_state = GST_STATE_NULL;
  if (!gst_media_->GetCurPipelineState(&cur_state) ||
      (cur_state == GST_STATE_PAUSED))
    return true;

  if (gst_media_->GetCurPosition(&position)) {
    if(position > pb_info_.duration_)
      position = pb_info_.duration_;
    //if((position/GST_SECOND) > (pb_info_.current_position_/GST_SECOND)){
        pb_info_.current_position_ = position;
        last_seek_pos_ = -1;
        event_->NotifyEventCurrentPosition(position);
        LOG_INFO ("UpdatePositionInfo time(sec)-%f", (float)position/GST_SECOND);
    //}
  }
  return true;
}

void VideoPipeline::ControlProperties(char* raw_uri){
#if defined(PLATFORM_NVIDIA)
    ControlPropertiesNvidia(raw_uri);
    return;
#elif defined (PLATFORM_TELECHIPS)
    ControlPropertiesTelechips(raw_uri);
    return;
#else
    ControlPropertiesCommon(raw_uri);
#endif
}

void VideoPipeline::ControlPropertiesNvidia(char* raw_uri) {
  gchar* video_sink = Conf::GetSink(VIDEO_SINK);
  if (strlen(video_sink)) {
    video_sink_ = gst_media_->CreateElement(video_sink, "video_sink");

    if (video_sink_) {
      GstElement *video_bin = gst_bin_new("videosinkbin");
      GstElement *tee = gst_element_factory_make("tee", NULL);
      GstPad *pad = NULL;
      GstPad *ghost_pad = NULL;

      if(video_bin == NULL || tee == NULL){
          LOG_ERROR("create element error ");
          return;
      }

      if (strcmp(video_sink,"nvmediaeglwaylandsink") == 0) {
          int surface_id = Conf::GetSurfaceIdByMediatype(media_type_.c_str());
          LOG_INFO("Set ivi-surface id=[%d].. media_type=[%s]", surface_id, media_type_.c_str());
          if (surface_id < 0) {
            surface_id = 10000; // Use default value
          }
          gst_media_->SetProperty<int>(video_sink_, "ivisurface-id", surface_id);
          gst_media_->SetProperty<gboolean>(video_sink_, "show-preroll-frame", show_preroll);
      }

      gchar* video_filter = Conf::GetFilter(VIDEO_SINK);
      if (strlen(video_filter)) {
        video_filter_ = gst_media_->CreateElement(video_filter, "video_filter");
      }

      if (video_filter_) {
          SetVideoBrightness(video_info_.brightness_);
          SetVideoContrast(video_info_.contrast_);
          SetVideoSaturation(video_info_.saturation_);

          gst_bin_add_many(GST_BIN(video_bin), tee, video_filter_, video_sink_, NULL);
          gst_element_link_many(tee, video_filter_, video_sink_,NULL);

          pad = gst_element_get_static_pad(tee,"sink");
          ghost_pad = gst_ghost_pad_new("sink", pad);
          gst_pad_set_active(ghost_pad, TRUE);
          gst_element_add_pad(video_bin, ghost_pad);
          gst_object_unref(pad);
      } else {
          gst_bin_add_many(GST_BIN(video_bin), tee, video_sink_, NULL);
          gst_element_link_many(tee, video_sink_, NULL);
      }
      gst_media_->SetProperty<GstElement*>(gst_media_->GetPipeline(), "video-sink", const_cast<GstElement*>(video_bin));
    }
  }

  if (!no_audio_mode_) {
    gchar* audio_sink = Conf::GetSink(AUDIO_SINK);
    if (strlen(audio_sink)) {
      std::string audio_property(audio_sink);
      provide_global_clock_ ? audio_property.append(" provide-clock=true") : audio_property.append(" provide-clock=false");
      audio_sink_ = gst_media_->CreateAudioSinkBin(audio_property.c_str(), audio_slot_, audio_6ch_slot_, audio_channel_, false, media_type_);
      if (audio_sink_) {
        //gst_media_->SetProperty<gboolean>(audio_sink_, "hwsrc", true);
        gst_media_->SetProperty<GstElement*>(gst_media_->GetPipeline(), "audio-sink", const_cast<GstElement*>(audio_sink_));
      }
    }

    gchar* audio_filter = Conf::GetFilter(AUDIO_SINK);
    if (strlen(audio_filter)) {
      audio_filter_ = gst_media_->CreateElement(audio_filter, "audio_filter");
      if (audio_filter_) {
        gst_media_->SetProperty<GstElement*>(gst_media_->GetPipeline(), "audio-filter", const_cast<GstElement*>(audio_filter_));
      }
    }
  }
  gst_media_->SetProperty<char*>(gst_media_->GetPipeline(), "uri", raw_uri);
}

void VideoPipeline::ControlPropertiesTelechips(char* raw_uri) {
  gchar* video_sink = Conf::GetSink(VIDEO_SINK);
  playsink_ = gst_bin_get_by_name(GST_BIN(gst_media_->GetPipeline()), "playsink");
  if(playsink_){
    LOG_INFO("get playsink");
    guint flags;
    flags =  GST_PLAY_FLAG_AUDIO | GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_SOFT_VOLUME | GST_PLAY_FLAG_DEINTERLACE | GST_PLAY_FLAG_SOFT_COLORBALANCE;
    gst_media_->SetProperty<gint>(gst_media_->GetPipeline(), "flags", flags);
    ElementAddCallback elementadd_callback = std::bind(&VideoPipeline::HandlePlaySinkElementAdd, this,
                                                          std::placeholders::_1,
                                                          std::placeholders::_2,
                                                          std::placeholders::_3);
    gst_media_->RegisterPlaySinkElementAddBin(elementadd_callback, playsink_);
    if(playsink_)
      gst_object_unref(G_OBJECT(playsink_));
  }
  
  if (strlen(video_sink)) {
    video_sink_ = gst_media_->CreateElement(video_sink, "video_sink");
    if (video_sink_) {
      if (strcmp(video_sink,"waylandsink") == 0) {
        int surface_id = Conf::GetSurfaceIdByMediatype(media_type_.c_str());
        LOG_INFO("Set wayland surface id=[%d].. media_type=[%s]", surface_id, media_type_.c_str());
        if (surface_id < 0) {
          surface_id = 10000; // Use default value
        }
        gst_media_->SetProperty<int>(video_sink_, "surface-id", surface_id);
      } else {
        LOG_INFO("Other video sink is used..[%s]", video_sink);
      }
      gst_media_->SetProperty<gboolean>(video_sink_, "show-preroll-frame", show_preroll);
      gst_media_->SetProperty<GstElement*>(gst_media_->GetPipeline(), "video-sink", const_cast<GstElement*>(video_sink_));

      gchar* video_filter = Conf::GetFilter(VIDEO_SINK);
      if (strlen(video_filter)) {
        LOG_INFO("Set video filter=[%s]", video_filter);
        video_filter_ = gst_media_->CreateElement(video_filter, "video_filter");
        if (video_filter_) {
          gst_media_->SetProperty<GstElement*>(gst_media_->GetPipeline(), "video-filter", const_cast<GstElement*>(video_filter_));
        }
      }
    }
  }

  if (!no_audio_mode_) {
    gchar* audio_sink = Conf::GetSink(AUDIO_SINK);
    if (strlen(audio_sink)) {
      std::string audio_property(audio_sink);
      provide_global_clock_ ? audio_property.append(" provide-clock=true") : audio_property.append(" provide-clock=false");
      audio_sink_ = gst_media_->CreateAudioSinkBin(audio_property.c_str(), audio_slot_, audio_6ch_slot_, audio_channel_, false, media_type_);
      if (audio_sink_) {
        gst_media_->SetProperty<GstElement*>(gst_media_->GetPipeline(), "audio-sink", const_cast<GstElement*>(audio_sink_));
      }
    }

    gchar* audio_filter = Conf::GetFilter(AUDIO_SINK);
    if (strlen(audio_filter)) {
      audio_filter_ = gst_media_->CreateElement(audio_filter, "audio_filter");
      if (audio_filter_) {
        gst_media_->SetProperty<GstElement*>(gst_media_->GetPipeline(), "audio-filter", const_cast<GstElement*>(audio_filter_));
      }
    }
  }
  //gst_media_->SetProperty<gboolean>(gst_media_->GetPipeline(), "force-aspect-ratio", force_aspect_ratio);
  gst_media_->SetProperty<char*>(gst_media_->GetPipeline(), "uri", raw_uri);
}

void VideoPipeline::ControlPropertiesCommon(char* raw_uri) {
  gboolean force_aspect_ratio = (video_info_.aspect_ratio_)? false:true;

  gchar* video_sink = Conf::GetSink(VIDEO_SINK);
  if (strlen(video_sink)) {
    video_sink_ = gst_media_->CreateElement(video_sink, "video_sink");
    if (video_sink_) {
      LOG_INFO("Set ivi-surface-info[%s]", video_info_.surface_info_.c_str());
      gst_media_->SetProperty<gchar*>(video_sink_, "ivi-surface-info", const_cast<gchar*>(video_info_.surface_info_.c_str()));
      gst_media_->SetProperty<gboolean>(video_sink_, "force-aspect-ratio", force_aspect_ratio);
      gst_media_->SetProperty<gboolean>(video_sink_, "show-preroll-frame", show_preroll);
      gst_media_->SetProperty<GstElement*>(gst_media_->GetPipeline(), "video-sink", const_cast<GstElement*>(video_sink_));
    }
  }
  gchar* video_filter = Conf::GetFilter(VIDEO_SINK);
  if (strlen(video_filter)) {
    video_filter_ = gst_media_->CreateElement(video_filter, "video_filter");
    if (video_filter_) {
      gst_media_->SetProperty<GstElement*>(gst_media_->GetPipeline(), "video-filter", const_cast<GstElement*>(video_filter_));
    }
  }

  if (!no_audio_mode_) {
    gchar* audio_sink = Conf::GetSink(AUDIO_SINK);
    if (strlen(audio_sink)) {
      audio_sink_ = gst_media_->CreateElement(audio_sink, "audio_sink");
      if (audio_sink_) {
        gst_media_->SetProperty<gboolean>(audio_sink_, "provide-clock", provide_global_clock_);
        gst_media_->SetProperty<GstElement*>(gst_media_->GetPipeline(), "audio-sink", const_cast<GstElement*>(audio_sink_));
      }
    }
    gchar* audio_filter = Conf::GetFilter(AUDIO_SINK);
    if (strlen(audio_filter)) {
      audio_filter_ = gst_media_->CreateElement(audio_filter, "audio_filter");
      if (audio_filter_) {
        gst_media_->SetProperty<GstElement*>(gst_media_->GetPipeline(), "audio-filter", const_cast<GstElement*>(audio_filter_));
      }
    }
  }

  gst_media_->SetProperty<gboolean>(gst_media_->GetPipeline(), "force-aspect-ratio", force_aspect_ratio);
  gst_media_->SetProperty<char*>(gst_media_->GetPipeline(), "uri", raw_uri);
}

gboolean VideoPipeline::CheckPlayback() {
  if (video_count_ < 2 ) {
    LOG_ERROR("No Video");
    if (pb_info_.is_load_completed_)
      event_->NotifyEventError();
  }

  if (!pb_info_.is_load_completed_) {
    LOG_ERROR(" ### PLAYBACK TIMEOUT - video__count=[%d] ###", video_count_);
    event_->NotifyEventError(ERROR_GST_NO_RESPONSE);
    LOG_ERROR(" ### Done ###");
  }
  return false;
}

gboolean VideoPipeline::CheckShowLoading() {
  if (!pb_info_.is_load_completed_) {
    LOG_INFO("show loading message");
    event_->NotifyEventWarn(WARNING_LOADING_START);
    pb_info_.is_show_loading = true;
  }
  return false;
}

gboolean VideoPipeline::CheckSeekNRateControl() {
  GstState cur_state = GST_STATE_NULL;
  if (!pb_info_.is_load_completed_ ||
      pb_info_.is_seeking_         ||
      !source_info_.can_seek_      ||
      !gst_media_->GetCurPipelineState(&cur_state) ||
      (cur_state < GST_STATE_PAUSED)) {
    return false;
  } else {
    return true;
  }
}

void VideoPipeline::ControlPlaybackRate(gdouble rate) {
  gint64 position = 0;

  if (!pb_info_.playback_started) {
    position_timer_->Start();
    event_->NotifyEventPlaybackStatus(STATE_PLAYING);
    pb_info_.playback_started = true;
  }

  trick_timer_->Stop();
  position = pb_info_.current_position_;
  gst_media_->ChangeStateToPause();

  trick_timer_->Start();
  pb_info_.trick_position_ = pb_info_.current_position_ = position;
  pb_info_.playback_rate_ = rate;
  pb_info_.playback_rate_count_ = 0;
}

void VideoPipeline::ControlAsfContainer(GstElement* bin) {
  bool found = false;
  GstIterator *it_uridecodebin;
  GstIterator *it_decodebin;
  GValue elem_uridecodebin = G_VALUE_INIT;
  GValue elem_decodebin = G_VALUE_INIT;
  gchar* name_elem_uridecodebin = nullptr;
  gchar* name_elem_decodebin = nullptr;

  it_uridecodebin = gst_bin_iterate_elements(GST_BIN(bin));
  while (!found && gst_iterator_next(it_uridecodebin, &elem_uridecodebin) == GST_ITERATOR_OK) {

    name_elem_uridecodebin = gst_element_get_name(g_value_get_object(&elem_uridecodebin));
    if (g_strrstr(name_elem_uridecodebin, "decodebin")) {
      it_decodebin = gst_bin_iterate_elements(GST_BIN(g_value_get_object(&elem_uridecodebin)));
      while (gst_iterator_next(it_decodebin, &elem_decodebin) == GST_ITERATOR_OK) {
        name_elem_decodebin = gst_element_get_name(g_value_get_object(&elem_decodebin));
        if (g_strrstr(name_elem_decodebin, "asfdemux")) {
          gst_media_->SetProperty<gboolean>((GstElement*)g_value_get_object(&elem_decodebin), "ignore-eos", true);
          LOG_INFO("");
          found = true;
          g_free(name_elem_decodebin);
          break;
        }
        g_free(name_elem_decodebin);
        g_value_reset(&elem_decodebin);
      }
    }
    g_free(name_elem_uridecodebin);
    g_value_reset(&elem_uridecodebin);
  }
  g_value_unset(&elem_decodebin);
  g_value_unset(&elem_uridecodebin);
  gst_iterator_free(it_decodebin);
  gst_iterator_free(it_uridecodebin);
}

bool VideoPipeline::TrickPlayInternal(gint64 position) {
  if (pb_info_.is_seeking_in_trick_)
    return false;
  if (gst_media_->Seek(position))
    pb_info_.is_seeking_in_trick_ = true;
  return true;
}

gboolean VideoPipeline::BusMessage(GstBus* bus, GstMessage* message, gpointer data) {
  gboolean ret = true;
  GstElement* pipeline = gst_media_->GetPipeline();
  if (!pipeline) {
    ret = false;
    goto EXIT;
  }
  //LOG_INFO("[BusMessage] src[%s] type[%d]", GST_MESSAGE_SRC_NAME(message), GST_MESSAGE_TYPE(message));

  if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_APPLICATION)
    HandleBusApplication(message);

  if (GST_MESSAGE_SRC(message) == GST_OBJECT_CAST(pipeline))
    ret = HandleBusPipelineMessage(message);
  else
    ret = HandleBusElementMessage(message);

EXIT:
  return ret;
}

gboolean VideoPipeline::HandleElementAdd(GstBin* bin, GstElement *element, gpointer data) {
  gchar *element_name = gst_element_get_name(element);

  if (!element_name)
    return true;
  LOG_INFO("%s\n", element_name);
  if (nullptr != g_strrstr (element_name, "uridecodebin")) {
    ElementAddCallback elementadd_callback = std::bind(&VideoPipeline::HandleUriDecodeBinElementAdd, this,
                                                       std::placeholders::_1,
                                                       std::placeholders::_2,
                                                       std::placeholders::_3);
    gst_media_->RegisterUriDecodeBinElementAddBin(elementadd_callback, element);

    AutoPlugSortCallback autoplugsort_callback = std::bind(&VideoPipeline::HandleAutoplugSort, this,
                                                       std::placeholders::_1,
                                                       std::placeholders::_2,
                                                       std::placeholders::_3,
                                                       std::placeholders::_4,
                                                       std::placeholders::_5);
    gst_media_->RegisterAutoPlugSort(autoplugsort_callback, element);
    AutoPlugSelectCallback autoplugselect_callback = std::bind(&VideoPipeline::HandleAutoplugSelect, this,
                                                       std::placeholders::_1,
                                                       std::placeholders::_2,
                                                       std::placeholders::_3,
                                                       std::placeholders::_4,
                                                       std::placeholders::_5);
    gst_media_->RegisterAutoPlugSelect(autoplugselect_callback, element);
    TimerCallback playback_callback = std::bind(&VideoPipeline::CheckPlayback, this);

    int interval = (pb_info_.is_mtp_)? 50000 : 1800;
    LOG_INFO("timeout interval-%d", interval);
    check_playback_timer_->AddCallback(playback_callback, interval);
    check_playback_timer_->Start();

    NoMorePadsCallback nomorepads_callback = std::bind(&VideoPipeline::HandleNoMorePads, this,
                                                       std::placeholders::_1,
                                                       std::placeholders::_2);
    gst_media_->RegisterNoMorePads(nomorepads_callback, element);
    TimerCallback loading_callback = std::bind(&VideoPipeline::CheckShowLoading, this);
    loading_timer_->AddCallback(loading_callback, 2000);

  }

  g_free(element_name);
  return true;
}
gboolean VideoPipeline::HandlePlaySinkElementAdd(GstBin* bin, GstElement *element, gpointer data) {
  gchar *element_name = gst_element_get_name(element);

  if (!element_name)
    return true;
  LOG_INFO("%s\n", element_name);
  if (nullptr != g_strrstr (element_name, "vbin")) {
    GstElement *conv, *scale;
    GstCaps *caps;
    GstElement *vconv =  gst_bin_get_by_name(GST_BIN(element), "vconv");
    if(!vconv) {
      LOG_ERROR("playsink vconv not exist");
      return true;
    }
    conv = gst_bin_get_by_name(GST_BIN(vconv), "conv");
    if(!conv) {
      LOG_ERROR("playsink videoconvert not exist");
      return true;
    }
    scale = gst_bin_get_by_name(GST_BIN(vconv), "scale");
    if(!scale) {
      LOG_ERROR("playsink videoscale not exist");
      return true;
    }
    video_balance_ = gst_bin_get_by_name(GST_BIN(vconv), "videobalance");
    if(!video_balance_) {
      LOG_ERROR("playsink videobalance not exist");
      return true;
    }
    SetVideoBrightness(video_info_.brightness_);
    SetVideoContrast(video_info_.contrast_);
    SetVideoSaturation(video_info_.saturation_);
    
    gst_element_unlink(conv, scale);
    caps = gst_caps_new_simple("video/x-raw",
      //"chroma-site", G_TYPE_STRING, "mpeg2",
      "name", G_TYPE_STRING, "vbcaps",
      NULL);
    if(!gst_element_link_filtered(conv, scale, caps)) {
      LOG_ERROR("Caps Linking Failed!!");
    }
    gst_object_unref(G_OBJECT(video_balance_));
    gst_object_unref(G_OBJECT(conv));
    gst_object_unref(G_OBJECT(scale));
    gst_object_unref(G_OBJECT(caps));
    gst_object_unref(G_OBJECT(vconv));
  }
  g_free(element_name);
  return true;
}

gboolean VideoPipeline::HandleUriDecodeBinElementAdd(GstBin* bin, GstElement *element, gpointer data) {
  gchar *element_name = gst_element_get_name(element);
  if (!element_name)
    return true;

  LOG_INFO("%s", element_name);

  if (nullptr != g_strrstr (element_name, "decodebin")) {
    ElementAddCallback elementadd_callback = std::bind(&VideoPipeline::F, this,
                                                       std::placeholders::_1,
                                                       std::placeholders::_2,
                                                       std::placeholders::_3);
    gst_media_->RegisterDecodeBinElementAddBin(elementadd_callback, element);
  }

  g_free(element_name);

  return true;
}

gboolean VideoPipeline::HandleNotifyAtmos (GstObject *gstobject, GstObject *prop_object,
                                                 GParamSpec *prop, gpointer data) {
  gchar *content_type = NULL;
  g_object_get (G_OBJECT (prop_object), "notify-atmos", &content_type, NULL);
  LOG_INFO ("NotifyAtmos content type [%s]", content_type);
  if (content_type !=  NULL) {
    bIsDolbyAtmosEacJoc = true;
    if (audio_channel_ > 2) {
      event_->NotifyEventContentType(content_type);
    } else if (audio_channel_ == 2) { // This is used for ccIC MY24 Atmos case
      std::string updatedType(content_type);
      updatedType.append("_2ch");
      LOG_INFO("2ch Atmos case..[%s]", updatedType.c_str());
      event_->NotifyEventContentType(updatedType);
    } else {
      LOG_WARN("Abnormal case[%d]. Do not notify Atmos event.", audio_channel_);
    }
    g_free (content_type);
    return true;
  }
  return false;
}

gboolean VideoPipeline::FDZZ(GstBin* bin, GstElement *element, gpointer data) {
  gchar *element_name = gst_element_get_name(element);
  if (!element_name)
    return true;
  LOG_INFO("%s", element_name);

  if (!no_audio_mode_ && ((nullptr != g_strrstr (element_name, "ocdtsdecoder")) ||
      (nullptr != g_strrstr (element_name, "ocac3decoder")))) {
    gst_media_->SetProperty<gint>(element, "output-channels", audio_channel_);
    LOG_INFO("AC3 or DTS element, output-channels[%d]", audio_channel_);
  } else if (!no_audio_mode_ && (nullptr != g_strrstr (element_name, "dlbac3dec"))) {
    if (audio_channel_ < 6) {
      gst_media_->SetProperty<gboolean>(element, "out-2ch-downmix", true);
    }
    LOG_INFO("LG dlbdec, output-channels[%d]", audio_channel_);
  } else if (!no_audio_mode_ && ((nullptr != g_strrstr (element_name, "dlbparse")) ||
             (nullptr != g_strrstr (element_name, "dlbac3parse")))) {
    NotifyAtmosCallback notify_atmos_callback = std::bind(&VideoPipeline::HandleNotifyAtmos, this,
                                                          std::placeholders::_1,
                                                          std::placeholders::_2,
                                                          std::placeholders::_3,
                                                          std::placeholders::_4);
    gst_media_->RegisterHandleNotifyAtmos(notify_atmos_callback);
    LOG_INFO("LG dlbparse, Atmos notify registered");
    use_atmos_ = true;
  }

  g_free(element_name);
  return true;
}

GValueArray* VideoPipeline::HandleAutoplugSort(GstElement *bin,GstPad *pad,GstCaps *caps,
                                               GValueArray *factories, gpointer data) {
  GstStructure* caps_str = nullptr;
  const gchar* name_str = nullptr;
  if (caps) {
    caps_str = gst_caps_get_structure(caps, 0);
    name_str = gst_structure_get_name(caps_str);
    LOG_DEBUG("cap name - %s\n", name_str);
    if (g_strrstr(name_str, "video") ||
        g_strrstr(name_str, "x-3gp") ||
        g_strrstr(name_str, "vnd.rn-realmedia") ||
        (Conf::GetFeatures(SUPPORT_MJPEG) && g_strrstr(name_str, "image/jpeg"))) {
      LOG_INFO("count - %d \n", video_count_);
      if (video_count_ == 0) {
        check_playback_timer_->Stop(); // Re-setting playback timer
        TimerCallback playback_callback = std::bind(&VideoPipeline::CheckPlayback, this);
        check_playback_timer_->AddCallback(playback_callback, 3200);
        check_playback_timer_->Start();

        loading_timer_->Start();
      }
      video_count_++;
    }
  }

  return nullptr;
}

int VideoPipeline::HandleAutoplugSelect(GstElement *bin,GstPad *pad, GstCaps *caps,
                                        GstElementFactory *factory, gpointer data) {
  GstStructure* caps_str = nullptr;
  const gchar* mime_type = nullptr;
  const gchar* stream_format = nullptr;

  int mpeg_version = 0;
  int select_result = GST_AUTOPLUG_SELECT_TRY;

  gchar* element_name = gst_plugin_feature_get_name(reinterpret_cast<GstPluginFeature*>(factory));
  LOG_INFO("#### gst_plugin_feature_get_name=[%s] ####", element_name);
  if (element_name && caps) {
    caps_str = gst_caps_get_structure(caps, 0);
    mime_type = gst_structure_get_name(caps_str);

    if (g_str_has_prefix(mime_type, "audio/")) {
      if (no_audio_mode_) {
        LOG_INFO("GST_AUTOPLUG_SELECT_SKIP(audio/) mime_type-%s", mime_type);
        select_result = GST_AUTOPLUG_SELECT_SKIP;
      } else {

        int sample_rate = 0;
        if (gst_structure_get_int(caps_str, "rate", &sample_rate)) {
          LOG_DEBUG("samplerate=[%d]", sample_rate);
          if (sample_rate > 0 && !gst_media_->IsLGsrcValidSamplerate(sample_rate)) {
            LOG_ERROR("Unsupported sampling rate=[%d]", sample_rate);
            select_result = GST_AUTOPLUG_SELECT_SKIP;
          }
        }
      }
    }

    if (g_strcmp0(element_name, "beepdec") == 0) {
      stream_format = gst_structure_get_string(caps_str, "stream-format");
      gst_structure_get_int(caps_str, "mpegversion", &mpeg_version);
      if (stream_format && (g_strcmp0(stream_format, "adts") == 0) &&
          mime_type && (g_strcmp0(mime_type, "audio/mpeg") == 0) &&
          (mpeg_version == 2 || mpeg_version == 4)) {
        LOG_INFO("GST_AUTOPLUG_SELECT_SKIP(beepdec) mime_type-%s, stream-format-%s, mpeg-version-%d",
                 mime_type, stream_format, mpeg_version);
        select_result = GST_AUTOPLUG_SELECT_SKIP;
      }
    } else if (g_strcmp0(element_name, "nvmediampeg4viddec") == 0) {
      if (mime_type && (g_strcmp0(mime_type, "video/x-h263") == 0)) {
        LOG_INFO("GST_AUTOPLUG_SELECT_SKIP(nvmediampeg4viddec) mime_type-%s",mime_type);
        select_result = GST_AUTOPLUG_SELECT_SKIP;
      }
    } else if (g_strcmp0(element_name, "jpegdec") == 0){
      if (mime_type && (g_strcmp0(mime_type, "image/jpeg") == 0)) {
        LOG_INFO("GST_AUTOPLUG_SELECT_SKIP(jpegdec) mime_type-%s", mime_type);
        select_result = GST_AUTOPLUG_SELECT_SKIP;
      }
    } else if (g_strcmp0(element_name, "avdec_amrwb") == 0 ||
               g_strcmp0(element_name, "avdec_amrnb") == 0) {
      if (mime_type && g_str_has_prefix(mime_type, "audio/")) {
        LOG_INFO("GST_AUTOPLUG_SELECT_SKIP(amr) mime_type-%s", mime_type);
        select_result = GST_AUTOPLUG_SELECT_SKIP;
      }
    } else if ((g_strcmp0(element_name, "avdec_msmpeg4v2") == 0) ||
               (g_strcmp0(element_name, "avdec_msmpeg4") == 0) ||
               (g_strcmp0(element_name, "avdec_h263") == 0) ||
               (g_strcmp0(element_name, "avdec_mpeg4") == 0)) { // For DivX legacy - avdec_mpeg4
      int width = 0;
      int height = 0;

      if (mime_type && g_str_has_prefix(mime_type, "video/")) {
        LOG_INFO("video sw decoder check=[%s]", mime_type);
        gst_structure_get_int(caps_str, "width", &width);
        gst_structure_get_int(caps_str, "height", &height);

        if (width > 0 && height > 0) {
          int max_resolution = Conf::GetSpec(DIVX_MAX_WIDTH) * Conf::GetSpec(DIVX_MAX_HEIGHT);
          if (width * height > max_resolution) {
            LOG_INFO("exceed max resolution(%d x %d).. skip", width, height);
            select_result = GST_AUTOPLUG_SELECT_SKIP;
          }
        }
      }
    } else if (g_strcmp0(element_name, "sfdec") == 0) {
      if (mime_type && g_str_has_prefix(mime_type, "audio/")) {
        LOG_INFO("GST_AUTOPLUG_SELECT_SKIP(sfdec) mime_type-%s", mime_type);
        select_result = GST_AUTOPLUG_SELECT_SKIP;
      }
    /* added check to skip secure plugin's since it is not currently handled in PE */
    } else if (mime_type && g_str_has_prefix(mime_type, "video/") && g_str_has_prefix(element_name, "nvmwvl1")) {
      LOG_INFO("GST_AUTOPLUG_SELECT_SKIP(nvmwvl1) mime_type-%s", mime_type);
      select_result = GST_AUTOPLUG_SELECT_SKIP;
    }
  }

  return select_result;
}

void VideoPipeline::HandleNoMorePads(GstElement *element, gpointer data) {
  LOG_INFO("");
  loading_timer_->Stop();
  if (pb_info_.is_show_loading) {
    LOG_INFO("show loading complete message");
    event_->NotifyEventWarn(WARNING_LOADING_COMPLETE);
    pb_info_.is_show_loading = false;
  }

  if (video_count_ < 2 ) {
    LOG_ERROR("No Video");
    event_->NotifyEventError();
  }

  ControlAsfContainer(element);
}

void VideoPipeline::HandleBusChangeState(GstMessage* message) {
  GstState old_state = GST_STATE_NULL;
  GstState new_state = GST_STATE_NULL;
  gst_message_parse_state_changed(message, &old_state, &new_state, NULL);
  LOG_INFO("[BUS] GST_MESSAGE_STATE_CHANGED [%s -> %s]", gst_element_state_get_name(old_state),
                                                         gst_element_state_get_name(new_state));
  if (old_state == new_state)
    return;

  switch (new_state) {
    case GST_STATE_NULL: //FALL-THROUGH
    case GST_STATE_READY:
      pb_info_.playback_started = false;
      pb_info_.is_playing_ = false;
      break;
    case GST_STATE_PAUSED:
      pb_info_.is_playing_ = false;
      if (pb_info_.is_external_paused_) {
        event_->NotifyEventPlaybackStatus(STATE_PAUSED);
        pb_info_.is_external_paused_ = false;
      }
      break;
    case GST_STATE_PLAYING:
      if (!pb_info_.playback_started) {
        position_timer_->Start();
        event_->NotifyEventPlaybackStatus(STATE_PLAYING);
        pb_info_.playback_started = true;
      } else if (!pb_info_.is_playing_) {
        event_->NotifyEventPlaybackStatus(STATE_PLAYING);
      } else {
      }
      pb_info_.is_playing_ = true;
      break;
    default:
      break;
  }
}

void VideoPipeline::HandleBusEOS() {
  LOG_INFO("[BUS] GST_MESSAGE_EOS");
  if (!pb_info_.is_load_completed_) {
    LOG_ERROR("load is not finished, but EOS is called. Abnormal case. Ignore");
    return;
  }
  if (pb_info_.playback_rate_ < 0 ) {
    pb_info_.is_bos_ = true;
    event_->NotifyEventBOS();
    return;
  }

  if (pb_info_.mode == PLAYBACK_REPEAT_GAPLESS) {
    LOG_INFO("EOS, but gapless mode - seek to 0");
    if (gst_media_->SeekSimple(0ll)) {
      pb_info_.current_position_ = 0;
    }
    return;
  }

  pb_info_.is_eos_ = true;
  event_->NotifyEventEOS();
  //event_->NotifyEventPlaybackStatus(STATE_DONE);
}

void VideoPipeline::HandleSourceInfo() {
  int i;
  source_info_.can_pause_ = true;
  source_info_.can_play_ = true;
  source_info_.can_seek_ = gst_media_->IsSeekable();
  if (!no_audio_mode_) {
    gst_media_->GetProperty<gint>(gst_media_->GetPipeline(), "n-audio", source_info_.num_of_audio_track_);
    for (i = 0; i < source_info_.num_of_audio_track_; i++) {
      AudioTrack audio;
      const char* temp = gst_media_->GetLanguage(i, "get-audio-tags");

      if (temp != nullptr) {
        audio.format_ = std::string(temp);
      } else {
        audio.format_ = "";
      }
      source_info_.audio_.push_back(audio);
      MI::Get()->audio_stream_count_++;
      MI::Get()->audio_.push_back(MediaFileAudioInfo());
      gst_media_->GetMediaInfo(MI::Get(), i, "get-audio-tags");
      gst_media_->GetMediaInfo(MI::Get(), i, "get-audio-pad");

      if (use_atmos_ && (MI::Get()->audio_[i].audio_codec_id_.find("E-AC-3") != std::string::npos)) {
        int ddp_bitrate = MI::Get()->audio_[i].audio_bitrate_;
        if (ddp_bitrate > 1024000) { // DCX doesn't support over 1024kbps bitrate
          LOG_WARN("DCX not support bitrate=[%d]kbps", ddp_bitrate);
          gst_media_->SetAudioMute(true);
          MI::Get()->warning_reason_ = WARNING_AUDIO_CODEC_NOT_SUPPORTED;
        }
      }
    }
  }

  gst_media_->GetProperty<gint>(gst_media_->GetPipeline(), "n-video", source_info_.num_of_video_track_);
  if (source_info_.num_of_video_track_) {
    gst_media_->GetProperty<gint>(gst_media_->GetPipeline(), "current-video", source_info_.cur_video_track_);
    gst_media_->GetMediaInfo(MI::Get(), source_info_.cur_video_track_, "get-video-tags");
    gst_media_->GetMediaInfo(MI::Get(), source_info_.cur_video_track_, "get-video-pad");
    VideoTrack video;
    char *videoTag = gst_media_->GetLanguage(source_info_.cur_video_track_, "get-video-tags");
    if (videoTag) {
      std::string format_str(videoTag);
      video.format_ = format_str;
    }
    gchar* video_sink = Conf::GetSink(VIDEO_SINK);
    if (strcmp(video_sink,"nvmediaeglwaylandsink") == 0) {
        int video_width = 0;
        int video_height = 0;
        gst_media_->GetProperty<gint>(video_sink_, "width", video_width);
        gst_media_->GetProperty<gint>(video_sink_, "height", video_height);
        video.width_ = video_width;
        video.height_ = video_height;
    } else {
        video.width_ = MI::Get()->video_.width_;
        video.height_ = MI::Get()->video_.height_;
    }

    source_info_.video_.push_back(video);
    MI::Get()->video_stream_count_ = source_info_.num_of_video_track_;
  }

  source_info_.num_of_text_track_ = 0;
#if defined(USE_SUBTITLE)
  if (subtitle_controller_->Load(media_type_)) {
    source_info_.num_of_text_track_ = subtitle_controller_->GetSubtitleLanguageCount();
    for (i = 0; i < source_info_.num_of_text_track_; i++) {
      TextTrack text;
      std::string text_language;
      subtitle_controller_->GetSubtitleLanguageCode(i, text_language);
      text.format_.append(text_language);
      source_info_.text_.push_back(text);
    }
  }
#elif defined(USE_LGE_SUBTITLE)
  HandleSubtitleInfo();
#endif
}

void VideoPipeline::HandleSubtitleInfo() {
#if defined(USE_LGE_SUBTITLE)
  TextTrack text;
  std::vector<std::string>::iterator itr;
  for (itr = subtitle_controller_->external_subtitle_language_list_.begin(); itr != subtitle_controller_->external_subtitle_language_list_.end(); ++itr) {
    text.format_ = *itr;
    source_info_.text_.push_back(text);
    subtitle_index_type_map_.insert(std::pair <int, int>(source_info_.num_of_text_track_, EXTERNAL_SUBTITLE));
    source_info_.num_of_text_track_++;
  }
  subtitle_controller_->subtitle_language_count_ = source_info_.num_of_text_track_;
  if (subtitle_controller_->subtitle_language_count_ == subtitle_controller_->external_subtitle_language_list_.size()
    && !subtitle_controller_->external_subtitle_language_list_.empty()) {
    LOG_INFO("Only external subtitles are available ..loading default subtitle");
    subtitle_controller_->Load(media_type_);
  }
#if 0
  gst_media_->GetProperty<gint>(gst_media_->GetPipeline(), "n-text", source_info_.num_of_text_track_);
  if (source_info_.num_of_text_track_) {
    gst_media_->GetProperty<gint>(gst_media_->GetPipeline(), "current-text", source_info_.cur_text_track_);
    gst_media_->GetMediaInfo(MI::Get(), source_info_.cur_text_track_, "get-text-tags");
    gst_media_->GetMediaInfo(MI::Get(), source_info_.cur_text_track_, "get-text-pad");
    for (int i = 0; i < source_info_.num_of_text_track_; i++) {
      subtitle_index_type_map_.insert(std::pair <int, int>(i, INTERNAL_SUBTITLE));
      text.format_ = gst_media_->GetLanguage(i, "get-text-tags");
      source_info_.text_.push_back(text);
      MI::Get()->text_stream_count_++;
    }
  }
#endif
#endif
}

void VideoPipeline::HandleAsyncDone() {
  LOG_INFO("[BUS] GST_MESSAGE_ASYNC_DONE");
  pb_info_.is_eos_ = false;
  pb_info_.is_bos_ = false;

  event_->NotifyEventAsyncDone();

  if (pb_info_.is_seeking_) {
    pb_info_.is_seeking_ = false;
    gst_media_->SeekDone();
    return;
  }

  if (pb_info_.is_seeking_in_trick_) {
    pb_info_.is_seeking_in_trick_ = false;
    gst_media_->SeekDone();
    return;
  }

  if (!pb_info_.is_load_completed_) {
    MI::Get()->duration_ = pb_info_.duration_ = gst_media_->GetDuration();
    event_->NotifyEventDuration(pb_info_.duration_);

    HandleSourceInfo();

    SupportMediaCreator creator;
    std::shared_ptr<SupportMedia> media;
    creator.CreateParser(media);
    bool not_support_media = false;
    if (MI::Get()->video_stream_count_ == 0) {
      MI::Get()->error_reason_ = ERROR_OPERATION_NOT_SUPPORTED;
      not_support_media = true;
      LOG_ERROR("## No video stream ##");
    } else if (!media->CheckVideo(*MI::Get()) || (!no_audio_mode_ && !media->CheckAudio(*MI::Get()))) {
      not_support_media = true;
      LOG_ERROR("## Not supported video ##");
    } else {
    }

    if (not_support_media) {
      event_->NotifyEventError(MI::Get()->error_reason_);
      event_->NotifyEventSourceInfo(MakeSourceInfoJson(source_info_));
      check_playback_timer_->Stop();
      return;
    }

    //Delayed notify channel info to call SetMode config in hmedia except Atmos case
    if (Conf::GetFeatures(SUPPORT_MULTI_CH) && !no_audio_mode_ &&
      (!bIsDolbyAtmosEacJoc || audio_channel_ == 2)) {
      event_->NotifyEventChannel(media_type_, audio_channel_);
    }

    event_->NotifyEventSourceInfo(MakeSourceInfoJson(source_info_));
    if (MI::Get()->warning_reason_)
      event_->NotifyEventWarn(MI::Get()->warning_reason_);

    //gst_media_->SetTaskPriority("audiosink-ringb", -10);
    pb_info_.is_load_completed_ = true;
    gst_media_->PrintGstDot("Video_PipeLine");

    /*send Ready state to media manager*/
    event_->NotifyEventPlaybackStatus(STATE_READY);
  }
}

gboolean VideoPipeline::HandleBusApplication(GstMessage* message) {
  LOG_INFO("[HandleBusApplication] src(%s)", GST_MESSAGE_SRC_NAME(message));
  return true;
}

gboolean VideoPipeline::HandleBusPipelineMessage(GstMessage* message) {
  GError* err = nullptr;
  GError* warn = nullptr;
  gchar* debug = nullptr;

  switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_STATE_CHANGED:
      HandleBusChangeState(message);
      break;

    case GST_MESSAGE_EOS:
      HandleBusEOS();
      break;

    case GST_MESSAGE_ERROR:
      gst_message_parse_error(message, &err, &debug);
      event_->NotifyEventError(ERROR_GST_INTERNAL_ERROR);
      LOG_ERROR("[BUS] GST_MESSAGE_ERROR : %s", err->message);
      break;

    case GST_MESSAGE_WARNING:
      gst_message_parse_warning(message, &warn, &debug);
      LOG_INFO("[BUS] GST_MESSAGE_WARNING : %s", warn->message);
      break;

    case GST_MESSAGE_ASYNC_DONE:
      HandleAsyncDone();
      break;

    default:
      break;
  }

  if (err)
    g_error_free(err);
  if (warn)
    g_error_free(warn);
  if (debug)
    g_free(debug);
  return true;
}

gboolean VideoPipeline::HandleBusElementMessage(GstMessage* message) {
  GError* warn = nullptr;
  gchar* debug = nullptr;

  if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_WARNING) {
    gst_message_parse_warning(message, &warn, &debug);
    if (!warn || !debug)
      goto EXIT;

    if (warn->code == (int)GST_STREAM_ERROR_CODEC_NOT_FOUND) {
      if (strstr(warn->message, "video")) {
        LOG_ERROR("[HandleBusElementMessage] not supported video");
        event_->NotifyEventError(ERROR_VIDEO_CODEC_NOT_SUPPORTED);
      }
      if (!no_audio_mode_) {
        if (strstr(warn->message, "audio")) {
          LOG_ERROR("[HandleBusElementMessage] not supported audio");
          event_->NotifyEventWarn(WARNING_AUDIO_CODEC_NOT_SUPPORTED);
        }
        if (strstr(warn->message, "Unknown Audio")) {
          LOG_WARN("[HandleBusElementMessage] not supported audio");
          event_->NotifyEventWarn(WARNING_AUDIO_CODEC_NOT_SUPPORTED);
        }
      }
    } else if((warn->code == GST_STREAM_ERROR_NOINDEX) ||
        (warn->code == GST_STREAM_ERROR_TOOBIG)){
        check_playback_timer_->Stop();
        TimerCallback playback_callback = std::bind(&VideoPipeline::CheckPlayback, this);
        int interval = 30000;
        check_playback_timer_->AddCallback(playback_callback, interval);
        LOG_INFO("Re-register playback timer as 30sec");
        check_playback_timer_->Start();

    }
    goto EXIT;
  }
  gst_media_->PrintGstDot("ERROR_V_PIPELINE");

  if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
    HandleBusElementErrorMessage(message);
  else
    return true;

EXIT:
  if (warn)
    g_error_free(warn);
  if (debug)
    g_free(debug);
  return true;
}

gboolean VideoPipeline::HandleBusElementErrorMessage(GstMessage* message) {
  gboolean ret = false;
  GError* err = nullptr;
  gchar* debug = nullptr;

  gst_message_parse_error(message, &err, &debug);
  if (!err || !debug)
    goto EXIT;

  LOG_ERROR("[HandleBusElementMessage] GST_MESSAGE_ERROR : %s - %d(from %s), %s, %s",
            (err->domain == GST_STREAM_ERROR ? "GST_STREAM_ERROR" :
            (err->domain == GST_CORE_ERROR ? "GST_CORE_ERROR" :
            (err->domain == GST_RESOURCE_ERROR ? "GST_RESOURCE_ERROR" :
            (err->domain == GST_LIBRARY_ERROR ? "GST_LIBRARY_ERROR" : "UNKNOWN")))),
            err->code, GST_MESSAGE_SRC_NAME(message), err->message, debug);

  if (err->code == (int)GST_RESOURCE_ERROR_READ &&
      strstr(GST_MESSAGE_SRC_NAME(message), "source")) {
    event_->NotifyEventError(ERROR_GST_RESOURCE_ERROR_READ);
  } else if(err->code == (int) GST_STREAM_ERROR_AUDIOSHORT && !no_audio_mode_) {
    event_->NotifyEventError(ERROR_STREAM_AUDIO_SHORT);
    loading_timer_->Stop();
  } else {
    event_->NotifyEventError(ERROR_GST_INTERNAL_ERROR);
  }
  check_playback_timer_->Stop(); // Error event is already sent to application.
  ret = true;

EXIT:
  if (err)
    g_error_free(err);
  if (debug)
    g_free(debug);
  return ret;
}

gboolean VideoPipeline::HandleTrickPlay() {
  const gint64 interval = 500000000;
  bool internal_ret = false;
  gint64 backup_trick_position = pb_info_.trick_position_;

  if (pb_info_.playback_rate_ < 0) {
    pb_info_.trick_position_ += (gint64)(interval)*pb_info_.playback_rate_;
    if (!pb_info_.is_bos_) {
      if (pb_info_.trick_position_ <= 0) {
        event_->NotifyEventBOS();
        return false;
      }
      internal_ret = TrickPlayInternal(pb_info_.trick_position_/GST_MSECOND);
    }
    else {
      pb_info_.trick_position_ = pb_info_.current_position_ = 0;
      return false;
    }
  } else if (fabs(pb_info_.playback_rate_ - 1.0) <= DBL_EPSILON) {
    return false;
  } else { /* pb_info_.playback_rate_ > 1.0 */
    pb_info_.trick_position_ += (gint64)(interval)*pb_info_.playback_rate_;
    if (!pb_info_.is_eos_) {
      if (pb_info_.trick_position_ >= pb_info_.duration_) {
        event_->NotifyEventEOS();
        return false;
      }
      internal_ret = TrickPlayInternal(pb_info_.trick_position_/GST_MSECOND);
    }
    else {
      pb_info_.trick_position_ = pb_info_.current_position_ = pb_info_.duration_;
      return false;
    }
  }

  pb_info_.playback_rate_count_++;
  if (pb_info_.playback_rate_count_ % (GST_SECOND / interval) == 0) {
      if(internal_ret){
        pb_info_.current_position_ = pb_info_.trick_position_;
        event_->NotifyEventCurrentPosition(pb_info_.current_position_);
      }
      else {
          pb_info_.trick_position_ = backup_trick_position;
          pb_info_.playback_rate_count_ = pb_info_.playback_rate_count_ - 1;
      }
  }

  return true;
}

void VideoPipeline::HandleSeekControl(int message) {
  pb_info_.is_eos_ = false;
  pb_info_.is_bos_ = false;
  event_->NotifyEventAsyncDone(true);
  pb_info_.is_seeking_ = false;
  pb_info_.is_seeking_in_trick_ = false;
}

} // namespace genivimedia
