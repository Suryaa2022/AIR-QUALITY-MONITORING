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

#ifndef GENIVIMEDIA_PLAYER_INTERFACE_H
#define GENIVIMEDIA_PLAYER_INTERFACE_H

/** @file player_interface.h
 *  @brief  Header for abstract IPlayer class to support media playback operation.
 *  @author JaeGu, Yoon jaegu.yoon@lge.com
 *  @version 1.0
 **/

#include <functional>
#include <glib.h>

namespace genivimedia {

/**
 * @class      genivimedia::IPlayer
 * @brief      The IPlayer class provides functions to play media contents.
 * @details    Member functions provided by IPlayer class perform the following actions.
 *             <ul>
 *                 <li>Loads media contents from local storage, and mtp.
 *                 <li>Performs general playback operations such as play, pause, seek and playback rate.
 *                 <li>Unloads currently loaded media contents.
 *                 <li>Controls audio track, audio volume, and audio mute/unmute.
 *                 <li>Controls subtitle track, and visibility.
 *                 <li>Changes video window size.
 *                 <li>Sends playback informations such as duration, audio/subtitle tracks, and playing time.
 *             </ul>
 * @see        genivimedia::MediaPlayer genivimedia::Pipeline
 */

class IPlayer {
 public:
  /**
   * @fn ~IPlayer
   * @brief Destructor.
   * @section function_flow_none Function Flow : None
   * @param : None
   * @section global_variable_none Global Variables : None
   * @section dependency_none Dependencies : None
   * @return None
   */
  virtual ~IPlayer() {}

  /**
   * @fn RegisterCallback
   * @brief Registers callback function.
   * @section function_flow_none Function Flow : None
   * @param[in] callback : callback function
   * @section global_variable_none Global Variables : None
   * @section dependency_none Dependencies : None
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool RegisterCallback(std::function < void (const std::string& data) > callback) =  0;

  /**
   * @fn Play
   * @brief Plays media contents.
   * @section function_flow_none Function Flow : None
   * @param : None
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - SetURI()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool Play() = 0;

  /**
   * @fn Pause
   * @brief Pauses media contents.
   * @section function_flow_none Function Flow : None
   * @param : None
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - SetURI()
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool Pause() = 0;

  /**
   * @fn Stop
   * @brief Unloads media contents.
   * @section function_flow_none Function Flow : None
   * @param : None
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - SetURI()
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool Stop() = 0;

  /**
   * @fn FastForward
   * @brief Changes playback rate of media pipeline in forward direction.
   * @section function_flow_none Function Flow : None
   * @param[in] step <4.0, 20.0> : playback rate
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool FastForward(double step) = 0;

  /**
   * @fn Rewind
   * @brief Changes playback rate of media pipeline in reverse direction.
   * @section function_flow_none Function Flow : None
   * @param[in] step <-4.0, -20.0> : playback rate
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool Rewind(double step) = 0;

  /**
   * @fn SetPlaybackSpeed
   * @brief Changes playback rate of media pipeline.
   * @section function_flow_none Function Flow : None
   * @param[in] speed <0.2, 2.0> : speed playback rate
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetPlaybackSpeed(double speed) = 0;

  /**
   * @fn bool SetURI(const std::string& uri, int media_type)
   * @brief Loads media contents with the given uri, and media type.<br>
   * - If a pipeline is loaded, it will destroy and creates new pipeline.<br>
   * - This can be one of the following prefix: file://, thumbnail://, and dvd://.<br>
   *
   * @section function_flow_none Function Flow : None
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
  virtual bool SetURI(const std::string& uri, int media_type) = 0;

  /**
   * @fn bool SetURI(const std::string& uri, const std::string& option)
   * @brief Loads media contents with the given uri, and option string.<br>
   * - The given uri string shall havve thumbnail prefix(thumbnail://).<br>
   * - The given option string shall be JSON format like {"Option":{"filename": "filename"}}.<br>
   * - This method is used to save thumbnail with the give filename.<br>
   *
   * @section function_flow_none Function Flow : None
   * @param[in] uri: uri string of media content
   * @param[in] option: option string in JSON format
   * @section global_variable_none Global Variables : None
   * @section dependency_none Dependencies : None
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetURI(const std::string& uri, const std::string& option) = 0;

  /**
   * @fn SetPosition
   * @brief Seeks media content to the specified position.
   * @section function_flow_none Function Flow : None
   * @param[in] position <0~duration of media content>: seek position in milliseconds
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetPosition(gint64 position) = 0;

  /**
   * @fn StopRateChange
   * @brief Stops playback rate change of media contents.
   * @section function_flow_none Function Flow : None
   * @param : None
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - FastForward()
   * - Rewind()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool StopRateChange() = 0;

  /**
   * @fn SetSubtitleEnable
   * @brief Sets visibility of loaded subtitle.
   * @section function_flow_none Function Flow : None
   * @param[in] show <0,1>: value of subtitle visibility
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetSubtitleEnable(bool show) = 0;

  /**
   * @fn SetSubtitleLanguage
   * @brief Sets language with the given language code of loaded subtitle.
   * @section function_flow_none Function Flow : None
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
  virtual bool SetSubtitleLanguage(const std::string& language) = 0;

  /**
   * @fn SetSubtitleLanguageIndex
   * @brief Sets language with the given index of loaded subtitle.
   * @section function_flow_none Function Flow : None
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
  virtual bool SetSubtitleLanguageIndex(int language) = 0;

  /**
   * @fn SetAudioLanguage
   * @brief Sets audio language with the given index.
   * @section function_flow_none Function Flow : None
   * @param[in] index <0~one less than a number of audio tracks>: index of audio language
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetAudioLanguage(int index) = 0;

  /**
   * @fn SetAudioMute
   * @brief Sets audio mute/unmute.
   * @section function_flow_none Function Flow : None
   * @param[in] mute <0,1>: mute/unmute value
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetAudioMute(bool mute) = 0;

     /**
   * @fn SetAVoffset
   * @brief Sets AV offset.
   * @section function_flow Function Flow : None
   * @param[in] delay : delay
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetAVoffset(int delay) = 0;

  /**
   * @fn SetAudioVolume
   * @brief Sets audio volume.
   * @section function_flow_none Function Flow : None
   * @param[in] volume <0.0~10.0>: audio volume
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - Play()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetAudioVolume(double volume) = 0;

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
  virtual bool SwitchChannel(bool downmix) = 0;

  /**
   * @fn SetVideoWindow
   * @brief Sets video window information to change video window size.
   * - video window inforamtion string shall be JSON format like "ivi-surface-info,ivi-surface-id=(int)value, window-x-coord=(int)value,window-y-coord=(int)value,window-width=(uint)value,window-height=(uint)value"
   *
   * @section function_flow_none Function Flow : None
   * @param[in] info: video window information string in JSON format
   * @section global_variable_none Global Variables : None
   * @section dependency Dependencies :
   * - SetURI()
   *
   * @return bool (TRUE - SUCCESS, FALSE - FAIL)
   */
  virtual bool SetVideoWindow(const std::string& info) = 0;

  virtual bool QuitPlayerEngine() = 0;

  virtual bool SetVideoContrast(float contrast) = 0;

  virtual bool SetVideoSaturation(float saturation) = 0;

  virtual bool SetVideoBrightness(float brightness) = 0;

  virtual int GetChannelInfo(const std::string& uri, const std::string& option) = 0;

 protected:
  /**
   * @fn IPlayer
   * @brief Costructor.
   * @section function_flow_none Function Flow : None
   * @param : None
   * @section global_variable_none Global Variables : None
   * @section dependency_none Dependencies : None
   * @return None
   */
  IPlayer() {};
};

}  // namespace genivimedia

#endif // GENIVIMEDIA_PLAYER_INTERFACE_H

