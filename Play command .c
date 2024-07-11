/*
 * Return -1 : HResult::ERROR
 * Return -2 : HResult::CONNECTION_FAIL (APN FAiled)
 * Return 0  : HResult::OK
 */

int32_t PlayerEngineCCOSAdaptor::load(uint64_t handle, bool use_audio_focus, bool needDownmix) {
    std::function<void(const CommonAPI::CallStatus&, uint64_t rhandle, const PlayerTypes::PlayerError&)> callback;

    PlayerTypes::PlayerError error;
    CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::INVALID_VALUE;
    CommonAPI::CallInfo callInfo(2000);

    GError *gError = NULL;
    GVariant *ret = NULL;

    PlayerEngineAdaptationInfo info;

    std::string str_url = "";
    std::string str_url_with_slot = "";

    bool needRetry = false;
    bool isAPNChanged = false;
    int32_t audio_channel = 0;
    uint32_t media_id = 0;
    uint32_t player_type = 0;

    if (getAdaptationInfoFromInfoMap(handle, info) == true) {
        str_url = str_url_with_slot = info.url;
        player_type = info.player_type;

       
--> MediaType : video


	 if (player_type == static_cast<uint32_t>(PlayerTypes::MediaType::VIDEO)) {
            //audio_channel = getReadCntByDatabase(str_url);
            m_logger->i("{}) {}:{} : channel info({}), 2ch_down=[{}]", TAG, __FUNCTION__, __LINE__, audio_channel, needDownmix);
            m_logger->i("{}) {}:{} : UHD({})", TAG, __FUNCTION__, __LINE__, getPlayNgByDatabase(str_url));
        } else if ((player_type == static_cast<uint32_t>(PlayerTypes::MediaType::MOOD_THERAPY_VIDEO)) ||
                    (player_type == static_cast<uint32_t>(PlayerTypes::MediaType::GOLF_VIDEO)) ||
                    (player_type == static_cast<uint32_t>(PlayerTypes::MediaType::RECORDING_PLAY)) ||
                    (player_type == static_cast<uint32_t>(PlayerTypes::MediaType::FACE_DETECTION)) ||
                    (player_type == static_cast<uint32_t>(PlayerTypes::MediaType::DVRS_REAR))) {
            // MOOD_THERAPY_VIDEO and GOLF_VIDEO doesn't use audio source
            use_audio_focus = false;
            if (player_type == static_cast<uint32_t>(PlayerTypes::MediaType::GOLF_VIDEO)) {
                m_logger->i("{}) {}:{} : enable APN", TAG, __FUNCTION__, __LINE__, handle);
                isAPNChanged = true;
                setAPNStatusToInfoMap(handle, true);
            }
        } else if (player_type == static_cast<uint32_t>(PlayerTypes::MediaType::MELON) ||
                    player_type == static_cast<uint32_t>(PlayerTypes::MediaType::GENIE) ||
                    player_type == static_cast<uint32_t>(PlayerTypes::MediaType::WELAAA_AUDIO_STREAMING) ||
                    player_type == static_cast<uint32_t>(PlayerTypes::MediaType::GENESIS_AUDIO_STREAMING) ||
                    player_type == static_cast<uint32_t>(PlayerTypes::MediaType::KAOLAFM) ||
                    player_type == static_cast<uint32_t>(PlayerTypes::MediaType::QQMUSIC) ||
                    player_type == static_cast<uint32_t>(PlayerTypes::MediaType::XIMALAYA) ||
                    player_type == static_cast<uint32_t>(PlayerTypes::MediaType::PODBBANG) ||
                    player_type == static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_FUNAUDIO) ||
                    player_type == static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_MINI_APP_AUDIO) ||
                    player_type == static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_MINI_APP_VIDEO) ||
                    player_type == static_cast<uint32_t>(PlayerTypes::MediaType::VIBE)) {
            if (str_url.compare(0, 3, "(1)") == 0) { // APN change to default CCS
                m_logger->i("{}) {}:{} : default APN", TAG, __FUNCTION__, __LINE__, handle);
                str_url = str_url.substr(3);
                str_url_with_slot = str_url;
                setAPNDefaultStatusToInfoMap(handle, true);
                isAPNChanged = true;
                setAPNStatusToInfoMap(handle, true);
                //update info.url by removing (1)
                setUrlToInfoMap(handle, str_url);
            } else { // To enable APN change
                m_logger->i("{}) {}:{} : enable APN", TAG, __FUNCTION__, __LINE__, handle);
                isAPNChanged = true;
                setAPNStatusToInfoMap(handle, true);
            }

            if (str_url.compare(0, 3, "(A)") == 0) { // To check Atmos streaming
                m_logger->i("{}) {}:{} : Atmos streaming", TAG, __FUNCTION__, __LINE__, handle);
                str_url = str_url.substr(3);
                str_url_with_slot = str_url;
                audio_channel = 6;
                //update info.url by removing (A)
                setUrlToInfoMap(handle, str_url);
            }

            if ((player_type == static_cast<uint32_t>(PlayerTypes::MediaType::KAOLAFM) ||
                 (player_type == static_cast<uint32_t>(PlayerTypes::MediaType::QQMUSIC)) )) {
                setPlayerStatusToInfoMap(handle, ccos::media::HMediaPlayingState::UNINIT);
            }
        } else {
            // None
            m_logger->i("{}) {}:{} : channel info({}), 2ch_down=[{}]", TAG, __FUNCTION__, __LINE__, audio_channel, needDownmix);
        }

        // Set Audio Channel after checking whether the multi channel is supported or not.
        if (!isMultiChannel || needDownmix) {
            audio_channel = 2;
        } else {
            if (audio_channel > 0) {
                // Do nothing
            } else if ( player_type == static_cast<uint32_t>(PlayerTypes::MediaType::AUDIO) ||
                 player_type == static_cast<uint32_t>(PlayerTypes::MediaType::VIDEO) ||
                 player_type == static_cast<uint32_t>(PlayerTypes::MediaType::MANUAL_VIDEO) ||
                 player_type == static_cast<uint32_t>(PlayerTypes::MediaType::MOOD_THERAPY_AUDIO) ||
                 player_type == static_cast<uint32_t>(PlayerTypes::MediaType::NATURE_SOUND) ) {
                audio_channel = 0;
            } else {
                audio_channel = 2;
            }
        }

        if ((mConnection != NULL) && use_audio_focus) {
            int32_t retry = 3;
            int16_t audio_ret = 0;
            int16_t audio_slot = -1;
            uint16_t audio_src_id = 0;
            std::string volumeType = "";

            volumeType = getVolumeTypeAndSrcId(player_type, &audio_src_id);
            if (volumeType.size() == 0 && audio_src_id == 0) {
                m_logger->e("{}) {}:{} : getVolumeTypeAndSrcId fail - type=[{}]",
                            TAG, __FUNCTION__, __LINE__, player_type);
                return -1;
            }

            do {
                ret = g_dbus_connection_call_sync(mConnection, AM_SERVICE_NAME, AM_OBJECT_PATH, AM_INTERFACE_NAME,
                                                  AM_REQ_GETSLOTAVAILABLE,
                                                  g_variant_new ("(qq)", audio_src_id, 0x01), // 0x01 2ch TDM table
                                                  G_VARIANT_TYPE_TUPLE,
                                                  G_DBUS_CALL_FLAGS_NONE,
                                                  -1,
                                                  NULL,
                                                  &gError);
                if (gError) {
                    m_logger->e("{}) {}:{} : Error getting available slot", TAG, __FUNCTION__, __LINE__);
                    g_error_free(gError);
                    return -1;
                }
                if (ret) {
                    g_variant_get(ret, "(nn)", &audio_ret, &audio_slot);
                    m_logger->i("{}) {}:{} : ret=[{}], Available slot=[{}]", TAG, __FUNCTION__, __LINE__, audio_ret, audio_slot);

                    if ( audio_ret == 8 || (audio_slot < 0 || audio_slot > 0x09) ) {
                        m_logger->e("{}) {}:{} : Error invalid slot~!!! ####", TAG, __FUNCTION__, __LINE__);
                        retry--;
                    } else {
                        retry = 0;
                        setAudioSlotToInfoMap(handle, audio_slot);
                        std::string slotString = std::to_string(audio_slot);
                        std::string slotBrace = "[]";

                        slotBrace.insert(1, slotString);
                        str_url_with_slot.insert(0, slotBrace);

                        if (volumeType.size() > 1) {
                            volumeType.replace(volumeType.size()-1, 1, slotString);
                        }
                    }
                    g_variant_unref(ret);
                    ret = NULL;
                }
            } while (retry > 0);

            // audio_ret == 8(NON_EXISTENT)
            if (audio_ret == 8 || audio_slot == -1) {
                return -1;
            }

            if (isMultiChannel && (audio_channel != 2)) { // check 5.1ch slot available or not
                ret = g_dbus_connection_call_sync(mConnection, AM_SERVICE_NAME, AM_OBJECT_PATH, AM_INTERFACE_NAME,
                                                  AM_REQ_GETSLOTAVAILABLE,
                                                  g_variant_new ("(qq)", audio_src_id, 0x02), // 0x02 5.1ch table
                                                  G_VARIANT_TYPE_TUPLE,
                                                  G_DBUS_CALL_FLAGS_NONE,
                                                  -1,
                                                  NULL,
                                                  &gError);
                if (gError) {
                    m_logger->e("{}) {}:{} : Error getting 5.1ch available slot", TAG, __FUNCTION__, __LINE__);
                    g_error_free(gError);
                    return -1;
                }
                if (ret) {
                    g_variant_get(ret, "(nn)", &audio_ret, &audio_slot);
                    m_logger->i("{}) {}:{} : ret=[{}], 5.1ch available slot=[{}]", TAG, __FUNCTION__, __LINE__, audio_ret, audio_slot);

                    if ( audio_ret == 8 || audio_slot < 0 ) {
#ifndef USE_DOWNMIX
                        m_logger->i("{}) {}:{} : 5.1ch slot is not available, use 2ch mode", TAG, __FUNCTION__, __LINE__);
                        audio_channel = 2;
#else
                        m_logger->i("{}) {}:{} : 5.1ch slot is not available, but keep channel", TAG, __FUNCTION__, __LINE__);
#endif
                    }
                    g_variant_unref(ret);
                    ret = NULL;
                } else {
                    m_logger->e("{}) {}:{} : failed to get 5.1ch slot info", TAG, __FUNCTION__, __LINE__);
                }
            }
        }
    } else { // No-element
        return -1;
    }

    if (isProxyConnectionAvailable()) {
        PlayerTypes::MediaType media_type;

        switch(player_type) {
            case static_cast<uint32_t>(PlayerTypes::MediaType::AUDIO):
                media_type = PlayerTypes::MediaType::AUDIO;
                callInfo.timeout_ = 2500;
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::VIDEO):
                media_type = PlayerTypes::MediaType::VIDEO;
                break;

        m_logger->i("{}:{} Setting media_type [{}]", __FUNCTION__, __LINE__, static_cast<uint32_t>(media_type));
        if (setMediaType(handle, media_type) == true) {
            playerProxy->openUri(str_url_with_slot, audio_channel, media_type, callStatus, error, media_id, &callInfo);
            if (callStatus != CommonAPI::CallStatus::SUCCESS) {
                needRetry = true;
            }

            if (needRetry || media_id < 10) {
                int retry = PLAYER_PROXY_CONNECT_RETRY_COUNT_SIMPLE;
                do {
                    m_logger->i("{}:{} Invalid media_id [{}], get by media type again", __FUNCTION__, __LINE__, media_id);
                    usleep(10*PLAYER_PROXY_CONNECT_INTERVAL);
                    playerProxy->getMediaIdByMediaType(media_type, callStatus, error, media_id);
                    retry--;
                } while (retry > 0 && media_id < 10);

                needRetry = (retry <= 0) ? true : false;

                if (needRetry) {
                    /* To-do: Recovery code review */
                    m_logger->e("{}) {}:{} : Command API dbus error, try force kill[{}] and retry", TAG, __FUNCTION__, __LINE__, media_id);
                    playerProxy->stop(media_id, true, callStatus, error, &callInfo);
                    playerProxy->openUri(str_url_with_slot, audio_channel, media_type, callStatus, error, media_id, &callInfo);
                }
            }
        } else {
            m_logger->e("{}) {}:{} : Failed to set media type",TAG, __FUNCTION__, __LINE__);
            return -1;
        }

        /* Remove the map, if already exits */
        (void)deleteHandleFromMediaIDMap(handle);
        if (createHandleToMediaIDMap(handle, media_id, true) != true) {
            m_logger->e("{}) {}:{} : Failed to create  type",TAG, __FUNCTION__, __LINE__);
            return -1;
        }
    } else {
        m_logger->e("{}) {}:{} : Command API connection error",TAG, __FUNCTION__, __LINE__);
        return -1;
    }

    std::lock_guard<std::mutex> lock(condLoadLock);
    isLoadFinished = true;
    condLoadWait.notify_all();
    return 0;
}


