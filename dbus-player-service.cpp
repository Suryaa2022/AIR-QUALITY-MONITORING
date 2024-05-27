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

#include "service/dbus_player_service.h"

#include <string>
#include <iostream>

#include "logger/player_logger.h"
#include "player/media_player.h"

namespace genivimedia {

typedef std::function < void (const std::string& data) > EventCallbackHandler;

std::vector<std::pair<const char*, GCallback>> DBusPlayerService::handler_table_ = {
  { "handle-play",                    G_CALLBACK(DBusPlayerService::Play) },
  { "handle-pause",                   G_CALLBACK(DBusPlayerService::Pause) },
  { "handle-stop",                    G_CALLBACK(DBusPlayerService::Stop) },
  { "handle-fast-forward",            G_CALLBACK(DBusPlayerService::FastForward) },
  { "handle-rewind",                  G_CALLBACK(DBusPlayerService::Rewind) },
  { "handle-set-playback-speed",      G_CALLBACK(DBusPlayerService::SetPlaybackSpeed) },
  { "handle-set-uri",                 G_CALLBACK(DBusPlayerService::SetURI) },
  { "handle-set-position",            G_CALLBACK(DBusPlayerService::SetPosition) },
  { "handle-stop-rate-change",        G_CALLBACK(DBusPlayerService::StopRateChange) },
  { "handle-set-subtitle-enable",     G_CALLBACK(DBusPlayerService::SetSubtitleEnable) },
  { "handle-set-subtitle-language",   G_CALLBACK(DBusPlayerService::SetSubtitleLanguage) },
  { "handle-set-subtitle-language-index",   G_CALLBACK(DBusPlayerService::SetSubtitleLanguageIndex) },
  { "handle-set-audio-language",      G_CALLBACK(DBusPlayerService::SetAudioLanguage) },
  { "handle-set-audio-mute",          G_CALLBACK(DBusPlayerService::SetAudioMute) },
  { "handle-set-avoffset",           G_CALLBACK(DBusPlayerService::SetAVoffset) },
  { "handle-set-audio-volume",        G_CALLBACK(DBusPlayerService::SetAudioVolume) },
  { "handle-switch-channel",      G_CALLBACK(DBusPlayerService::SwitchChannel) },
  { "handle-set-video-window",        G_CALLBACK(DBusPlayerService::SetVideoWindow) },
  {"handle-set-video-brightness",     G_CALLBACK(DBusPlayerService::SetVideoBrightness)},
  {"handle-set-video-contrast",     G_CALLBACK(DBusPlayerService::SetVideoContrast)},
  {"handle-set-video-saturation",     G_CALLBACK(DBusPlayerService::SetVideoSaturation)},
  {"handle-get-channel-info",      G_CALLBACK(DBusPlayerService::GetChannelInfo)}
};

ComLgePlayerEngine* DBusPlayerService::skeleton_ = nullptr;

DBusPlayerService::DBusPlayerService()
  : player_(new MediaPlayer()),
    loop_(nullptr),
    instance_number_(0),
    gbus_id_(0),
    connection_id_(nullptr)
{

  EventCallbackHandler callback = std::bind(&DBusPlayerService::HandleEvent,
                                            std::placeholders::_1);
  player_->RegisterCallback(callback);
  MMLogInfo("DBusPlayerService");
}

DBusPlayerService::~DBusPlayerService() {
  if (player_)
    delete player_;
}

gboolean DBusPlayerService::Play(ComLgePlayerEngine *skeleton,
                                 GDBusMethodInvocation *invocation,
                                 gpointer user_data) {
  MMLogInfo("Play");
  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  bool result = instance->player_->Play();
  com_lge_player_engine_complete_play(skeleton, invocation, result);
  return result;
}

gboolean DBusPlayerService::Pause(ComLgePlayerEngine *skeleton,
                                  GDBusMethodInvocation *invocation,
                                  gpointer user_data) {
  MMLogInfo("Pause");
  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  bool result = instance->player_->Pause();
  com_lge_player_engine_complete_pause(skeleton, invocation,result);
  return result;
}

gboolean DBusPlayerService::Stop(ComLgePlayerEngine *skeleton,
                                 GDBusMethodInvocation *invocation,
                                 gpointer user_data) {
  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  bool result = instance->player_->Stop();
  com_lge_player_engine_complete_stop(skeleton, invocation, result);
  return result;
}

gboolean DBusPlayerService::FastForward(ComLgePlayerEngine *skeleton,
                                        GDBusMethodInvocation *invocation,
                                        gdouble step,
                                        gpointer user_data) {
  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  bool result = instance->player_->FastForward(step);
  com_lge_player_engine_complete_fast_forward(skeleton, invocation, result);
  return result;
}

gboolean DBusPlayerService::Rewind(ComLgePlayerEngine *skeleton,
                                   GDBusMethodInvocation *invocation,
                                   gdouble step,
                                   gpointer user_data) {
  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  bool result = instance->player_->Rewind(step);
  com_lge_player_engine_complete_rewind(skeleton, invocation, result);
  return result;
}

gboolean DBusPlayerService::SetPlaybackSpeed(ComLgePlayerEngine *skeleton,
                                   GDBusMethodInvocation *invocation,
                                   gdouble speed,
                                   gpointer user_data) {
  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  bool result = instance->player_->SetPlaybackSpeed(speed);
  com_lge_player_engine_complete_set_playback_speed(skeleton, invocation, result);
  return result;
}

gboolean DBusPlayerService::SetURI(ComLgePlayerEngine *skeleton,
                                   GDBusMethodInvocation *invocation,
                                   gchar* uri,
                                   gchar* option,
                                   gpointer user_data) {
  MMLogInfo("SetURI : uri: %s ,option : %s", uri, option);

  if (uri == NULL || option == NULL) {
    return false;
  }

  //char slot = -1;
  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  //std::string str(uri);
  //if (str.length() > 2 && uri[0] == '[' && uri[2] == ']') {
  //  slot = uri[1];
  //  uri += 3;
  //}
  bool result = instance->player_->SetURI(uri, option);
  com_lge_player_engine_complete_set_uri(skeleton, invocation, result);
  return result;
}

gboolean DBusPlayerService::SetPosition(ComLgePlayerEngine *skeleton,
                                        GDBusMethodInvocation *invocation,
                                        gint64 position,
                                        gpointer user_data) {
    MMLogInfo("SetPosition[%lld]", position);
  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  bool result = instance->player_->SetPosition(position);
  com_lge_player_engine_complete_set_position(skeleton, invocation, result);
  return result;
}

gboolean DBusPlayerService::StopRateChange(ComLgePlayerEngine *skeleton,
                                           GDBusMethodInvocation *invocation,
                                           gpointer user_data) {
  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  bool result = instance->player_->StopRateChange();
  com_lge_player_engine_complete_set_position(skeleton, invocation, result);
  return result;
}

gboolean DBusPlayerService::SetSubtitleEnable(ComLgePlayerEngine *skeleton,
                                              GDBusMethodInvocation *invocation,
                                              gboolean show,
                                              gpointer user_data) {
  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  bool result = instance->player_->SetSubtitleEnable(show);
  com_lge_player_engine_complete_set_subtitle_enable(skeleton, invocation, result);
  return result;
}

gboolean DBusPlayerService::SetSubtitleLanguage(ComLgePlayerEngine *skeleton,
                                                GDBusMethodInvocation *invocation,
                                                gchar* language,
                                                gpointer user_data) {
  if (language == NULL) {
    return false;
  }

  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  bool result = instance->player_->SetSubtitleLanguage(language);
  com_lge_player_engine_complete_set_subtitle_language(skeleton, invocation, result);
  return result;
}

gboolean DBusPlayerService::SetSubtitleLanguageIndex(ComLgePlayerEngine *skeleton,
                                                GDBusMethodInvocation *invocation,
                                                gint32 language,
                                                gpointer user_data) {
  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  bool result = instance->player_->SetSubtitleLanguageIndex(language);
  com_lge_player_engine_complete_set_subtitle_language_index(skeleton, invocation, result);
  return result;
}

gboolean DBusPlayerService::SetAudioLanguage(ComLgePlayerEngine *skeleton,
                                             GDBusMethodInvocation *invocation,
                                             gint32 index,
                                             gpointer user_data) {
  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  bool result = instance->player_->SetAudioLanguage(index);
  com_lge_player_engine_complete_set_audio_language(skeleton, invocation, result);
  return result;
}

gboolean DBusPlayerService::SetAudioMute(ComLgePlayerEngine *skeleton,
                                         GDBusMethodInvocation *invocation,
                                         gboolean mute,
                                         gpointer user_data) {
  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  bool result = instance->player_->SetAudioMute(mute);
  com_lge_player_engine_complete_set_audio_mute(skeleton, invocation, result);
  return result;
}

gboolean DBusPlayerService::SetAVoffset(ComLgePlayerEngine *skeleton,
                                        GDBusMethodInvocation *invocation,
                                        gint delay,
                                        gpointer user_data) {
  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  bool result = instance->player_->SetAVoffset(delay);
  com_lge_player_engine_complete_set_avoffset(skeleton, invocation, result);
  return result;
}

gboolean DBusPlayerService::SetAudioVolume(ComLgePlayerEngine *skeleton,
                                           GDBusMethodInvocation *invocation,
                                           gdouble volume,
                                           gpointer user_data) {
  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  bool result = instance->player_->SetAudioVolume(volume);
  com_lge_player_engine_complete_set_audio_mute(skeleton, invocation, result);
  return result;
}

gboolean DBusPlayerService::SwitchChannel(ComLgePlayerEngine *skeleton,
                                           GDBusMethodInvocation *invocation,
                                           gboolean downmix,
                                           gpointer user_data) {
  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  bool result = instance->player_->SwitchChannel(downmix);
  com_lge_player_engine_complete_switch_channel(skeleton, invocation, result);
  return result;
}

gboolean DBusPlayerService::SetVideoWindow(ComLgePlayerEngine *skeleton,
                                           GDBusMethodInvocation *invocation,
                                           gchar* info,
                                           gpointer user_data) {
  if (info == NULL) {
    return false;
  }
  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  bool result = instance->player_->SetVideoWindow(info);
  com_lge_player_engine_complete_set_video_window(skeleton, invocation, result);
  return result;
}
gboolean DBusPlayerService::SetVideoBrightness(ComLgePlayerEngine *skeleton,
                            GDBusMethodInvocation *invocation,
                            gdouble brightness,
                            gpointer user_data){

    DBusPlayerService* instance = (DBusPlayerService*)user_data;
    bool result = true;

    result = instance->player_->SetVideoBrightness((gfloat)brightness);

    com_lge_player_engine_complete_set_video_brightness(skeleton, invocation,  result);
    return result;
}

gboolean DBusPlayerService::SetVideoContrast(ComLgePlayerEngine *skeleton,
                            GDBusMethodInvocation *invocation,
                            gdouble contrast,
                            gpointer user_data){

    DBusPlayerService* instance = (DBusPlayerService*)user_data;
    bool result = true;

    result = instance->player_->SetVideoContrast((float)contrast);

    com_lge_player_engine_complete_set_video_contrast(skeleton, invocation,  result);
    return result;
}

gboolean DBusPlayerService::SetVideoSaturation(ComLgePlayerEngine *skeleton,
                            GDBusMethodInvocation *invocation,
                            gdouble saturation,
                            gpointer user_data){

    DBusPlayerService* instance  = (DBusPlayerService*)user_data;
    bool result = true;

    result = instance->player_->SetVideoSaturation((gfloat)saturation);

    com_lge_player_engine_complete_set_video_saturation(skeleton, invocation, result);
    return result;
}
gboolean DBusPlayerService::GetChannelInfo(ComLgePlayerEngine *skeleton,
                            GDBusMethodInvocation *invocation,
                            gchar* uri,
                            gchar* option,
                            gpointer user_data){
    DBusPlayerService* instance  = (DBusPlayerService*)user_data;
    gint result = 2;

    result = instance->player_->GetChannelInfo(uri, option);

    com_lge_player_engine_complete_get_channel_info(skeleton, invocation, result);
    return true;
}

void DBusPlayerService::HandleEvent(const std::string& data) {
  com_lge_player_engine_emit_state_change(skeleton_, data.c_str());
}

void DBusPlayerService::OnDBusNameLost(GDBusConnection *connection, const gchar *name, gpointer user_data) {

    DBusPlayerService* instance = (DBusPlayerService*)user_data;

    LOG_INFO("");
    g_signal_handlers_disconnect_by_data(skeleton_,  user_data);

    g_dbus_interface_skeleton_unexport_from_connection(G_DBUS_INTERFACE_SKELETON(skeleton_),
                                                        instance->connection_id_);

}

void DBusPlayerService::OnBusNameAcquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {
  MMLogInfo("OnBusNameAcquired called, name[%s]", name);
}

void DBusPlayerService::OnDBusNameAcquired(GDBusConnection *connection, const gchar *name, gpointer user_data) {

  GError *err = NULL;
  MMLogInfo("OnDBusNameAcquired called");

  if (!skeleton_)
    skeleton_ = com_lge_player_engine_skeleton_new();

  DBusPlayerService* instance = (DBusPlayerService*)user_data;
  instance->connection_id_ = connection;
  for (auto& handler : instance->handler_table_)
    g_signal_connect(skeleton_, std::get<0>(handler), std::get<1>(handler), user_data);

  if (g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(skeleton_),
                                   connection,
                                   "/com/lge/PlayerEngine",
                                   &err) == true) {
  
    MMLogInfo("player-engine skeleton created");
  }else {
    if (err != NULL) {
      MMLogInfo("player-engine skeleton failed, [%s]", err->message);
      g_error_free(err);
    }
  }
}

bool DBusPlayerService::Run(void) {
  guint owner_id = 0;

  MMLogInfo("g_main_loop_new called");
  loop_ = g_main_loop_new(NULL, false);
  GBusNameOwnerFlags flags;
  flags = (GBusNameOwnerFlags)(G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT | G_BUS_NAME_OWNER_FLAGS_REPLACE);
  owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
                 "com.lge.PlayerEngine",
                 flags,
                 OnBusNameAcquired,
                 OnDBusNameAcquired,
                 NULL,
                 this,
                 NULL);

  MMLogInfo("g_bus_own_name called, id[%u]", owner_id);

  g_main_loop_run(loop_);
  g_main_loop_unref(loop_);

  g_bus_unown_name(owner_id);

  return true;
}

bool DBusPlayerService::Exit(void) {

  bool result = true;


  result = player_->QuitPlayerEngine();

  g_bus_unown_name(gbus_id_);
  g_main_loop_quit(loop_);

  return result;
}

} // namespace genivimedia

