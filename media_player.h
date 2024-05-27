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

#ifndef GENIVIMEDIA_MEDIA_PLAYER_H
#define GENIVIMEDIA_MEDIA_PLAYER_H

#include <memory>

#include <glib.h>
#include <gst/gst.h>

#include "player/audio_controller.h"
#include "player/player_interface.h"

#include "player/pipeline/timer.h"
#include "player/pipeline/common.h"

namespace genivimedia {

class Pipeline;
class PipelineCreator;

/**
 * @class      genivimedia::MediaPlayer
 * @brief      The MediaPlayer class derived from IPlayer class controls Pipeline instance.
 * @details    Member functions provided by MediaPlayer class perform the following actions.
 *             <ul>
 *                 <li>Creates and destroys a Pipeline instance.
 *                 <li>Controls a Pipeline instance for playback controls such as play, pause, seek, playback rate.
 *                 <li>Controls a Pipeline instance for controlling audio track, audio volume, and audio mute/unmute.
 *                 <li>Controls a Pipeline instance for controlling subtitle track, and visibility.
 *                 <li>Controls a Pipeline instance for controlling video window size.
 *             </ul>
 * @see        genivimedia::IPlayer genivimedia::Pipeline genivimedia::PipelineCreator genivimedia::KeepAlive
 */

class MediaPlayer : public IPlayer {
 public:
  /**
   * @fn MediaPlayer
   * @brief Constructor
   * @section function_flow Function Flow :
   * - Creates PipelineCreator instance to create Pipeline instance.
   * - Creates Null Pipeline to handle invoked playback methods.
   * - Loads configuration file provided by platform.
   * - Checks GStreamer initialization, initialize GStreamer library if not initialized.
   * - Load default audio/videosink plugin of GStreamer.
   * - Set rank of GStreamer plugins.
   * - Creates KeepAlive instance.
   *
   * @param : None
   * @section global_variable_none Global Variables : None
   * @section dependency_none Dependencies : None
   * @return None
   */
  MediaPlayer();

  /**
   * @fn ~MediaPlayer
   * @brief Destructor.
   * @section function_flow Function Flow :
   * - Destroys KeepAlive instance.
   *
   * @param : None
   * @section global_variable_none Global Variables : None
   * @section dependency_none Dependencies : None
   * @return None
   */
  virtual ~MediaPlayer();


  /**
   * @fn RegisterCallback
   * @brief Registers callback function.
   * @section function_flow Function Flow :
   * - Stores the given callback function.
   * - Calls RegisterCallback() of Pipeline instance.
   *
   * @param[in] callback : callback function
   * @section global_variable_none Global Variables : None
   * @section dependency_none Dependencies : None
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool RegisterCallback(std::function <void (const std::string& data)> callback);

  /**
   * @fn Play
   * @brief Plays media contents.
   * @section function_flow Function Flow
   * - Plays by controlling a Pipeline instance.
   *
   * @param : None
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - SetURI()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool Play();

  /**
   * @fn Pause
   * @brief Pauses media contents.
   * @section function_flow Function Flow :
   * - Pauses by controlling a  Pipeline instance.
   *
   * @param : None
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - SetURI()
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool Pause();

  /**
   * @fn Stop
   * @brief Unloads media contents.
   * @section function_flow Function Flow :
   * - Destroys Pipeline instance which is currently used.
   * - Creates NullPipeline instance.
   * - Calls RegisterCallback() of Pipeline instance.
   *
   * @param : None
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - SetURI()
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool Stop();

  /**
   * @fn FastForward
   * @brief Changes playback rate of media pipeline in forward direction.
   * @section function_flow Function Flow :
   * - Changes playback rate by controlling Pipeline instance.
   *
   * @param[in] step <4.0, 20.0> : playback rate
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool FastForward(double step);

  /**
   * @fn Rewind
   * @brief Changes playback rate of media pipeline in reverse direction.
   * @section function_flow Function Flow :
   * - Changes playback rate by controlling Pipeline instance.
   *
   * @param[in] step <-4.0, -20.0> : playback rate
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool Rewind(double step);

  /**
   * @fn bool SetURI(const std::string& uri, int media_type)
   * @brief Loads media contents with the given uri.<br>
   * - If a pipeline is loaded, it will destroy and creates new pipeline.<br>
   * - The given uri string shall have one of the following prefix: file://, thumbnail://, and dvd://.<br>
   *
   * @section function_flow Function Flow :
   * - It creates a concrete Pipeline instance.
   * - If a Pipeline instance is loaded, it destroys its instance, and creates new Pipeline instance.
   * - Loads media content by controlling Pipeline instance with the given uri.
   *
   * @param[in] uri: uri string of media content
   * @param[in] media_type <1~5> : enum type of @link common_header MediaType @endlink<br>
   * => TYPE_AUDIO(1)<br>
   * => TYPE_VIDEO(2)<br>
   * => TYPE_DVD(3)<br>
   * => TYPE_AUX(4)<br>
   * => TYPE_THUMBNAIL(5)<br>
   *
   * @section global_variable_none Global Variables : None
   * @section dependency_none Dependencies : None
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetURI(const std::string& uri, int media_type);

  /**
   * @fn bool SetURI(const std::string& uri, const std::string& option)
   * @brief Loads media contents with the given uri, and option string.<br>
   * - The given option string shall be JSON format like {"Option":{"filename": "filename", "mediatype": "video", "show-preroll-frame":boolean}}.<br>
   * - This method is used to save thumbnail with the give filename.<br>
   * - This method is used to control show preroll frame the given show-preroll-frame.<br>
   *
   * @section function_flow Function Flow :
   * - It parses media type from the given option string.
   * - It creates a concrete Pipeline instance.
   * - If a Pipeline instance is loaded, it destroys its instance, and creates new Pipeline instance.
   * - Loads media content by controlling Pipeline instance with the given uri, and option.
   *
   * @param[in] uri: uri string of media content
   * @param[in] option: option string in JSON format
   * @section global_variable_none Global Variables : None
   * @section dependency_none Dependencies : None
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetURI(const std::string& uri, const std::string& option);

  /**
   * @fn SetPosition
   * @brief Seeks media content to the specified position.
   * @section function_flow Function Flow :
   * - Seeks media content by controlling Pipeline instance.
   *
   * @param[in] position <0~duration of media content>: seek position in milliseconds
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetPosition(gint64 position);

  /**
   * @fn StopRateChange
   * @brief Stops playback rate change of media contents.
   * @section function_flow Function Flow :
   * - Stops playback rate change by controlling Pipeline instance.
   *
   * @param : None
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - FastForward()
   * - Rewind()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool StopRateChange();

  /**
   * @fn SetSubtitleEnable
   * @brief Sets visibility of loaded subtitle.
   * @section function_flow Function Flow :
   * - Sets visibility by controlling Pipeline instance.
   *
   * @param[in] show <0,1>: value of subtitle visibility
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetSubtitleEnable(bool show);

  /**
   * @fn SetSubtitleLanguage
   * @brief Sets language with the given language code of loaded subtitle.
   * @section function_flow Function Flow :
   * - Sets subtitle language by controlling Pipeline instance.
   *
   * @param[in] language : language code string of subtitle<br>
   * => index   |   language<br>
   * => 0       |   "eng"   <br>
   * => 1       |   "kor"   <br>
   * => 2       |   "jpn"   <br>
   * => 3       |   "zho"   <br>
   * => 4       |   "deu"   <br>
   * => 5       |   "spa"   <br>
   * => 6       |   "fra"   <br>
   * => 7       |   "ita"   <br>
   * => 8       |   "nld"   <br>
   * => 9       |   "por"   <br>
   * => 10      |   "rus"   <br>
   * => 11      |   "tur"   <br>
   * => 12      |   "subtitle1"<br>
   *
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetSubtitleLanguage(const std::string& language);

  /**
   * @fn SetSubtitleLanguageIndex
   * @brief Sets language with the given index of loaded subtitle.
   * @section function_flow Function Flow :
   * - Sets subtitle language by controlling Pipeline instance.
   *
   * @param[in] language <0~12>: index of subtitle language code<br>
   * => index   |   language<br>
   * => 0       |   "eng"   <br>
   * => 1       |   "kor"   <br>
   * => 2       |   "jpn"   <br>
   * => 3       |   "zho"   <br>
   * => 4       |   "deu"   <br>
   * => 5       |   "spa"   <br>
   * => 6       |   "fra"   <br>
   * => 7       |   "ita"   <br>
   * => 8       |   "nld"   <br>
   * => 9       |   "por"   <br>
   * => 10      |   "rus"   <br>
   * => 11      |   "tur"   <br>
   * => 12      |   "subtitle1"<br>
   *
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetSubtitleLanguageIndex(int language);

  /**
   * @fn SetAudioLanguage
   * @brief Sets audio language with the given index.
   * @section function_flow Function Flow :
   * - Sets audio language by controlling Pipeline instance.
   *
   * @param[in] index <0~one less than a number of audio tracks>: index of audio language
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetAudioLanguage(int index);

  /**
   * @fn SetPlaybackSpeed
   * @brief Sets playback speed.
   * @section function_flow Function Flow :
   * - Sets playback speed by controlling Pipeline instance.
   *
   * @param[in] rate : rate value
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetPlaybackSpeed (double rate);

  /**
   * @fn SetAudioMute
   * @brief Sets audio mute/unmute.
   * @section function_flow Function Flow :
   * - Sets audio language by controlling Pipeline instance.
   *
   * @param[in] mute <0,1>: mute/unmute value
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetAudioMute(bool mute);

     /**
   * @fn SetAVoffset
   * @brief Sets AV offset.
   * @section function_flow Function Flow : None
   * @param[in] delay : synchronisation offset between A/V streams
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetAVoffset(int delay);

  /**
   * @fn SetAudioVolume
   * @brief Sets audio volume.
   * @section function_flow Function Flow :
   * - Sets audio volume by controlling Pipeline instance.
   *
   * @param[in] volume <0.0~10.0>: audio volume
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetAudioVolume(double volume);

  /**
   * @fn SwitchChannel
   * @brief Switch audio channel(downmix).
   * @section function_flow Function Flow : None
   *
   * @param[in] downmix : enable downmix flag
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SwitchChannel(bool downmix);

  /**
   * @fn SetVideoWindow
   * @brief Sets video window information to change video window size.
   * - video window inforamtion string shall be JSON format like "ivi-surface-info,ivi-surface-id=(int)value, window-x-coord=(int)value,window-y-coord=(int)value,window-width=(uint)value,window-height=(uint)value"
   *
   * @section function_flow Function Flow :
   * - Parses video window informations from the given JSON string.
   * - Returns false if the given JSON string is invalid.
   * - Stores surface, aspect ratio information.
   * - Sets video window information by controlling Pipeline instance.
   *
   * @param[in] info: video window information string in JSON format
   * @section global_variable Global Variables :
   * - gin kAspectRatio : std::map<std::string, int> which contains aspect ratio information.
   *
   * @section dependency Dependencies :
   * - SetURI()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetVideoWindow(const std::string& info);

  virtual bool SetVideoSaturation(float saturation);

  virtual bool SetVideoBrightness(float brightness);

  virtual bool SetVideoContrast(float contrast);

  virtual int GetChannelInfo(const std::string& uri, const std::string& option);

  virtual bool QuitPlayerEngine();

 protected:
  /**
   * @fn CreatePipeline
   * @brief Creates concrete Pipeline instance with the given media type.
   * @section function_flow Function Flow :
   * - Creates concrete Pipeline instance by using PipelineCreator instance.
   * - Creates NullPipeline instance by default if proper media type is not set.
   *
   * @param[in] media_type <0~5> : enum type of @link common_header MediaType @endlink<br>
   * => TYPE_UNSUPPORTED(0)<br>
   * => TYPE_AUDIO(1)<br>
   * => TYPE_VIDEO(2)<br>
   * => TYPE_DVD(3)<br>
   * => TYPE_AUX(4)<br>
   * => TYPE_THUMBNAIL(5)<br>
   *
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - SetURIInternal()
   *
   * @return None
   */
  void CreatePipeline(int media_type, const std::string& uri="");

  bool ReleasePipeline();

  /**
   * @fn SetURIInternal
   * @brief Destroys current Pipeline instance, and Creates a new Pipeline instance.
   * @section function_flow Function Flow :
   * - Unloads current Pipeline instance.
   * - Creates Pipeline instance with the media type, and registers callback function to Pipeline.
   * - Sets video window information by controlling Pipeline instance.
   *
   * @param[in] uri: uri string of media content
   * @param[in] media_type <0~5> : enum type of @link common_header MediaType @endlink<br>
   * => TYPE_UNSUPPORTED(0)<br>
   * => TYPE_AUDIO(1)<br>
   * => TYPE_VIDEO(2)<br>
   * => TYPE_DVD(3)<br>
   * => TYPE_AUX(4)<br>
   * => TYPE_THUMBNAIL(5)<br>
   *
   * @section global_variable Global Variables :
   * - ginout MI::info_.error_reason_ : enum type of @link common_header MediaPlaybackError @endlink of MediaFileInfo instance
   *
   * @section dependency Dependencies :
   * - SetURI()
   *
   * @return None
   */
  void SetURIInternal(const std::string& uri, int media_type = 0);

  /**
   * @fn MediaPlayerInit
   * @brief Read player engine configuration and Gstreamer Initialize.
   * @section function_flow Function Flow :
   * - Gstreamer Initialize.
   *
   * @param : None
   * @section global_variable_none Global Variables : None
   *
   * @section dependency Dependencies :
   * - Play()
   * - Pause()
   * - Stop()
   * - FastForward()
   * - Rewind()
   * - SetURI()
   * - SetPosition()
   * - StopRateChange()
   * - SetSubtitleEnable()
   * - SetSubtitleLanguage()
   * - SetSubtitleLanguageIndex()
   * - SetAudioLanguage()
   * - SetAudioMute()
   * - SetAudioVolume()
   * - SetVideoWindow()
   *
   * @return None
   */
  void MediaPlayerInit();

  void fadeIn(const int ms = 100);
  void fadeOut(const int ms = 100);

  gboolean updateTimerFlag();
  static void PrintGstLog(GstDebugCategory* category, GstDebugLevel level,
                                const char* file, const char* function, 
                                gint line, GObject* object, GstDebugMessage *message,
                                gpointer pointer);

  std::string surface_info_; /**< standard string to store video window information */
  std::string media_type_str_; /**< standard string to store media type information */
  std::shared_ptr<Pipeline> pipeline_; /**< Pipeline instance */
  std::shared_ptr<PipelineCreator> creator_; /**< PipelineCreator instance */
  std::shared_ptr<AudioController> audio_controller_; /**< AudioController instance */
  Timer* start_timer_; /** playback start timer - called after 1 sec for adding fade out when staring playback*/
  std::function <void (const std::string& data)> callback_; /**< Callback function */

  bool need_fade_out_;
  bool need_fade_in_;
  bool media_init_flag_; /**< flag of gstreamer initialized or not */
  int aspect_ratio_; /**< aspect ratio value */
  int media_type_; /**< enum type of @link common_header MediaType @endlink<br> */
};

}  // namespace genivimedia

#endif // GENIVIMEDIA_MEDIA_PLAYER_H

