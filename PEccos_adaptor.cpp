#include <cstring>
#include <memory>
#include <type_traits>
#include <thread>
#include <chrono>
#include <limits>
#include <functional>
#include <algorithm>

#include <unistd.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "PlayerEngineCCOSAdaptor.h"

#define DNS_QUERY_BUFFER_SIZE 1024

namespace MM = ::v1::org::genivi::mediamanager;

using namespace std::placeholders;
using namespace boost::property_tree;

PlayerEngineCCOSAdaptor* PlayerEngineCCOSAdaptor::sInstance = nullptr;

PlayerEngineCCOSAdaptor* PlayerEngineCCOSAdaptor::getInstance() {
    if(sInstance == nullptr) {
        sInstance = new PlayerEngineCCOSAdaptor;
        std::atexit([]{
            if(sInstance != nullptr){
                delete sInstance;
                sInstance = nullptr;
            }
        });
    }
    return sInstance;
}

PlayerEngineCCOSAdaptor::PlayerEngineCCOSAdaptor():
    betweenHandleContextOffset(PLAYER_HANDLE_CONTEXT_OFFSET), adaptorInitialized(false), stateChangedEventSubscribed(false),isLoadFinished(false),
    isLastNoResponse(false),isMultiChannel(false),isFirstPlay(false),isSpeedSet(false),StateChangeSubs(0),PlaybackErrorSubs(0),
    getPosSubs(0),getBufferingSubs(0), getPlaybackSubs(0),changeDurationEvent(0),mConnection(nullptr), mConnectTelephony(nullptr),mTranscoderListener(nullptr),m_SubtitleData(nullptr) {

        try {
            m_logger = HMediaPlayerLogger::getInstance().get_logger();
        } catch (const std::bad_alloc& exception) {
            // Empty
        }

        JsonParserList.push_back(new PEJsonParser("AudioLanguageList", nullptr));
        JsonParserList.push_back(new PEJsonParser("SubtitleList", nullptr));
        JsonParserList.push_back(new PEJsonParser("CurrentTrack", nullptr));
        //JsonParserList.push_back(new PEJsonParser("Error", nullptr));
        std::function<bool (uint64_t, std::string) > resolutionParser
            = std::bind(&PlayerEngineCCOSAdaptor::parseJsonVideoResolution,this,std::placeholders::_1,std::placeholders::_2);
        std::function<bool (uint64_t,std::string)> eosParser
            = std::bind(&PlayerEngineCCOSAdaptor::parseJsonEOS,this, std::placeholders::_1, std::placeholders::_2);
        std::function<bool (uint64_t,std::string)> bosParser
            = std::bind(&PlayerEngineCCOSAdaptor::parseJsonBOS,this, std::placeholders::_1, std::placeholders::_2);
        std::function<bool (uint64_t,std::string)> subtitleDataParser
            = std::bind(&PlayerEngineCCOSAdaptor::parseJsonSubtitleData,this, std::placeholders::_1, std::placeholders::_2);
        std::function<bool (uint64_t, std::string)> SeekingParser
            = std::bind(&PlayerEngineCCOSAdaptor::parseJsonSeeking, this, std::placeholders::_1, std::placeholders::_2);
        std::function<bool (uint64_t, std::string)> TrickPlayingParser
            = std::bind(&PlayerEngineCCOSAdaptor::parseJsonTrickPlaying, this, std::placeholders::_1, std::placeholders::_2);
        std::function<bool (uint64_t, std::string)> WarningParser
            = std::bind(&PlayerEngineCCOSAdaptor::parseJsonWarning, this, std::placeholders::_1, std::placeholders::_2);
        std::function<bool (uint64_t, std::string)> ChannelParser
            = std::bind(&PlayerEngineCCOSAdaptor::parseJsonChannel, this, std::placeholders::_1, std::placeholders::_2);
        std::function<bool (uint64_t, std::string)> AddressParser
            = std::bind(&PlayerEngineCCOSAdaptor::parseJsonAddress, this, std::placeholders::_1, std::placeholders::_2);
        std::function<bool (uint64_t, std::string)> ContentTypeParser
            = std::bind(&PlayerEngineCCOSAdaptor::parseJsonContentType, this, std::placeholders::_1, std::placeholders::_2);

        JsonParserList.push_back(new PEJsonParser("Resolution", resolutionParser));
        JsonParserList.push_back(new PEJsonParser("BOS", bosParser));
        JsonParserList.push_back(new PEJsonParser("EOS", eosParser));
        JsonParserList.push_back(new PEJsonParser("SubtitleData", subtitleDataParser));
        JsonParserList.push_back(new PEJsonParser("Seeking", SeekingParser));
        JsonParserList.push_back(new PEJsonParser("TrickPlaying",TrickPlayingParser));
        JsonParserList.push_back(new PEJsonParser("Warning",WarningParser));
        JsonParserList.push_back(new PEJsonParser("ChannelInfo",ChannelParser));
        JsonParserList.push_back(new PEJsonParser("AddressInfo", AddressParser));
        JsonParserList.push_back(new PEJsonParser("ContentType",ContentTypeParser));
}

PlayerEngineCCOSAdaptor::~PlayerEngineCCOSAdaptor() {
    if (mConnection) {
        g_object_unref(mConnection);
        mConnection = nullptr;
    }

    if (mConnectTelephony) {
        g_object_unref(mConnectTelephony);
        mConnectTelephony = nullptr;
    }

    if (m_SubtitleData) {
        munmap(m_SubtitleData, sizeof(SubtitleData));
        m_SubtitleData = nullptr;
    }

    JsonParserList.clear();
    /*TODO : disconnect CommonAPI or check the way to close CommonAPI connection*/
    runtime.reset();
    playerProxy.reset();
    mTranscoderListener.reset();
    /*TODO : check the player status and Stop player engine*/
    PlayerEngineAdaptInfoMap.clear();
    PlayerContextStatusCallbackMap.clear();
    PlayerContextSubtitleListenerMap.clear();

/*
 * Request DNS Change to remove rules
 */
    std::map<std::string, std::vector<std::string>>::iterator map_iter = currentHostMap.begin();

    for (; map_iter != currentHostMap.end(); ++map_iter) {
        std::vector<std::string>::iterator iter = map_iter->second.begin();
        for(; iter != map_iter->second.end(); ++iter) {
            setRouteTable(*iter, currentSrcMap[map_iter->first], false);
        }
        map_iter->second.clear();
    }
}

//ccos::media::IHMediaPlayerListener::IHMediaPlayerListener() {}
//ccos::media::IHMediaPlayerListener::~IHMediaPlayerListener() {}

PlayerEngineCCOSAdaptor::ConvertedTransCoderListener::ConvertedTransCoderListener(
        const std::shared_ptr<ccos::media::IHMediaTranscoderListener>& transListener) {
    m_logger = HMediaPlayerLogger::getInstance().get_logger();
    mTransListener = transListener.get();
    mDuration = 0;
}

PlayerEngineCCOSAdaptor::ConvertedTransCoderListener::~ConvertedTransCoderListener() {

}

void PlayerEngineCCOSAdaptor::ConvertedTransCoderListener::onPlayingStateChanged(
        const ccos::HUInt64& handle, const ccos::media::HMediaPlayingState& state) {
    if (mTransListener != nullptr) {
        switch(state) {
            case ccos::media::HMediaPlayingState::PLAYING:
                mTransListener->onStateChanged(ccos::media::HMediaTranscoderState::RUNNING);
                break;
            case ccos::media::HMediaPlayingState::PAUSED:
                mTransListener->onStateChanged(ccos::media::HMediaTranscoderState::PAUSED);
                break;
            case ccos::media::HMediaPlayingState::STOPPED:
                mTransListener->onStateChanged(ccos::media::HMediaTranscoderState::TERMINATED);
                mTransListener->onStatusChanged(ccos::media::HMediaTranscoderStatus::INSTANCE_REMOVED);
                break;
            case ccos::media::HMediaPlayingState::EOS:
                mTransListener->onStateChanged(ccos::media::HMediaTranscoderState::STOPPED);
                mTransListener->onStatusChanged(ccos::media::HMediaTranscoderStatus::TRANSCODING_COMPLETE);
                break;
            case ccos::media::HMediaPlayingState::FAILED_TO_PLAY: // FALL-THROUGH
            case ccos::media::HMediaPlayingState::FAILED_TO_POSITION_CHANGED: // FALL-THROUGH
            case ccos::media::HMediaPlayingState::FAILED_UNSUPPORTED_CODEC_ALL: // FALL-THROUGH
            case ccos::media::HMediaPlayingState::FAILED_UNSUPPORTED_CODEC_AUDIO: // FALL-THROUGH
            case ccos::media::HMediaPlayingState::FAILED_UNSUPPORTED_CODEC_VIDEO: // FALL-THROUGH
            case ccos::media::HMediaPlayingState::FAILED_UNSUPPORTED_CONTAINER:
                mTransListener->onStateChanged(ccos::media::HMediaTranscoderState::STOPPED);
                mTransListener->onStatusChanged(ccos::media::HMediaTranscoderStatus::ERROR);
                break;
            default:
                m_logger->e("{}) {}:{} : Undefined state=[{}]", TAG, __FUNCTION__, __LINE__, static_cast<uint32_t>(state));
                break;
        }
    }
}

void PlayerEngineCCOSAdaptor::ConvertedTransCoderListener::onPlayingTimeChanged(
        const ccos::HUInt64& handle, const ccos::HUInt64& position) {
    if (mTransListener != nullptr && mDuration > 0) {
        ccos::HUInt32 progress = (ccos::HUInt32)((position * 100) / mDuration);
        mTransListener->onTranscodingProgress(progress, 100);
        m_logger->i("{}) {}:{} : Transcoding progress=[{}]", TAG, __FUNCTION__, __LINE__, static_cast<uint32_t>(progress));
    }
}

void PlayerEngineCCOSAdaptor::ConvertedTransCoderListener::setDuration(const uint64_t& duration) {
    mDuration = duration;
}

bool PlayerEngineCCOSAdaptor::createProxy() {

    const std::string &domain = "local";
    const std::string &instance = "org.genivi.mediamanager.Player";
    const std::string &connection = "Player";

    runtime = CommonAPI::Runtime::get();

    if(runtime == nullptr){
        m_logger->e("{}) {}:{} : Error-fail to get Common API runtime", TAG, __FUNCTION__, __LINE__);
        return false;
    }

    this->playerProxy = runtime->buildProxy<PlayerProxy>(domain, instance, connection);

    if(!this->playerProxy){
        m_logger->e("{}) {}:{} : Error-fail to build proxy", TAG, __FUNCTION__, __LINE__);
        return false;
    }

    GError *error = NULL;
    if (!mConnection)
        mConnection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);

    if (error) {
        m_logger->e("{}) {}:{} : Failed to set up am connection ({})", TAG, __FUNCTION__, __LINE__, error->message);
        g_error_free(error);
        return false;
    }
    return true;
}

bool PlayerEngineCCOSAdaptor::CommonAPIInit() {

    this->createProxy();

    if(this->playerProxy){

        if(waitUntilProxyAvailable(PLAYER_PROXY_CONNECT_RETRY_COUNT) != true){
            m_logger->e("{}) {}:{} : Error-Player is not available", TAG, __FUNCTION__, __LINE__);
            return false;
        }

        this->playerProxy->getProxyStatusEvent().subscribe(
                [&](const CommonAPI::AvailabilityStatus& status){
                    if(status == CommonAPI::AvailabilityStatus::AVAILABLE){
                        m_logger->i("{}) {}:{} : PlayerProxy AVAILABLE", TAG, __FUNCTION__, __LINE__);
                        return true;
                    }
                    else if(status == CommonAPI::AvailabilityStatus::NOT_AVAILABLE){
                        m_logger->e("{}) {}:{} : Error-PlayerProxy NOT_AVAILABLE", TAG, __FUNCTION__, __LINE__);
                        return false;
                    }
                    else
                        return false;
                });
    }
    else {
        m_logger->e("{}) {}:{} : Error Can't get playerProxy",TAG,__FUNCTION__, __LINE__);
        return false;
    }

    return true;
}

bool PlayerEngineCCOSAdaptor::isProxyConnectionAvailable() {
    if(this->playerProxy != 0){
        if (!adaptorInitialized) {
            m_logger->e("{}) {}:{} : Proxy is valid, but it needs init", TAG, __FUNCTION__, __LINE__);
            adaptorInitialized = CommonAPIInit();
        }
        return this->playerProxy->isAvailable() ? true : false;
    } else {
        if (!adaptorInitialized) {
            m_logger->e("{}) {}:{} : Try to recover playerProxy", TAG, __FUNCTION__, __LINE__);
            adaptorInitialized = CommonAPIInit();
            if (adaptorInitialized && this->playerProxy != 0)
                return this->playerProxy->isAvailable() ? true : false;
        }
        m_logger->e("{}) {}:{} : playerProxy fail", TAG, __FUNCTION__, __LINE__);
        return false;
    }
}

bool PlayerEngineCCOSAdaptor::waitUntilProxyAvailable(int32_t try_count) {
   int count = 0;

   while(this->playerProxy && !this->playerProxy->isAvailable()
           &&  count < PLAYER_PROXY_CONNECT_RETRY_COUNT)
   {
       usleep(PLAYER_PROXY_CONNECT_INTERVAL);
       count++;
   }

   if(count >= try_count){
       //TODO: try to recover media manager
       return false;
   }
   else
       return true;
}

bool PlayerEngineCCOSAdaptor::unsubscribeEvents(uint64_t handle) {
    if (stateChangedEventSubscribed == true) {
        playerProxy->getStateChangedEvent().unsubscribe(StateChangeSubs);
        playerProxy->getErrorOccuredEvent().unsubscribe(PlaybackErrorSubs);
        unsubscribeDefaultPlayerContextEvent(handle);
        stateChangedEventSubscribed = false;
    }
    return true;
}

bool PlayerEngineCCOSAdaptor::setSubscriptionActivation(uint64_t handle ,bool activation) {
    bool activated = false;

    if (getSubscriptionActivationFromInfoMap(handle, activated) == true) {
        if (activated == activation) {
            m_logger->i("{}) {}:{} : subscription already has done", TAG, __FUNCTION__, __LINE__);
        } else {
            if (setSubscriptionActivationToInfoMap(handle, activation) ) {
                return true;
            }
        }
    }
    return false;
}

void PlayerEngineCCOSAdaptor::unsubscribeDefaultPlayerContextEvent(uint64_t handle) {
    m_logger->i("{}) {}:{} : handle({})", TAG, __FUNCTION__, __LINE__, handle);

    if(setSubscriptionActivation(handle, false)){
        playerProxy->getPositionAttribute().getChangedEvent().unsubscribe(getPosSubs);
        playerProxy->getBufferingAttribute().getChangedEvent().unsubscribe(getBufferingSubs);
        playerProxy->getPlaybackAttribute().getChangedEvent().unsubscribe(getPlaybackSubs);
        playerProxy->getDurationAttribute().getChangedEvent().unsubscribe(changeDurationEvent);
    } else {
        m_logger->e("{}) {}:{} : set Subscription for context 0 has some problems", TAG, __FUNCTION__, __LINE__);
    }
}

bool PlayerEngineCCOSAdaptor::subscribeEvents(uint64_t handle) {
    if(stateChangedEventSubscribed == false){
        std::function<void(std::string, uint32_t)> stateChangedEvent
            = std::bind(&PlayerEngineCCOSAdaptor::MediaDataChangeCallback, this, std::placeholders::_1, std::placeholders::_2);
        StateChangeSubs = this->playerProxy->getStateChangedEvent().subscribe(stateChangedEvent);

        std::function<void(PlayerTypes::PlaybackError, PlayerTypes::Track, uint32_t)> errorOccuredEvent
            = std::bind(&PlayerEngineCCOSAdaptor::PlaybackErrorCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
        PlaybackErrorSubs = this->playerProxy->getErrorOccuredEvent().subscribe(errorOccuredEvent);

        SubscribeDefaultPlayerContextEvent(handle);
        stateChangedEventSubscribed = true;
    }
    return true;
}

void PlayerEngineCCOSAdaptor::SubscribeDefaultPlayerContextEvent(uint64_t handle) {
    m_logger->i("{}) {}:{} : handle({})", TAG, __FUNCTION__, __LINE__, handle);

    if(setSubscriptionActivation(handle, true)){

        std::function<void(std::vector<PlayerTypes::Position>)> changePositionAttribute
         = std::bind(&PlayerEngineCCOSAdaptor::positionChangedCallback,this, std::placeholders::_1);
        getPosSubs = this->playerProxy->getPositionAttribute().getChangedEvent().subscribe(changePositionAttribute);

        std::function<void(std::vector<PlayerTypes::Buffering>)> changeBufferingAttribute
         = std::bind(&PlayerEngineCCOSAdaptor::bufferingChangedCallback,this, std::placeholders::_1);
        getBufferingSubs = this->playerProxy->getBufferingAttribute().getChangedEvent().subscribe(changeBufferingAttribute);

        std::function<void (std::vector<PlayerTypes::Playback>)> playbackStatusCallback
         = std::bind(&PlayerEngineCCOSAdaptor::PlaybackStatusChangeCallback, this, std::placeholders::_1);
        getPlaybackSubs = playerProxy->getPlaybackAttribute().getChangedEvent().subscribe(playbackStatusCallback);

        std::function<void (std::vector<PlayerTypes::Duration>)> changeDurationAttribute
         = std::bind(&PlayerEngineCCOSAdaptor::ChangeDurationCallback,this, std::placeholders::_1);
        changeDurationEvent = playerProxy->getDurationAttribute().getChangedEvent().subscribe(changeDurationAttribute);
    } else {
        m_logger->e("{}) {}:{} : set Subscription for context 0 has some problems", TAG, __FUNCTION__, __LINE__);
    }
}

uint64_t PlayerEngineCCOSAdaptor::getHandle(uint64_t handle) {

    uint64_t value = 0;

    if (getHandleFromInfoMap(handle, value) == true) {
        return value;
    }

    m_logger->e("{}) {}:{} : can't get handle from the map {}", TAG, __FUNCTION__, __LINE__, handle);
    return LLONG_MAX;
}

uint64_t PlayerEngineCCOSAdaptor::obtainPlayerContext(uint32_t type, std::string name) {

    uint64_t handle = 0;

    v1::org::genivi::mediamanager::PlayerTypes::PlayerError error;

    m_logger->i("{}) {}:{} : New player type=[{}], [{}]", TAG, __FUNCTION__, __LINE__, type, name);
    if(adaptorInitialized == false){
        adaptorInitialized = this->CommonAPIInit();

        if (adaptorInitialized == false) {
            return UINT_MAX;
        }
    }

    isMultiChannel = true; // validateMultiChannel(); // This logic is not used any more
    if (name.find("2ch") != std::string::npos) {
        m_logger->i("{}) {}:{} : 5.1ch not supported mode, only 2ch is used..", TAG, __FUNCTION__, __LINE__);
        isMultiChannel = false;
    }

    if(!this->initSharedMemory(type)) {
        m_logger->i("{}) {}:{} : Failed to initialize shared memory", TAG, __FUNCTION__, __LINE__);
    }

    /* As per the new architecture we get the handle/media_id after the openUri.
     * We complete the instantiation with dummy media id and set the correct media id
     * after the openUrl. Use media type(type) itself as handle temporarily.
     */
    handle = type;
    isFirstPlay = (name.compare("Sound of Nature") == 0 ) ? true : false; // HMCCCIC-83916

    if (createAdaptaionInfo(type, name, handle, false) == true) { // In case of making a instance
        m_logger->i("{}) {}:{} : Created a AdaptationInfo, [{}], [{}], [{}]", TAG, __FUNCTION__, __LINE__, type, name, handle);

    } else {
        uint32_t type_ = 0;
        std::string name_ = "";

        (void)getPlayerTypeFromInfoMap(handle, type_);
        (void)getPlayerNameFromInfoMap(handle, name_);

        if ((type_ != type) || (name != name_) ) {
            if (createAdaptaionInfo(type, name, handle, true) == true) {
                m_logger->i("{}) {}:{} : Created a AdaptationInfo forcibly, [{}], [{}], [{}]", TAG, __FUNCTION__, __LINE__, type, name, handle);
            }
        }
    }
    subscribeEvents(handle);

    auto StatusIter = PlayerContextStatusCallbackMap.begin();
    for(; StatusIter != PlayerContextStatusCallbackMap.end(); StatusIter++){
        uint64_t  cont_idx = StatusIter->first;
        if((handle == cont_idx)){
            auto backupStatusListener = StatusIter->second.Listener;
            PlayerContextStatusCallbackMap.erase(handle);
            std::lock_guard<std::mutex> lock(PlayerContextStatusCallbackMap[handle].m);
            PlayerContextStatusCallbackMap[handle].Listener = backupStatusListener;
        }
    }

    auto SubtitleIter = PlayerContextSubtitleListenerMap.begin();
    for(; SubtitleIter != PlayerContextSubtitleListenerMap.end(); SubtitleIter++){
        uint64_t  cont_idx = SubtitleIter->first;
        if((handle == cont_idx)){
            auto backupSubtitleListener = SubtitleIter->second.Listener;
            PlayerContextSubtitleListenerMap.erase(handle);

            std::lock_guard<std::mutex> lock(PlayerContextSubtitleListenerMap[handle].m);
            PlayerContextSubtitleListenerMap[handle].Listener = backupSubtitleListener;
        }
    }
    return handle;
}

bool PlayerEngineCCOSAdaptor::releasePlayerContext(uint64_t handle) {
    v1::org::genivi::mediamanager::PlayerTypes::PlayerError error;

    m_logger->i("{}) {}:{} : called releasePlayerContext({})", TAG, __FUNCTION__, __LINE__, handle);

    if(PlayerContextStatusCallbackMap.find(handle) != PlayerContextStatusCallbackMap.end()){
        PlayerContextStatusCallbackMap.erase(handle);
    }
    if(PlayerContextSubtitleListenerMap.find(handle) != PlayerContextSubtitleListenerMap.end()){
        PlayerContextSubtitleListenerMap.erase(handle);
    }

    if (deleteAdaptationInfo(handle) == true) {
        unsubscribeEvents(handle);
        return true;
    }

    return false;
}

bool PlayerEngineCCOSAdaptor::getSubtitleLanguageList(uint64_t handle, std::vector<std::string>& languages) {
    CommonAPI::CallStatus status = CommonAPI::CallStatus::INVALID_VALUE;
    v1::org::genivi::mediamanager::PlayerTypes::SubtitleList list;
    v1::org::genivi::mediamanager::PlayerTypes::PlayerError error;
    std::vector<std::string> language_list;
    uint32_t media_id = 0;

    if (getMediaIDFromMediaIDMap(handle, media_id) == true) {
        if(isProxyConnectionAvailable()){
            playerProxy->getSubtitleList( media_id, status, list, error);
        } else {
            m_logger->e("{}) {}:{} : CommonAPI Connection Error", TAG, __FUNCTION__, __LINE__);
            return false;
        }
        for(auto& subtitleLanguage : list){
            language_list.push_back(std::string(subtitleLanguage));
        }
        (void)setSubtitleLanguageToInfoMap(handle, language_list);
        if (getSubtitleLanguageToInfoMap(handle, languages) == true) {
            return true;
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setSubtitleLanguage(uint64_t handle, const std::string& language) {
    CommonAPI::CallStatus status = CommonAPI::CallStatus::INVALID_VALUE;
    PlayerTypes::PlayerError error;
    std::vector<std::string> language_list;
    uint32_t media_id = 0;

    if(getSubtitleLanguageToInfoMap(handle, language_list) == true) {
        auto iter = std::find(language_list.begin(), language_list.end(), language);

        if (iter == language_list.end()) {
            getSubtitleLanguageList(handle, language_list);

            auto sub_iter = std::find(language_list.begin(), language_list.end(), language);
            if (sub_iter == language_list.end()) {
                return false;
            }
        }

        if (getMediaIDFromMediaIDMap(handle, media_id) == true) {
            if(isProxyConnectionAvailable()){
                //std::function<void(const CommonAPI::CallStatus&, const uint64_t handle, const v1::org::genivi::mediamanager::PlayerTypes::PlayerError&)> callback =
                //    std::bind(&PlayerEngineCCOSAdaptor::setSubtitleLanguageCallback,this,std::placeholders::_1,std::placeholders::_2, std::placeholders::_3);
                //playerProxy->setSubtitleLanguageAsync(handle, language, callback);
                playerProxy->setSubtitleLanguage(language, media_id, status, error);
                return true;
            }
        } else {
            m_logger->e("{}) {}:{} : CommonAPI Connection Error", TAG, __FUNCTION__, __LINE__);
            return false;
        }
    }
    return false;
}

void PlayerEngineCCOSAdaptor::setSubtitleLanguageCallback(const CommonAPI::CallStatus& status, const uint64_t handle, const PlayerTypes::PlayerError& error) {

}

bool PlayerEngineCCOSAdaptor::getAudioSupportedTracks(uint64_t handle, std::vector<std::string>& audioTracks) {
    CommonAPI::CallStatus status = CommonAPI::CallStatus::INVALID_VALUE;
    PlayerTypes::AudioLanguageList list;
    PlayerTypes::PlayerError error;
    std::vector<std::string> track_list;
    uint32_t media_id = 0;


    if (getMediaIDFromMediaIDMap(handle, media_id) == true) {
        if(isProxyConnectionAvailable()){
            playerProxy->getAudioLanguageList(media_id, status, list, error);
        } else {
            m_logger->e("{}) {}:{} : CommonAPI Connection Error", TAG, __FUNCTION__, __LINE__);
            return false;
        }

        for(auto& audioTrack : list){
            m_logger->e("{}) {}:{} : Audio Language list : [{}]", TAG, __FUNCTION__, __LINE__, audioTrack);
            track_list.push_back(audioTrack);
        }

        (void)setAudioTracksToInfoMap(handle, track_list);
        if (getAudioTrackToInfoMap(handle, audioTracks) == true) {
            return true;
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setAudioTrack(uint64_t handle, const std::string& audioTrack) {
    CommonAPI::CallStatus status = CommonAPI::CallStatus::INVALID_VALUE;
    PlayerTypes::PlayerError error;
    std::vector<std::string> track_list;
    int32_t index = -1;
    uint32_t media_id = 0;

    if(getAudioTrackToInfoMap(handle, track_list) == true) {
        auto iter = std::find(track_list.begin(), track_list.end(), audioTrack);

        if (iter == track_list.end()) {
            getAudioSupportedTracks(handle, track_list);

            auto sub_iter = std::find(track_list.begin(), track_list.end(), audioTrack);
            if (sub_iter == track_list.end()) {
                return false;
            } else {
                index = sub_iter - track_list.begin();
            }
        } else {
            index = iter - track_list.begin();
        }

        if (getMediaIDFromMediaIDMap(handle, media_id) == true) {
            if(isProxyConnectionAvailable()){
                //std::function<void(const CommonAPI::CallStatus&,const uint64_t handle, const PlayerTypes::PlayerError&)> callback =
            //    std::bind(&PlayerEngineCCOSAdaptor::setAudioLanguageCallback,this,std::placeholders::_1,std::placeholders::_2, std::placeholders::_3);
                playerProxy->setAudioLanguage(index, media_id, status, error);
                return true;
            } else {
                m_logger->e("{}) {}:{} : Error-CommonAPI C++ connection error", TAG, __FUNCTION__, __LINE__);
            }
        }
    }
    return false;
}

void PlayerEngineCCOSAdaptor::setAudioLanguageCallback(const CommonAPI::CallStatus& status, const uint64_t handle, const PlayerTypes::PlayerError& error) {

}

void PlayerEngineCCOSAdaptor::MediaDataChangeCallback(std::string s, uint32_t media_id) {
    ptree pt;
    std::istringstream is(s);
    read_json(is,pt);
    uint64_t handle = LLONG_MAX;

    {
        std::unique_lock<std::mutex> lock(condLoadLock);
        condLoadWait.wait_for(lock, std::chrono::milliseconds(500), [this]{ return isLoadFinished; });
    }

    if (getHandleFromMediaIDMap(media_id, handle) == true) {
        m_logger->i("{}) {}:{} : m_id=[{}] {}", TAG, __FUNCTION__, __LINE__, media_id, s);

        auto it = std::find_if(JsonParserList.begin(),JsonParserList.end(),
                [=](PEJsonParser *p) -> bool {
                    if (p == nullptr) {
                        return false;
                    }
                    size_t pos = s.find(p->JsonName);
                    size_t len = (p->JsonName).size();
                    if(pos != std::string::npos && pos > 0) {
                        if (s.at(pos-1) == '"' && s.at(pos+len) == '"')
                            return true;
                        else
                            return false;
                    } else {
                        return false;
                    }
                });

        if(it == JsonParserList.end()){
            m_logger->e("{}) {}:{}: Error- can't get JsonParser of JSON : {})", TAG, __FUNCTION__, __LINE__, s);
            return ;
        }
        m_logger->i("{}) {}:{}, {}", TAG, __FUNCTION__, __LINE__, ((*it)->JsonName));

        if(((*it)->parser) != nullptr){
            m_logger->i("{}) {}:{} : parser is called({})", TAG, __FUNCTION__, __LINE__, (((*it)->JsonName)));
            (((*it)->parser))(handle,s);
        }
    }
    return ;
}

bool PlayerEngineCCOSAdaptor::initSharedMemory(uint32_t type) {
    int shmem_fd = -1;

    if(m_SubtitleData != nullptr)
       munmap(m_SubtitleData, sizeof(SubtitleData));

    // setting umask to zero to get write permission for group
    mode_t old_umask = umask(0);

    if (type == static_cast<uint32_t>(PlayerTypes::MediaType::GOLF_VIDEO)) {
      m_logger->i("{}:{} shared memory create for golf video", __FUNCTION__, __LINE__);
      shmem_fd = shm_open("/subtitle.shm.golf.key", O_CREAT | O_RDWR | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
      if ((shmem_fd == -1) && (errno == EEXIST)) {
        m_logger->i("{}:{} SHM create fails first time as it exist, try normal open", __FUNCTION__, __LINE__);
        // Maybe its already open, try opening it normally
        shmem_fd = shm_open("/subtitle.shm.golf.key", O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
      }
    } else if (type == static_cast<uint32_t>(PlayerTypes::MediaType::VIDEO)) {
      m_logger->i("{}:{} shared memory create for USB video", __FUNCTION__, __LINE__);
      shmem_fd = shm_open("/subtitle.shm.key", O_CREAT | O_RDWR | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
      if ((shmem_fd == -1) && (errno == EEXIST)) {
        m_logger->i("{}:{} SHM create fails first time as it exist, try normal open", __FUNCTION__, __LINE__);
        // Maybe its already open, try opening it normally
        shmem_fd = shm_open("/subtitle.shm.key", O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
      }
    } else {
      m_logger->i("{}:{} shared memory : Unsupported type", __FUNCTION__, __LINE__);
      umask(old_umask);
      return false;
    }

    if(shmem_fd == -1){
        m_logger->e("{}) {}:{} : Error shm open error errno : {} strerr: {}", TAG, __FUNCTION__, __LINE__, errno, strerror(errno));
        umask(old_umask);
        return false;
    }

    if(ftruncate(shmem_fd, sizeof(SubtitleData)) == -1){
        umask(old_umask);
        return false;
    }

    if((m_SubtitleData = (SubtitleData*)mmap(NULL, sizeof(SubtitleData), PROT_READ, MAP_SHARED, shmem_fd, 0)) == MAP_FAILED){
        m_logger->e("{}) {}:{} : Error mmap error", TAG, __FUNCTION__, __LINE__);
        umask(old_umask);
        return false;
    }

    // restore to old umask after SHM creation
    umask(old_umask);
    close(shmem_fd);

    return true;
}

bool PlayerEngineCCOSAdaptor::setPositionAsync(uint64_t handle, uint64_t position) {
    m_logger->i("{}:{} is called with handle : {} position : {}", __FUNCTION__, __LINE__, handle, position);
    std::function<void(const CommonAPI::CallStatus&, const PlayerTypes::PlayerError&)> callback;
    uint64_t pos = 0;
    uint32_t media_id = 0;

    if (getPositionFromInfoMap(handle, pos) == true) {
        if (position != pos) {
            (void)setPositionToInfoMap(handle, position);
        }
        if (getMediaIDFromMediaIDMap(handle, media_id)) {
            if(isProxyConnectionAvailable()){
                callback = std::bind(&PlayerEngineCCOSAdaptor::setPositionCallback, this,std::placeholders::_1, std::placeholders::_2);
                playerProxy->setPositionAsync(position, media_id, callback);
                return true;
            } else {
                m_logger->e("{}) {}:{} : Error-CommonAPI C++ connection error", TAG, __FUNCTION__, __LINE__);
            }
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setURL(uint64_t handle, const std::string& url) {
    m_logger->i("{}) {}:{} : called setURL({}) : [{}]", TAG, __FUNCTION__, __LINE__, handle, url);

    if (setUrlToInfoMap(handle, url) == true) {
        std::lock_guard<std::mutex> lock(condLoadLock);
        isLoadFinished = false;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setTranscodeOutput(const std::string& url, ccos::media::HMediaCodecType codec, ccos::media::HMediaContainerType contain, const std::string& location, const std::string& platform) {
    PlayerTypes::PlayerError error;
    CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::INVALID_VALUE;
    CommonAPI::CallInfo callInfo(1000);

    std::string output_path = "";
    const std::string filePrefix("file://");

    size_t found = url.find(filePrefix);

    MM::PlayerTypes::ACodecType c_type;
    MM::PlayerTypes::FileFormatType f_type;

    c_type = (codec == ccos::media::HMediaCodecType::AUDIO_AAC) ? MM::PlayerTypes::ACodecType::AAC :
             (codec == ccos::media::HMediaCodecType::AUDIO_VORBIS) ? MM::PlayerTypes::ACodecType::OPUS :
             (codec == ccos::media::HMediaCodecType::AUDIO_VORBIS_OPUS) ? MM::PlayerTypes::ACodecType::OPUS :
             (codec == ccos::media::HMediaCodecType::AUDIO_PCM) ? MM::PlayerTypes::ACodecType::WAV_PCM :
             MM::PlayerTypes::ACodecType::AAC; // Default
    f_type = (contain == ccos::media::HMediaContainerType::MPEG4) ? MM::PlayerTypes::FileFormatType::MP4 :
             (contain == ccos::media::HMediaContainerType::OGG) ? MM::PlayerTypes::FileFormatType::OGG :
             (contain == ccos::media::HMediaContainerType::WAV) ? MM::PlayerTypes::FileFormatType::WAV :
             MM::PlayerTypes::FileFormatType::MP4; // Default

    if(isProxyConnectionAvailable()){
        if (found != std::string::npos) {
            output_path = url.substr(found + filePrefix.size());
        } else {
            output_path = url;
        }
        this->playerProxy->arrangeTranscodeOutput(output_path, c_type, f_type, location, platform, callStatus, error, &callInfo);
        if (callStatus != CommonAPI::CallStatus::SUCCESS) {
            m_logger->e("{}) {}:{} : Command API dbus error", TAG, __FUNCTION__, __LINE__);
            return false;
        }
    } else {
        m_logger->e("{}) {}:{} : Command API connection error", TAG, __FUNCTION__, __LINE__);
        return false;
    }
    return true;
}

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
            case static_cast<uint32_t>(PlayerTypes::MediaType::NATURE_SOUND):
                media_type = PlayerTypes::MediaType::NATURE_SOUND;
                if (isFirstPlay) {
                    callInfo.timeout_ = 6000;
                    isFirstPlay = false;
                } else {
                    callInfo.timeout_ = 3000;
                }
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::KAOLAFM):
                media_type = PlayerTypes::MediaType::KAOLAFM;
                callInfo.timeout_ = 3000;
                if(isAPNChanged == true) {
                    std::lock_guard<std::mutex> lock(PERequestAPNChangeMutex);
                    if (requestAPNChange(str_url, static_cast<uint32_t>(PlayerTypes::MediaType::KAOLAFM), true, handle)) {
                        m_logger->e("{}) {}:{} : APN is changed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                    } else {
                        m_logger->e("{}) {}:{} : Chaning APN is failed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                        return -2;
                    }
                }
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::MELON):
                media_type = PlayerTypes::MediaType::MELON;
                if(isAPNChanged == true) {
                    std::lock_guard<std::mutex> lock(PERequestAPNChangeMutex);
                    if (requestAPNChange(str_url, static_cast<uint32_t>(PlayerTypes::MediaType::MELON), true, handle)) {
                        m_logger->e("{}) {}:{} : APN is changed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                    } else {
                        m_logger->e("{}) {}:{} : Chaning APN is failed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                      return -2;
                    }
                }
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::QQMUSIC):
                media_type = PlayerTypes::MediaType::QQMUSIC;
                if(isAPNChanged == true) {
                    std::lock_guard<std::mutex> lock(PERequestAPNChangeMutex);
                    if (requestAPNChange(str_url, static_cast<uint32_t>(PlayerTypes::MediaType::QQMUSIC), true, handle)) {
                        m_logger->e("{}) {}:{} : APN is changed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                    } else {
                        m_logger->e("{}) {}:{} : Chaning APN is failed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                        return -2;
                    }
                }
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::KAKAOI):
                media_type = PlayerTypes::MediaType::KAKAOI;
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::GENIE):
                media_type = PlayerTypes::MediaType::GENIE;
                if(isAPNChanged == true) {
                    std::lock_guard<std::mutex> lock(PERequestAPNChangeMutex);
                    if (requestAPNChange(str_url, static_cast<uint32_t>(PlayerTypes::MediaType::GENIE), true, handle)) {
                        m_logger->e("{}) {}:{} : APN is changed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                    } else {
                        m_logger->e("{}) {}:{} : Chaning APN is failed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                        return -2;
                    }
                }
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::XIMALAYA):
                media_type = PlayerTypes::MediaType::XIMALAYA;
                if(isAPNChanged == true) {
                    std::lock_guard<std::mutex> lock(PERequestAPNChangeMutex);
                    if (requestAPNChange(str_url, static_cast<uint32_t>(PlayerTypes::MediaType::XIMALAYA), true, handle)) {
                        m_logger->e("{}) {}:{} : APN is changed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                    } else {
                        m_logger->e("{}) {}:{} : Chaning APN is failed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                        return -2;
                    }
                }
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::GOLF_VIDEO):
                media_type = PlayerTypes::MediaType::GOLF_VIDEO;
                if(isAPNChanged == true) {
                    std::lock_guard<std::mutex> lock(PERequestAPNChangeMutex);
                    if (requestAPNChange(str_url, static_cast<uint32_t>(PlayerTypes::MediaType::GOLF_VIDEO), true, handle)) {
                        m_logger->e("{}) {}:{} : APN is changed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                    } else {
                        m_logger->e("{}) {}:{} : Chaning APN is failed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                        return -2;
                    }
                }
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::MANUAL_VIDEO):
                media_type = PlayerTypes::MediaType::MANUAL_VIDEO;
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::STREAM):
                media_type = PlayerTypes::MediaType::STREAM;
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::MOOD_THERAPY_AUDIO):
                media_type = PlayerTypes::MediaType::MOOD_THERAPY_AUDIO;
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::MOOD_THERAPY_VIDEO):
                media_type = PlayerTypes::MediaType::MOOD_THERAPY_VIDEO;
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::KIDS_VIDEO):
                media_type = PlayerTypes::MediaType::KIDS_VIDEO;
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::RECORDING_PLAY):
                media_type = PlayerTypes::MediaType::RECORDING_PLAY;
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::PODBBANG):
                media_type = PlayerTypes::MediaType::PODBBANG;
                callInfo.timeout_ = 3000;
                if(isAPNChanged == true) {
                    std::lock_guard<std::mutex> lock(PERequestAPNChangeMutex);
                    if (requestAPNChange(str_url, static_cast<uint32_t>(PlayerTypes::MediaType::PODBBANG), true, handle)) {
                        m_logger->e("{}) {}:{} : APN is changed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                    } else {
                        m_logger->e("{}) {}:{} : Chaning APN is failed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                        return -2;
                    }
                }
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::DVRS_FRONT):
                media_type = PlayerTypes::MediaType::DVRS_FRONT;
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::DVRS_REAR):
                media_type = PlayerTypes::MediaType::DVRS_REAR;
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::KAKAOI2):
                media_type = PlayerTypes::MediaType::KAKAOI2;
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::FACE_DETECTION):
                media_type = PlayerTypes::MediaType::FACE_DETECTION;
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::VIBE):
                media_type = PlayerTypes::MediaType::VIBE;
                callInfo.timeout_ = 3000;
                if(isAPNChanged == true) {
                    std::lock_guard<std::mutex> lock(PERequestAPNChangeMutex);
                    if (requestAPNChange(str_url, static_cast<uint32_t>(PlayerTypes::MediaType::VIBE), true, handle)) {
                        m_logger->i("{}) {}:{} : APN is changed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                    } else {
                        m_logger->e("{}) {}:{} : Chaning APN is failed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                        return -2;
                    }
                }
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_FUNAUDIO):
                media_type = PlayerTypes::MediaType::TENCENT_FUNAUDIO;
                if(isAPNChanged == true) {
                    std::lock_guard<std::mutex> lock(PERequestAPNChangeMutex);
                    if (requestAPNChange(str_url, static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_FUNAUDIO), true, handle)) {
                        m_logger->e("{}) {}:{} : APN is changed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                    } else {
                        m_logger->e("{}) {}:{} : Chaning APN is failed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                        return -2;
                    }
                }
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_MINI_APP_AUDIO):
                media_type = PlayerTypes::MediaType::TENCENT_MINI_APP_AUDIO;
                if(isAPNChanged == true) {
                    std::lock_guard<std::mutex> lock(PERequestAPNChangeMutex);
                    if (requestAPNChange(str_url, static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_MINI_APP_AUDIO), true, handle)) {
                        m_logger->e("{}) {}:{} : APN is changed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                    } else {
                        m_logger->e("{}) {}:{} : Chaning APN is failed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                        return -2;
                    }
                }
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_MINI_APP_VIDEO):
                media_type = PlayerTypes::MediaType::TENCENT_MINI_APP_VIDEO;
                if(isAPNChanged == true) {
                    std::lock_guard<std::mutex> lock(PERequestAPNChangeMutex);
                    if (requestAPNChange(str_url, static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_MINI_APP_VIDEO), true, handle)) {
                        m_logger->e("{}) {}:{} : APN is changed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                    } else {
                        m_logger->e("{}) {}:{} : Chaning APN is failed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                        return -2;
                    }
                }
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::WELAAA_AUDIO_STREAMING):
                media_type = PlayerTypes::MediaType::WELAAA_AUDIO_STREAMING;
                if(isAPNChanged == true) {
                    std::lock_guard<std::mutex> lock(PERequestAPNChangeMutex);
                    if (requestAPNChange(str_url, static_cast<uint32_t>(PlayerTypes::MediaType::WELAAA_AUDIO_STREAMING), true, handle)) {
                        m_logger->e("{}) {}:{} : APN is changed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                    } else {
                        m_logger->e("{}) {}:{} : Chaning APN is failed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                        return -2;
                    }
                }
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::KAKAOI3):
                media_type = PlayerTypes::MediaType::KAKAOI3;
                break;
            case static_cast<uint32_t>(PlayerTypes::MediaType::GENESIS_AUDIO_STREAMING):
                media_type = PlayerTypes::MediaType::GENESIS_AUDIO_STREAMING;
                if(isAPNChanged == true) {
                    std::lock_guard<std::mutex> lock(PERequestAPNChangeMutex);
                    if (requestAPNChange(str_url, static_cast<uint32_t>(PlayerTypes::MediaType::GENESIS_AUDIO_STREAMING), true, handle)) {
                        m_logger->e("{}) {}:{} : APN is changed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                    } else {
                        m_logger->e("{}) {}:{} : Chaning APN is failed, handle[{}]", TAG, __FUNCTION__, __LINE__, handle);
                        return -2;
                    }
                }
                break;
            default:
                m_logger->e("{}) {}:{} : Error-wrong player_type : handle({})", TAG, __FUNCTION__, __LINE__, handle);
                return -1;
        }

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

/*
 * Return true : HResult::OK (if successfully started to trick play mode)
 * Return false : HResult::ERROR (if internal error occured)
 */
bool PlayerEngineCCOSAdaptor::switchChannel(uint64_t handle, bool useDownmix) {
    m_logger->e("{}) {}:{} : Command API called", TAG, __FUNCTION__, __LINE__);
    PlayerEngineAdaptationInfo info;
    uint32_t media_id = 0;

    if (getAdaptationInfoFromInfoMap(handle, info) == true) {
        if (getMediaIDFromMediaIDMap(handle, media_id) == true) {
            if (isProxyConnectionAvailable()) {
                this->playerProxy->switchChannelAsync(media_id, useDownmix);
                return true;
            } else {
                m_logger->e("{}) {}:{} : Command API connection error", TAG, __FUNCTION__, __LINE__);
                return false;
            }
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::play(uint64_t handle) {
    PlayerTypes::PlayerError error;
    CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::INVALID_VALUE;
    CommonAPI::CallInfo callInfo(1000);
    PlayerEngineAdaptationInfo info;
    uint32_t media_id = 0;

    if (getAdaptationInfoFromInfoMap(handle, info) == true) {
        if (getMediaIDFromMediaIDMap(handle, media_id) == true) {
            if(isProxyConnectionAvailable()){
                this->playerProxy->play(media_id, callStatus, error, &callInfo);
                isLastNoResponse = false;
                return true;
            } else {
                m_logger->e("{}) {}:{} : Command API connection error", TAG, __FUNCTION__, __LINE__);
                return false;
            }
        }
    }

    return false;
}

bool PlayerEngineCCOSAdaptor::pause(uint64_t handle) {
    PlayerTypes::PlayerError error;
    CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::INVALID_VALUE;
    CommonAPI::CallInfo callInfo(1000);
    uint32_t type = 0;
    uint32_t media_id = 0;

    if (getPlayerTypeFromInfoMap(handle, type) == true) {
        if (getMediaIDFromMediaIDMap(handle, media_id) == true) {
            if(isProxyConnectionAvailable()){
                // For ignoring pause when SONA loading is not finished yet
                if (type == static_cast<uint32_t>(PlayerTypes::MediaType::NATURE_SOUND)) {
                    std::vector<PlayerTypes::Playback> playback_t;

                    int32_t Idx = getPlaybackAttrIndex(media_id, playback_t);

                    if (Idx <= -1 || (std::size_t)Idx >= playback_t.size()) {
                        m_logger->e("{}) {}:{} : Error[{}]-playback attribute is out-dated", TAG, __FUNCTION__, __LINE__, handle);
                        return false;
                    }

                    if (playback_t[Idx].getStatus() == PlayerTypes::PlaybackStatus::STOPPED ||
                        playback_t[Idx].getStatus() == PlayerTypes::PlaybackStatus::UNINIT) {
                        m_logger->i("{}) {}:{} : Ignore pause[{}]-loading is on-going", TAG, __FUNCTION__, __LINE__, handle);
                        return false;
                    }
                }
                this->playerProxy->pause(media_id, callStatus, error, &callInfo);
                return true;
            }
        }
    }
    m_logger->e("{}) {}:{} : Command API connection error", TAG, __FUNCTION__, __LINE__);
    return false;
}

bool PlayerEngineCCOSAdaptor::stop(uint64_t handle, bool force_kill) {
    PlayerTypes::PlayerError error;
    CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::INVALID_VALUE;
    CommonAPI::CallInfo callInfo(1500);
    PlayerEngineAdaptationInfo info;
    uint32_t media_id = 0;
    int retry = 10;

    do {
        if (getAdaptationInfoFromInfoMap(handle, info) == true) {
            break;
        } else {
            m_logger->w("{}) {}:{} : AdaptationInfo handle[{}] is not created yet.. try 100ms later..", TAG, __FUNCTION__, __LINE__, handle);
            retry--;
            usleep(100*1000);
        }
    } while (retry > 0);

    if (retry < 0) {
        m_logger->e("{}) {}:{} : Stop request is skipeed.. handle:{} force:{}", TAG, __FUNCTION__, __LINE__, handle, force_kill);
        return false;
    }

    if (getMediaIDFromMediaIDMap(handle, media_id) == true) {
        if(isProxyConnectionAvailable()){
            this->playerProxy->stop(media_id, force_kill, callStatus, error, &callInfo);
        } else {
            m_logger->e("{}) {}:{} : Command API connection error", TAG, __FUNCTION__, __LINE__);
            return false;
        }

        if (callStatus != CommonAPI::CallStatus::SUCCESS && force_kill == false) {
            m_logger->e("{}) {}:{} : Command API dbus error, try force kill", TAG, __FUNCTION__, __LINE__);
            this->playerProxy->stop(media_id, true, callStatus, error, &callInfo);
        }
        m_logger->i("{}) {}:{} : return stop({})", TAG, __FUNCTION__, __LINE__, handle);
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::seek(uint64_t handle, const int64_t time) {
    PlayerTypes::PlayerError error;
    CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::INVALID_VALUE;
    PlayerEngineAdaptationInfo info;
    uint64_t pos = 0;
    uint32_t media_id = 0;

    if (getAdaptationInfoFromInfoMap(handle, info) == true) {
        pos = info.position + time;

        if(info.duration == 0 || pos > info.duration){
            m_logger->e("{}) {}:{} : can't get duration from a content. duration: ({}), handle({})",
                        TAG, __FUNCTION__, __LINE__, info.duration, handle);
            return false;
        }

        if (getMediaIDFromMediaIDMap(handle, media_id) == true) {

            if(isProxyConnectionAvailable()) {
                //std::function<void(const CommonAPI::CallStatus&, uint64_t handle, const PlayerTypes::PlayerError&)> callback
                //= std::bind(&PlayerEngineCCOSAdaptor::setPositionCallback, this,std::placeholders::_1, std::placeholders::_2,std::placeholders::_3);
                //playerProxy->setPositionAsync(handle, pos, callback);
                playerProxy->setPosition(pos, media_id, callStatus, error);
            } else {
                m_logger->e("{}) {}:{} : Command API connection error" , TAG, __FUNCTION__, __LINE__);
                return false;
            }
            return true;
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setPlaybackRate(const uint64_t handle, const float rate) {
    PlayerEngineAdaptationInfo info;

    if (getAdaptationInfoFromInfoMap(handle, info) == true) {
       if (info.player_type == static_cast<uint32_t>(PlayerTypes::MediaType::PODBBANG) ||
           info.player_type == static_cast<uint32_t>(PlayerTypes::MediaType::WELAAA_AUDIO_STREAMING) ||
           info.player_type == static_cast<uint32_t>(PlayerTypes::MediaType::VIBE) ||
           info.player_type == static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_FUNAUDIO) ||
           info.player_type == static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_MINI_APP_VIDEO) ||
           info.player_type == static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_MINI_APP_AUDIO)) {
            // rate 0-2x call speed with sound and ignore normal rate 1.0f as it needs to set rate instead of speed
            uint32_t media_id = 0;
            if (getMediaIDFromMediaIDMap(handle, media_id) == false) {
                m_logger->e("{}) {}:{} : Failed to get media id", TAG, __FUNCTION__, __LINE__);
                return false;
            }

            m_logger->i("{}) {}:{} : SetPlaybackRate-rate attribute ({}) speedSet ({})", TAG, __FUNCTION__, __LINE__, rate, isSpeedSet);
            if ((fabs(rate - 0.0f) > DBL_EPSILON) && (fabs(rate) <= (2.0f + DBL_EPSILON))) {
                if((fabs(rate - 1.0f) < DBL_EPSILON && !isSpeedSet) ) {
                    return setPlaybackRateWithMute(handle, rate);
                }
                // set the dummy rate attr to zero to handle next rate 1.0 change after speed attr change
                std::vector<PlayerTypes::Rate> rate_t;
                std::vector<PlayerTypes::Rate>::iterator itr;
                CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::INVALID_VALUE;
                std::vector<PlayerTypes::Rate> response;

                int32_t Idx = getRateAttrIndex(media_id, rate_t);
                if (Idx <= -1 || (std::size_t)Idx >= rate_t.size()) {
                    m_logger->e("{}) {}:{} : Error[{}]-rate attribute is out-dated({})", TAG, __FUNCTION__, __LINE__, handle, rate);
                    return false;
                }

                rate_t[Idx].setMedia_id(media_id);
                rate_t[Idx].setRate((double)0.0);
                for (itr = rate_t.begin(); itr != rate_t.end(); itr++) {
                    if((itr - rate_t.begin()) == Idx) {
                        itr->setActive(true);
                    } else {
                        itr->setActive(false);
                    }
                }
                playerProxy->getRateAttribute().setValue(rate_t, callStatus, response);
                if((fabs(rate - 1.0f) < DBL_EPSILON)) {
                  //reset the speed setting flag as change it to normal rate 1.0
                  isSpeedSet = false;
                } else {
                  //set the speed setting flag
                  isSpeedSet = true;
                }
                return setPlaybackRateWithUnmute(handle, rate);
            } else { //call FF/REW without sound for rates > 2x
                // set the dummy speed attr to zero to handle previous speed change after rate attr change
                std::vector<PlayerTypes::Speed> response;
                std::vector<PlayerTypes::Speed> speed_t;
                std::vector<PlayerTypes::Speed>::iterator itr;
                CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::INVALID_VALUE;
                if(isProxyConnectionAvailable()){
                    int32_t Idx = getSpeedAttrIndex(media_id, speed_t);

                    if (Idx <= -1 || (std::size_t)Idx >= speed_t.size()) {
                        m_logger->e("{}) {}:{} : Error[{}]-rate attribute is out-dated({})", TAG, __FUNCTION__, __LINE__, handle, rate);
                        return false;
                    }

                    speed_t[Idx].setMedia_id(media_id);
                    speed_t[Idx].setSpeed((double)0.0);
                    for(itr = speed_t.begin(); itr != speed_t.end(); itr++) {
                        if((itr - speed_t.begin()) == Idx) {
                            itr->setActive(true);
                        } else {
                            itr->setActive(false);
                        }
                    }
                    playerProxy->getSpeedAttribute().setValue(speed_t, callStatus, response);
                }
                //reset the speed setting flag
                isSpeedSet = false;
                return setPlaybackRateWithMute(handle, rate);
            }
        } else {
            return setPlaybackRateWithMute(handle, rate);
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setPlaybackRateWithMute(const uint64_t handle, const float rate) {
    std::vector<PlayerTypes::Rate> response;
    std::vector<PlayerTypes::Rate> rate_t;
    std::vector<PlayerTypes::Rate>::iterator itr;
    CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::INVALID_VALUE;
    CommonAPI::CallInfo callInfo(1000);
    PlayerEngineAdaptationInfo info;
    uint32_t media_id = 0;
    if (getAdaptationInfoFromInfoMap(handle, info) == true) {
        if ((info.player_type == static_cast<uint32_t>(PlayerTypes::MediaType::KAOLAFM) ||
             info.player_type == static_cast<uint32_t>(PlayerTypes::MediaType::QQMUSIC)) &&
             info.playerStatus == ccos::media::HMediaPlayingState::UNINIT) {
            m_logger->e("{}) {}:{} : player instance is not ready..", TAG, __FUNCTION__, __LINE__);
            return false;
        }

        if (getMediaIDFromMediaIDMap(handle, media_id) == false) {
            m_logger->e("{}) {}:{} : Failed to get media id", TAG, __FUNCTION__, __LINE__);
            return false;
        }

        if(isProxyConnectionAvailable()){
            int32_t Idx = getRateAttrIndex(media_id, rate_t);
            //CID : 13235991
            if (Idx <= -1 || (std::size_t)Idx >= rate_t.size()) {
                m_logger->e("{}) {}:{} : Error[{}]-rate attribute is out-dated({})", TAG, __FUNCTION__, __LINE__, handle, rate);
                return false;
            }

            if ((fabs(rate - 1.0) <= DBL_EPSILON) && (fabs(rate_t[Idx].getRate() - 1.0) <= DBL_EPSILON)) {
                m_logger->i("{}) {}:{} : Set unmute for safety", TAG, __FUNCTION__, __LINE__);

                std::vector<PlayerTypes::MuteOption> mute_response;
                std::vector<PlayerTypes::MuteOption> mute_t;
                std::vector<PlayerTypes::MuteOption>::iterator m_itr;

                int32_t mute_Idx = getMuteAttrIndex(media_id, mute_t);
                //CID : 13236004
                if (mute_Idx <= -1 || (std::size_t)mute_Idx >= mute_t.size()) {
                    m_logger->e("{}) {}:{} : Error[{}]-mute attribute is out-dated", TAG, __FUNCTION__, __LINE__, handle);
                    return false;
                }
                mute_t[mute_Idx].setMedia_id(media_id);
                mute_t[mute_Idx].setStatus(PlayerTypes::MuteStatus::UNMUTED);

                for(m_itr = mute_t.begin(); m_itr != mute_t.end(); m_itr++) {
                    if((m_itr - mute_t.begin()) == mute_Idx) {
                        m_itr->setActive(true);
                    } else {
                        m_itr->setActive(false);
                    }
                }
                playerProxy->getMuteAttribute().setValue(mute_t, callStatus, mute_response, &callInfo);
                if (callStatus != CommonAPI::CallStatus::SUCCESS) {
                    m_logger->e("{}) {} : setMute Attr fail - m_id=[{}]" ,TAG, __FUNCTION__, media_id);
                }
            }
            rate_t[Idx].setMedia_id(media_id);
            rate_t[Idx].setRate((double)rate);
            for(itr = rate_t.begin(); itr != rate_t.end(); itr++) {
                if((itr - rate_t.begin()) == Idx) {
                    itr->setActive(true);
                } else {
                    itr->setActive(false);
                }
            }
            playerProxy->getRateAttribute().setValue(rate_t, callStatus, response, &callInfo);
            if (callStatus != CommonAPI::CallStatus::SUCCESS) {
                m_logger->e("{}) {} : setRate Attr fail - m_id=[{}]" ,TAG, __FUNCTION__, media_id);
                return false;
            }
            return true;
        } else {
            m_logger->e("{}) {}:{} : Command API connection error" ,TAG, __FUNCTION__, __LINE__);
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setPlaybackRateWithUnmute(const uint64_t handle, const float rate) {

    std::vector<PlayerTypes::Speed> response;
    std::vector<PlayerTypes::Speed> speed_t;
    std::vector<PlayerTypes::Speed>::iterator itr;
    CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::INVALID_VALUE;
    CommonAPI::CallInfo callInfo(1000);
    PlayerEngineAdaptationInfo info;
    uint32_t media_id = 0;

    if (getAdaptationInfoFromInfoMap(handle, info) == true) {
        if ((info.player_type == static_cast<uint32_t>(PlayerTypes::MediaType::KAOLAFM) ||
             info.player_type == static_cast<uint32_t>(PlayerTypes::MediaType::QQMUSIC)) &&
             info.playerStatus == ccos::media::HMediaPlayingState::UNINIT) {
            m_logger->e("{}) {}:{} : player instance is not ready..", TAG, __FUNCTION__, __LINE__);
            return false;
        }

        if (getMediaIDFromMediaIDMap(handle, media_id) == false) {
            m_logger->e("{}) {}:{} : Failed to get media id", TAG, __FUNCTION__, __LINE__);
            return false;
        }

        if(isProxyConnectionAvailable()){
            int32_t Idx = getSpeedAttrIndex(media_id, speed_t);

            if (Idx <= -1 || (std::size_t)Idx >= speed_t.size()) {
                m_logger->e("{}) {}:{} : Error[{}]-rate attribute is out-dated({})", TAG, __FUNCTION__, __LINE__, handle, rate);
                return false;
            }

            speed_t[Idx].setMedia_id(media_id);
            speed_t[Idx].setSpeed((double)rate);
            for(itr = speed_t.begin(); itr != speed_t.end(); itr++) {
                if((itr - speed_t.begin()) == Idx) {
                    itr->setActive(true);
                } else {
                    itr->setActive(false);
                }
            }
            playerProxy->getSpeedAttribute().setValue(speed_t, callStatus, response, &callInfo);
            if (callStatus != CommonAPI::CallStatus::SUCCESS) {
                m_logger->e("{}) {} : setSpeed Attr fail - m_id=[{}]" ,TAG, __FUNCTION__, media_id);
                return false;
            }
            return true;
        } else {
            m_logger->e("{}) {}:{} : Command API connection error" ,TAG, __FUNCTION__, __LINE__);
        }
    }
    return false;
}

boost::optional<uint64_t> PlayerEngineCCOSAdaptor::getPosition(uint64_t handle) {

    m_logger->i("{}) {}:{} : called getPosition({})", TAG, __FUNCTION__, __LINE__, handle);
    PlayerEngineAdaptationInfo info;

    if (getAdaptationInfoFromInfoMap(handle, info) == true) {
        if(info.playerStatus == ccos::media::HMediaPlayingState::PAUSED || info.playerStatus == ccos::media::HMediaPlayingState::PLAYING) {
            return info.position;
        }
    }
    m_logger->e("{}) {}:{} : player state Error", TAG,__FUNCTION__, __LINE__);
    return boost::none;
}

boost::optional<uint64_t> PlayerEngineCCOSAdaptor::getDuration(uint64_t handle) {
    CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::INVALID_VALUE;
    std::vector<PlayerTypes::Duration> duration_t;
    PlayerEngineAdaptationInfo info;
    uint64_t duration = 0;
    uint32_t media_id = 0;

    m_logger->i("{}) {}:{} : called getDuration({})", TAG, __FUNCTION__, __LINE__, handle);
    if (getAdaptationInfoFromInfoMap(handle, info) == true) {
        if ((info.player_type == static_cast<uint32_t>(PlayerTypes::MediaType::KAOLAFM) ||
             info.player_type == static_cast<uint32_t>(PlayerTypes::MediaType::QQMUSIC)) &&
             info.playerStatus == ccos::media::HMediaPlayingState::UNINIT) {
            m_logger->e("{}) {}:{} : player instance is not ready..", TAG, __FUNCTION__, __LINE__);
            return boost::none;
        }

        if (getMediaIDFromMediaIDMap(handle, media_id) == false) {
            return boost::none;
        }

        if(isProxyConnectionAvailable()){
            int32_t duration_index = getDurationAttrIndex(media_id, duration_t);
            //CID : 13235989
            if (duration_index <= -1 || (std::size_t)duration_index >= duration_t.size()) {
                m_logger->e("{}) {}:{} : Error[{}]-duration attribute is out-dated", TAG, __FUNCTION__, __LINE__, handle);
                return boost::none;
            }
            duration = duration_t[duration_index].getDuration();

            if (info.player_type == static_cast<uint32_t>(PlayerTypes::MediaType::AUDIO)) {
                m_logger->i("{}) {}:{} : Acquire duration using DB", TAG, __FUNCTION__, __LINE__);
                uint64_t duration_db = getAudioDurationByDatabase(info.url);

                if ((duration_db > 0) &&
                    (duration_db > duration+1000000ULL || duration_db+1000000ULL < duration)) {
                    // Update duration only when there is a gap more than 1 sec.
                    if (duration > 0 && (duration_db > duration+200000000ULL)) {
                        // Abnormal case. There is a gap more than 200 seconds -> Unreliable DB value. (Do nothing)
                    } else {
                        m_logger->i("{}) {}:{} : Acquire duration using DB.. updated {}->{}", TAG, __FUNCTION__, __LINE__, duration, duration_db);
                        duration = duration_db;
                    }
                }
            }
            m_logger->i("{}) {}:{} : Duration=[{}]", TAG, __FUNCTION__, __LINE__, duration);
            return duration;
        } else {
            m_logger->i("{}) {}:{} : CommonAPI Connection Error", TAG, __FUNCTION__, __LINE__);
        }
    }
    return boost::none;
}

boost::optional<ccos::media::HMediaPlayingState> PlayerEngineCCOSAdaptor::getPlayingStatus(uint64_t handle) {
    ccos::media::HMediaPlayingState status;

    if (getPlayerStatusFromInfoMap(handle, status) == true) {
        return status;
    }
    return boost::none;
}

void PlayerEngineCCOSAdaptor::loadCallback(const CommonAPI::CallStatus& status, uint64_t rhandle, const PlayerTypes::PlayerError& error) {
}

void PlayerEngineCCOSAdaptor::playCallback(const CommonAPI::CallStatus& status, uint64_t rhandle, const PlayerTypes::PlayerError& error) {
}

void PlayerEngineCCOSAdaptor::stopCallback(const CommonAPI::CallStatus& status, uint64_t rhandle, const PlayerTypes::PlayerError& error) {
}

void PlayerEngineCCOSAdaptor::pauseCallback(const CommonAPI::CallStatus& status, uint64_t rhandle, const PlayerTypes::PlayerError& error) {
}

void PlayerEngineCCOSAdaptor::resetCallback(const CommonAPI::CallStatus& status, const PlayerTypes::PlayerError& error) {
}

void PlayerEngineCCOSAdaptor::setMediaTypeCallback(const CommonAPI::CallStatus& status, const PlayerTypes::MediaType Type) {
    m_logger->e("{}) {}:{} : setmediaType Callback, mediatype=[%d]",TAG, __FUNCTION__, __LINE__, static_cast<uint32_t>(Type));
}

void PlayerEngineCCOSAdaptor::positionChangedCallback(std::vector<PlayerTypes::Position> position) {
    for(auto itr : position) {
        if(itr.getActive()) {
            uint32_t media_id = itr.getMedia_id();
            uint64_t handle = LLONG_MAX;

            if(getHandleFromMediaIDMap(media_id, handle) == true) {
                positionChangedCallbackInternal(itr, handle);
                itr.setActive(false);
            }
        }
    }
}

void PlayerEngineCCOSAdaptor::positionChangedCallbackInternal(PlayerTypes::Position position, uint64_t handle) {
    uint64_t pos = position.getPosition();

    if (setPositionToInfoMap(handle, pos) == true) {
        if(PlayerContextStatusCallbackMap.find(handle) != PlayerContextStatusCallbackMap.end()){
            if(PlayerContextStatusCallbackMap[handle].Listener != nullptr){
                std::lock_guard<std::mutex> lock(PlayerContextStatusCallbackMap[handle].m);
                PlayerContextStatusCallbackMap[handle].Listener->onPlayingTimeChanged(handle, static_cast<ccos::HUInt64>(pos));
                m_logger->i("{}) {}:{} : handle({}), onPlayingTimeChanged({})", TAG, __FUNCTION__, __LINE__, handle, static_cast<ccos::HUInt64>(pos));
            }
        } else {
            m_logger->e("{}) {}:{} : Error-Status Callback map value is null", TAG, __FUNCTION__, __LINE__);
        }
    }
}

void PlayerEngineCCOSAdaptor::bufferingChangedCallback(std::vector<PlayerTypes::Buffering> buff) {
    for(auto itr : buff) {
        if(itr.getActive()) {
            uint32_t media_id = itr.getMedia_id();
            uint64_t handle = LLONG_MAX;

            if(getHandleFromMediaIDMap(media_id, handle) == true) {
                bufferingChangedCallbackInternal(itr, handle);
                itr.setActive(false);
            }
        }
    }
}

void PlayerEngineCCOSAdaptor::bufferingChangedCallbackInternal(PlayerTypes::Buffering buff, uint64_t handle) {
    ccos::HUInt32 total = 100;
    ccos::HUInt32 progress = (ccos::HUInt32)buff.getProgress();

    if (progress > total) {
        m_logger->i("{}) {}:{} : handle({}), Invalid buffer progress({}).. set as total", TAG, __FUNCTION__, __LINE__, handle, buff.getProgress());
        progress = total;
    }

    if (PlayerContextStatusCallbackMap.find(handle) != PlayerContextStatusCallbackMap.end()) {
        if (PlayerContextStatusCallbackMap[handle].Listener != nullptr) {
            std::lock_guard<std::mutex> lock(PlayerContextStatusCallbackMap[handle].m);
            PlayerContextStatusCallbackMap[handle].Listener->onStreamingBuffering(progress, total);
            m_logger->i("{}) {}:{} : handle({}), Buffering progress({})", TAG, __FUNCTION__, __LINE__, handle, buff.getProgress());
        } else {
            m_logger->e("{}) {}:{} : Error, Listener is null", TAG, __FUNCTION__, __LINE__);
        }
    }
}

void PlayerEngineCCOSAdaptor::ChangeDurationCallback(std::vector<PlayerTypes::Duration> duration) {
    for(auto itr : duration) {
        if(itr.getActive()) {
            uint32_t media_id = itr.getMedia_id();
            uint64_t handle = LLONG_MAX;
            {
                std::unique_lock<std::mutex> l(condLoadLock);
                condLoadWait.wait_for(l, std::chrono::milliseconds(500), [this]{ return isLoadFinished; });
            }

            if(getHandleFromMediaIDMap(media_id, handle) == true) {
                ChangeDurationCallbackInternal(itr, handle);
                itr.setActive(false);
            }
        }
    }
}

void PlayerEngineCCOSAdaptor::ChangeDurationCallbackInternal(PlayerTypes::Duration duration, uint64_t handle) {
    PlayerEngineAdaptationInfo info;

    if (getAdaptationInfoFromInfoMap(handle, info)) {
        if (setDurationToInfoMap(handle, duration.getDuration()) == true) {
            if(info.player_type == static_cast<uint32_t> (PlayerTypes::MediaType::STREAM)) {
                if (mTranscoderListener != nullptr) {
                    mTranscoderListener->setDuration(duration.getDuration());
                    m_logger->i("{}) {}:{} : Set transcode file duration=[{}]", TAG, __FUNCTION__, __LINE__, duration.getDuration());
                }
            }
        }
    }
}

void PlayerEngineCCOSAdaptor::ChangeRateCallback(const CommonAPI::CallStatus& status, double rate) {
    ChangeRateCallbackInternal(rate, DEFAULT_PLAYER_CONTEXT);
}

void PlayerEngineCCOSAdaptor::ChangeRateCallbackInternal(double rate, uint32_t context_number) {
    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);

    auto iter = PlayerEngineAdaptInfoMap.begin();

    for(; iter != PlayerEngineAdaptInfoMap.end(); iter++){
        if(((*iter).second->handle - betweenHandleContextOffset) == context_number){
            break;
        }
    }

    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : can't find the info related to context_number : {}", TAG, __FUNCTION__, __LINE__, context_number);
        return ;
    }

    auto info = (*iter).second;
    if(info != nullptr){
        info->speed = rate;
    }
}

void PlayerEngineCCOSAdaptor::setPositionCallback(const CommonAPI::CallStatus& status, const PlayerTypes::PlayerError& error) {
    m_logger->e("{}) {}:{} : setPositionCallback ", TAG, __FUNCTION__, __LINE__);
}

bool PlayerEngineCCOSAdaptor::PlaybackErrorCallbackInternal(uint64_t handle, PlaybackContentError error) {
    m_logger->e("{}) {}:{} : Error[{}]", TAG, __FUNCTION__, __LINE__, static_cast<int32_t>(error));

    uint32_t media_id = UINT_MAX;

    if (getMediaIDFromMediaIDMap(handle, media_id) == true) {
        PlayerEngineAdaptationInfo info;
        ccos::media::HMediaPlayingState HStatus = ccos::media::HMediaPlayingState::FAILED_TO_PLAY;

        m_logger->e("{}) {}:{} : Error-mediaId ({})", TAG, __FUNCTION__, __LINE__, media_id);
        if (getAdaptationInfoFromInfoMap(handle, info)) {
            switch(error){
                case PlaybackContentError::FILE_NOT_SUPPORTED:
                case PlaybackContentError::DIVX_RESOLUTION_NOT_SUPPORTED:
                case PlaybackContentError::VIDEO_RESOLUTION_NOT_SUPPORTED:
                    HStatus = ccos::media::HMediaPlayingState::FAILED_UNSUPPORTED_CODEC_VIDEO;
                break;

                case PlaybackContentError::PLAYBACK_INTERNAL_ERROR:
                case PlaybackContentError::BAD_URI:
                case PlaybackContentError::DIVX_UNAUTHORIZED:
                case PlaybackContentError::DIVX_CODEC_NOT_SUPPORTED:
                    HStatus = ccos::media::HMediaPlayingState::FAILED_TO_PLAY;
                    break;

                case PlaybackContentError::RESOURCE_READ_ERROR:
                    HStatus = ccos::media::HMediaPlayingState::MEDIA_REMOVED;
                    break;

                case PlaybackContentError::WARNING_AUDIO_CODEC_NOT_SUPPORTED:
                    HStatus = ccos::media::HMediaPlayingState::FAILED_UNSUPPORTED_CODEC_AUDIO;
                    break;
                case PlaybackContentError::HTTP_ERROR_RESPONSE_403:
                    HStatus = ccos::media::HMediaPlayingState::HTTP_ERROR_RESPONSE_403;
                    break;
                case PlaybackContentError::HTTP_ERROR_RESPONSE_500:
                    HStatus = ccos::media::HMediaPlayingState::HTTP_ERROR_RESPONSE_500;
                    break;
                case PlaybackContentError::HTTP_ERROR_RESPONSE_501:
                    HStatus = ccos::media::HMediaPlayingState::HTTP_ERROR_RESPONSE_501;
                    break;
                case PlaybackContentError::HTTP_ERROR_RESPONSE_502:
                    HStatus = ccos::media::HMediaPlayingState::HTTP_ERROR_RESPONSE_502;
                    break;
                case PlaybackContentError::HTTP_ERROR_RESPONSE_503:
                    HStatus = ccos::media::HMediaPlayingState::HTTP_ERROR_RESPONSE_503;
                    break;
                case PlaybackContentError::HTTP_ERROR_RESPONSE_504:
                    HStatus = ccos::media::HMediaPlayingState::HTTP_ERROR_RESPONSE_504;
                    break;
                case PlaybackContentError::HTTP_ERROR_RESPONSE_505:
                    HStatus = ccos::media::HMediaPlayingState::HTTP_ERROR_RESPONSE_505;
                    break;
                case PlaybackContentError::HTTP_ERROR_RESPONSE_506:
                    HStatus = ccos::media::HMediaPlayingState::HTTP_ERROR_RESPONSE_506;
                    break;
                case PlaybackContentError::HTTP_ERROR_RESPONSE_507:
                    HStatus = ccos::media::HMediaPlayingState::HTTP_ERROR_RESPONSE_507;
                    break;

                default :
                    break;
            }

            setPlayerStatusToInfoMap(handle, HStatus);

            if(HStatus == ccos::media::HMediaPlayingState::MEDIA_REMOVED) {
                if((info.player_type == static_cast<uint32_t>(PlayerTypes::MediaType::AUDIO)) || (info.player_type == static_cast<uint32_t>(PlayerTypes::MediaType::VIDEO))) {
                    m_logger->e("{}) {}:{} : RESOURCE_READ_ERROR occurred in gstreamer", TAG, __FUNCTION__, __LINE__);
                    usleep(300*1000);

                    if(validateFile(info.url) == false) {
                        m_logger->e("{}) {}:{} : The file doesn't exist", TAG, __FUNCTION__, __LINE__);
                        return true;
                    } else {
                        m_logger->e("{}) {}:{} : The file exists", TAG, __FUNCTION__, __LINE__);
                        HStatus = ccos::media::HMediaPlayingState::FAILED_TO_PLAY;
                        setPlayerStatusToInfoMap(handle, HStatus);
                    }
                } else {
                    HStatus = ccos::media::HMediaPlayingState::FAILED_TO_PLAY;
                    setPlayerStatusToInfoMap(handle, HStatus);
                }
            }

            if(PlayerContextStatusCallbackMap.find(handle) != PlayerContextStatusCallbackMap.end()){
                std::lock_guard<std::mutex> lock(PlayerContextStatusCallbackMap[handle].m);
                if (PlayerContextStatusCallbackMap[handle].Listener != nullptr) {
                    PlayerContextStatusCallbackMap[handle].Listener->onPlayingStateChanged(handle, HStatus);
                    m_logger->i("{}) {}:{} : handle({}), onPlayingStatusChanged({})",
                            TAG, __FUNCTION__, __LINE__, handle, getHMediaPlayingStateString(HStatus));
                } else {
                    m_logger->i("{}) {}:{} : handle({}), onPlayingStatusChanged({}) failed. Listener is empty",
                            TAG, __FUNCTION__, __LINE__, handle, getHMediaPlayingStateString(HStatus));
                }
                return true;
            }
        } else {
            m_logger->e("{}) {}:{} : Error-can't find the information related to context_number", TAG, __FUNCTION__, __LINE__);
        }
    } else {
        m_logger->e("{}:{} : Media id is not present in the map", __FUNCTION__, __LINE__);
    }
    return false;
}

PlaybackContentError PlayerEngineCCOSAdaptor::ConvertPlaybackErrorToPlaybackContentError(PlayerTypes::PlaybackError error) {
    PlaybackContentError err = PlaybackContentError::MAX;

    switch(error){
        case PlayerTypes::PlaybackError::FILE_NOT_SUPPORTED :
            err = PlaybackContentError::FILE_NOT_SUPPORTED;
            break;
        case PlayerTypes::PlaybackError::DIVX_CODEC_NOT_SUPPORTED:
            err = PlaybackContentError::DIVX_CODEC_NOT_SUPPORTED;
            break;
        case PlayerTypes::PlaybackError::DIVX_RESOLUTION_NOT_SUPPORTED:
            err = PlaybackContentError::DIVX_RESOLUTION_NOT_SUPPORTED;
            break;
        case PlayerTypes::PlaybackError::VIDEO_RESOLUTION_NOT_SUPPORTED:
            err = PlaybackContentError::VIDEO_RESOLUTION_NOT_SUPPORTED;
            break;
        case PlayerTypes::PlaybackError::PLAYBACK_INTERNAL_ERROR :
            err = PlaybackContentError::PLAYBACK_INTERNAL_ERROR;
            break;
        case PlayerTypes::PlaybackError::RESOURCE_READ_ERROR:
            err = PlaybackContentError::RESOURCE_READ_ERROR;
            break;
        case PlayerTypes::PlaybackError::BAD_URI :
            err = PlaybackContentError::BAD_URI;
            break;
        case PlayerTypes::PlaybackError::DIVX_UNAUTHORIZED:
            err = PlaybackContentError::DIVX_UNAUTHORIZED;
            break;
        case PlayerTypes::PlaybackError::NO_RESPONSE_ERROR:
            err = PlaybackContentError::MAX;
            break;
        case PlayerTypes::PlaybackError::HTTP_ERROR_RESPONSE_403:
            err = PlaybackContentError::HTTP_ERROR_RESPONSE_403;
            break;
        case PlayerTypes::PlaybackError::HTTP_ERROR_RESPONSE_500:
            err = PlaybackContentError::HTTP_ERROR_RESPONSE_500;
            break;
        case PlayerTypes::PlaybackError::HTTP_ERROR_RESPONSE_501:
            err = PlaybackContentError::HTTP_ERROR_RESPONSE_501;
            break;
        case PlayerTypes::PlaybackError::HTTP_ERROR_RESPONSE_502:
            err = PlaybackContentError::HTTP_ERROR_RESPONSE_502;
            break;
        case PlayerTypes::PlaybackError::HTTP_ERROR_RESPONSE_503:
            err = PlaybackContentError::HTTP_ERROR_RESPONSE_503;
            break;
        case PlayerTypes::PlaybackError::HTTP_ERROR_RESPONSE_504:
            err = PlaybackContentError::HTTP_ERROR_RESPONSE_504;
            break;
        case PlayerTypes::PlaybackError::HTTP_ERROR_RESPONSE_505:
            err = PlaybackContentError::HTTP_ERROR_RESPONSE_505;
            break;
        case PlayerTypes::PlaybackError::HTTP_ERROR_RESPONSE_506:
            err = PlaybackContentError::HTTP_ERROR_RESPONSE_506;
            break;
        case PlayerTypes::PlaybackError::HTTP_ERROR_RESPONSE_507:
            err = PlaybackContentError::HTTP_ERROR_RESPONSE_507;
            break;
        default:
            m_logger->e("{}) {}:{} : Error({})", TAG, __FUNCTION__, __LINE__, static_cast<int32_t>(err));
            break;
    }

    return err;
}

std::string PlayerEngineCCOSAdaptor::getVolumeTypeAndSrcId(uint32_t player_type, uint16_t *src_id) {
    if (src_id == NULL) {
        m_logger->e("{}) {}:{} : src_id is missing", TAG, __FUNCTION__, __LINE__);
        return "";
    }

    switch (player_type) { // Must be sync with 'AudioDatabaseTypes.h' in HAudio
        case static_cast<uint32_t>(PlayerTypes::MediaType::AUDIO):
            *src_id = 0x30;
            return "usb_audio_vol_0";
        case static_cast<uint32_t>(PlayerTypes::MediaType::VIDEO):
            *src_id = 0x32;
            return "usb_video_vol_0";
        case static_cast<uint32_t>(PlayerTypes::MediaType::NATURE_SOUND):
            *src_id = 0x49;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::KAOLAFM):
            *src_id = 0x4b;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::MELON):
            *src_id = 0x4a;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::QQMUSIC):
            *src_id = 0x47;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::MANUAL_VIDEO):
            *src_id = 0x48;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::KAKAOI):
            *src_id = 0x146;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::GENIE):
            *src_id = 0x4c;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::XIMALAYA):
            *src_id = 0x4d;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::MOOD_THERAPY_AUDIO):
            *src_id = 0x45;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::KIDS_VIDEO):
            *src_id = 0x14e;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::PODBBANG):
            *src_id = 0x4f;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::KAKAOI2):
            *src_id = 0x15a;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::FACE_DETECTION):
            *src_id = 0x1a3;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::DVRS_FRONT):
            *src_id = 0x13a;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::VIBE):
            *src_id = 0x69;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_FUNAUDIO):
            *src_id = 0x6A;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_MINI_APP_AUDIO):
            *src_id = 0x6B;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_MINI_APP_VIDEO):
            *src_id = 0x6B;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::WELAAA_AUDIO_STREAMING):
            *src_id = 0x6E;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::GENESIS_AUDIO_STREAMING):
            *src_id = 0x6F;
            return "";
        case static_cast<uint32_t>(PlayerTypes::MediaType::KAKAOI3):
            *src_id = 0x70;
            return "";
        default:
            m_logger->e("{}) {}:{} : Error-wrong player_type : {})", TAG, __FUNCTION__, __LINE__, player_type);
            *src_id = 0x00;
            return "";
    }
}

int PlayerEngineCCOSAdaptor::setPacket(char* buffer, char** address, bool setting, void* data) {
#pragma pack(push,1)
    struct dns_header { // 12 byte
        uint16_t trans_id;
        uint16_t flags;
        uint16_t question;
        uint16_t ans_rrs;
        uint16_t auth_rrs;
        uint16_t other_rrs;
    };

    struct dns_answer {
        uint16_t name; // 16 byte
        uint16_t type;
        uint16_t klass;
        uint32_t ttl;
        uint16_t len;
        uint8_t data[];
    };
#pragma pack(pop)

    PlayerEngineCCOSAdaptor* instance = static_cast<PlayerEngineCCOSAdaptor*>(data);

    if (setting) {
        /* Setting */

        struct dns_header* header = (struct dns_header*) buffer;
        int32_t size = 2;
        char* start = buffer;

        if(header == nullptr) {
            m_logger->e("{}) {}:{} : header is empty.", TAG, __FUNCTION__, __LINE__);
            return -1;
        }

        header->trans_id = static_cast<uint16_t>((reinterpret_cast<std::uintptr_t>(address[0]))^0x5555);
        header->flags = htons(0x0100);
        header->question = htons(1);
        header->ans_rrs = 0;
        header->auth_rrs = 0;
        header->other_rrs = 0;
        buffer += 12;

        while(size) {
            int index = 0;
            char* ref = address[0];
            while(*ref) {
                if(*ref == '.') {
                    break;
                }
                index++;
                ref++;
            }
            size = index;
            m_logger->e("{}) {}:{} : parse address :{}, index:{}", TAG, __FUNCTION__, __LINE__, (char *)address[0], index);
            *buffer = static_cast<uint8_t>(size);
            buffer++;
            memcpy(buffer, address[0], size);
            address[0]+=size;
            buffer+=size;

            if(!*address[0]) {
                break;
            }

            address[0]++;
        }

        *buffer = 0;
        buffer++;
        *buffer = 0;
        buffer++;
        *buffer = 1;
        buffer++;
        *buffer = 0;
        buffer++;
        *buffer = 1;
        buffer++;
        m_logger->e("{}) {}:{} : address buf ret : {}", TAG, __FUNCTION__, __LINE__, (reinterpret_cast<std::uintptr_t>(buffer)-reinterpret_cast<std::uintptr_t>(start)));

        return reinterpret_cast<std::uintptr_t>(buffer)-reinterpret_cast<std::uintptr_t>(start);

    } else {
/*
 * If Memory over flow occur, We need to re-allocate memory to buffer.
 */
        /* get DNS address */
        uint8_t size = 0;
        uint16_t count = 0;
        int32_t addr_len = 0;
        size_t current_size = 0;
        struct dns_header* header = NULL;
        struct dns_answer* header_answer = NULL;
        char* value = NULL;
        char dns_name[64] = {0,};

        header = (struct dns_header*)buffer;

        if(header != NULL) {
            instance->m_logger->e("{}) {}:{} : Status value : [{}] , count: [{}] ", TAG, __FUNCTION__, __LINE__, ntohs(header->flags), ntohs(header->ans_rrs));
            instance->m_logger->e("{}) {}:{} : Status : [{}] found", TAG, __FUNCTION__, __LINE__, (ntohs(header->flags)&0x0007)? "Not" : "");
            count = ntohs(header->ans_rrs);
            instance->m_logger->e("{}) {}:{} : Answer : [{}]", TAG, __FUNCTION__, __LINE__, count);

            buffer += 12;
            current_size += 12;

            while(*buffer){
                uint32_t temp = *buffer + 1;
                buffer += temp;
                current_size += temp;
            }
            buffer += 5;
            current_size += 5;

            instance->m_logger->e("{}) {}:{} : Current Buffer Size including header : [{}]", TAG, __FUNCTION__, __LINE__, current_size);

            header_answer = (struct dns_answer*)buffer;

            if(header_answer != NULL) {
                for(int index = 0; index < count; index++) {

                    if (current_size + 12 >= DNS_QUERY_BUFFER_SIZE ) {
                        instance->m_logger->e("{}) {}:{} : Buffer overflow, Current Buffer Size : [{}]", TAG, __FUNCTION__, __LINE__, current_size);
                        return -1;
                    } else {
                        current_size += 12; // Size of struct dns_answer except for data[]
                        instance->m_logger->e("{}) {}:{} : Current Buffer Size, including header_answer: [{}]", TAG, __FUNCTION__, __LINE__, current_size);
                    }
                    instance->m_logger->e("{}) {}:{} : index : [{}]", TAG, __FUNCTION__, __LINE__, index);
                    header_answer->type = ntohs(header_answer->type);
                    instance->m_logger->e("{}) {}:{} : type : [{}]", TAG, __FUNCTION__, __LINE__, header_answer->type);
                    header_answer->klass = ntohs(header_answer->type);
                    instance->m_logger->e("{}) {}:{} : klass : [{}]", TAG, __FUNCTION__, __LINE__, header_answer->klass);
                    header_answer->ttl = ntohl(header_answer->ttl);
                    instance->m_logger->e("{}) {}:{} : ttl : [{}]", TAG, __FUNCTION__, __LINE__, header_answer->ttl);
                    header_answer->len = ntohs(header_answer->len);
                    instance->m_logger->e("{}) {}:{} : len : [{}]", TAG, __FUNCTION__, __LINE__, header_answer->len);

                    if (current_size + (header_answer->len) >= DNS_QUERY_BUFFER_SIZE ) {
                        instance->m_logger->e("{}) {}:{} : Buffer overflow, Current Buffer Size : [{}]", TAG, __FUNCTION__, __LINE__, current_size);
                        return -1;
                    } else {
                        current_size += header_answer->len;
                        instance->m_logger->e("{}) {}:{} : Current Buffer Size, including header_answer wih data : [{}]", TAG, __FUNCTION__, __LINE__, current_size);
                    }
                    if(header_answer->type == 0x0005) {
                        value = (char*) header_answer->data;
                        size = *value;

                        do {
                            *value = '.';
                            value += size+1;
                            size = *value;
                            if(size == 0xc0) {
                                size = 0;
                            }
                        } while(size);

                        *value = 0;

                        snprintf(dns_name, sizeof(dns_name) ,"%s", (char*)header_answer->data+1);
                        instance->m_logger->e("{}) {}:{} : name : [{}]", TAG, __FUNCTION__, __LINE__, dns_name);
                    }

                    if(header_answer->type == 0x0001) {
                        if (addr_len < 30) {
                            address[addr_len] = new char[20];
                            if (address[addr_len] == nullptr) {
                                instance->m_logger->e("{}) {}:{} : Can't allocate the memory", TAG, __FUNCTION__, __LINE__);
                                continue;
                            }

                            snprintf(address[addr_len], sizeof(char)*20 ,"%d.%d.%d.%d", header_answer->data[0], header_answer->data[1], header_answer->data[2], header_answer->data[3]);
                            instance->m_logger->e("{}) {}:{} : address : [{}]", TAG, __FUNCTION__, __LINE__, address[addr_len]);
                            addr_len += 1;
                        } else {
                            return addr_len;
                        }
                    }

                    buffer += header_answer->len+12;
                    header_answer = (struct dns_answer*) buffer;
                }
                instance->m_logger->e("{}) {}:{} : Used Buffer Size : [{}]", TAG, __FUNCTION__, __LINE__, current_size);

                return addr_len;
            }
        }
    }

    return -1;
}

int32_t PlayerEngineCCOSAdaptor::getDNSAddress(const std::string domain, const std::string dns_server, std::vector<std::string>& address) {
    struct sockaddr_in server_addr;
    struct sockaddr_in recv_addr;
    struct timeval time_val;
    char buffer[DNS_QUERY_BUFFER_SIZE] = {0,};
    char* addr[30] = {0,};
    int32_t socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    int32_t str_len = 0;
    int32_t reply = 0;
    socklen_t size = 0;

    if (socket_fd < 0) {
        m_logger->e("{}) {}:{} : failed to open socket", TAG, __FUNCTION__, __LINE__);
        return -1;
    }

    if((str_len = domain.size()) <= 0) {
        m_logger->e("{}) {}:{} : domain name is empty.", TAG, __FUNCTION__, __LINE__);
        close(socket_fd);
        return -1;
    }

    addr[0] = const_cast<char*>(domain.c_str());
    str_len = setPacket(buffer, addr, true, this);

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(dns_server.c_str());
    server_addr.sin_port = htons(53);

    if(server_addr.sin_addr.s_addr == INADDR_NONE) {
        m_logger->e("{}) {}:{} : Bad server address.", TAG, __FUNCTION__, __LINE__);
        close(socket_fd);
        return -1;
    }

    if((reply = sendto(socket_fd, buffer, str_len, 0, (struct sockaddr*)&server_addr, sizeof(server_addr))) <= 0) {
        m_logger->e("{}) {}:{} : Failed to send a message, reply=[{}]", TAG, __FUNCTION__, __LINE__, reply);
        close(socket_fd);
        return -1;
    }

    time_val.tv_sec = 1;
    time_val.tv_usec = 0;

    if((reply = setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &time_val, sizeof(time_val))) < 0) {
        m_logger->e("{}) {}:{} : Failed to send a message., reply=[{}]", TAG, __FUNCTION__, __LINE__, reply);
        close(socket_fd);
        return -1;
    }

    memset(buffer, 0, sizeof(char)*DNS_QUERY_BUFFER_SIZE);

    if((reply = recvfrom(socket_fd, buffer, sizeof(buffer), 0, (struct sockaddr*)&recv_addr, &size)) <= 0) {
        m_logger->e("{}) {}:{} : Failed to receive a message., reply=[{}]", TAG, __FUNCTION__, __LINE__, reply);
        close(socket_fd);
        return -1;
    }else {
        if (reply < DNS_QUERY_BUFFER_SIZE) {
            buffer[reply] = '\0';
        } else {
            m_logger->e("{}) {}:{} : Buffer overflow for dns_answer, reply=[{}]", TAG, __FUNCTION__, __LINE__, reply);
            close(socket_fd);
            return -1;
        }
    }

    int index = setPacket(buffer, addr, false, this);

    if (index > 0) {
        for(int i = 0; i < index; i++) {
            if(addr[i] != nullptr) {
                address.push_back(addr[i]);
                m_logger->i("{}) {}:{} : [{}]DNS Address = [{}]", TAG, __FUNCTION__, __LINE__, i, addr[i]);
                delete addr[i];
            }
        }
    } else {
        close(socket_fd);
        return -1;
    }

    close(socket_fd);

    return 0;
}

bool PlayerEngineCCOSAdaptor::setRouteTable(const std::string& address, const std::string& source, bool connect) {
    char command_buffer[256] = {0, };

    if(connect == true) {
        snprintf(command_buffer, sizeof(command_buffer), "iptables -t nat -A POSTROUTING --destination %s/32 -j SNAT --to-source %s", address.c_str(), source.c_str());
    } else {
        snprintf(command_buffer, sizeof(command_buffer), "iptables -t nat -D POSTROUTING --destination %s/32 -j SNAT --to-source %s", address.c_str(), source.c_str());
    }

    m_logger->i("{}) {}:{} : Routing = [{}]", TAG, __FUNCTION__, __LINE__, command_buffer);

    char* argv[] = {(char*)"sh", (char*)"-c", command_buffer, NULL};
    int status =0;
    pid_t pid = 0;

    status = posix_spawn(&pid, "/bin/sh", NULL, NULL, argv, environ);

    if(status == 0) {
        if(waitpid(pid, &status, 0) != -1) {
            m_logger->i("{}) {}:{} : Routing is running, child status[{}]", TAG, __FUNCTION__, __LINE__, status);
        } else {
            m_logger->i("{}) {}:{} : Routing is failed, child status[{}]", TAG, __FUNCTION__, __LINE__, status);
            return false;
        }
    }
    return true;
}

//Add logic to get the DNS address in ccIC27 case
DBusMessage* PlayerEngineCCOSAdaptor::CCIC_CallMethodSyncWithoutArgs(const string &name) {
    DBusMessage * msg = nullptr;
    DBusMessage * reply = nullptr;
    DBusPendingCall * pending = nullptr;
    DBusConnection * bus = nullptr;
    DBusError err;
    int timer = 1000;

    msg = dbus_message_new_method_call("com.lge.telephony.service", "/", "com.lge.telephony.service", name.c_str());

    if (msg == nullptr) {
         m_logger->i("{}) {}:{} :[DBusConnector] create method error", TAG, __FUNCTION__, __LINE__);
        goto _error;
    }

    dbus_error_init(&err);

    if (mConnectTelephony != nullptr) {
        bus = mConnectTelephony;
    } else {
        mConnectTelephony = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
        if (dbus_error_is_set(&err)) {
            m_logger->i("{}) {}:{} :[DBusConnector] err.message ({})", TAG, __FUNCTION__, __LINE__, err.message);
            dbus_error_free(&err);
            goto _error;
        }
        bus = mConnectTelephony;
    }

    dbus_error_free(&err);

    if (bus == nullptr) {
        m_logger->i("{}) {}:{} :[DBusConnector] dbus_bus_get_private error", TAG, __FUNCTION__, __LINE__);
        dbus_message_unref(msg);
        return nullptr;
    }

    if (dbus_connection_send_with_reply(bus, msg, &pending, timer) == (unsigned int)FALSE) {
        m_logger->i("{}) {}:{} :[DBusConnector] send error", TAG, __FUNCTION__, __LINE__);
        goto _error;
    }

    if (pending == nullptr) {
        m_logger->i("{}) {}:{} :[DBusConnector] pending null", TAG, __FUNCTION__, __LINE__);
        dbus_message_unref(msg);
        reply = nullptr;
        goto _error;
    }

    dbus_connection_flush(bus);
    dbus_pending_call_block(pending);
    reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);
    if (reply != nullptr && dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        m_logger->i("{}) {}:{} :[DBusConnector] call error ({})", TAG, __FUNCTION__, __LINE__, name.c_str());
        dbus_message_unref(reply);
        reply = nullptr;
        goto _error;
    }
    dbus_message_unref(msg);
    msg = nullptr;

_error:

    if (msg != nullptr) {
        dbus_message_unref(msg);
        msg = nullptr;
    }

    return reply;
}

inline std::vector<std::string> split(const std::string &str, char delim) {
    size_t i = 0;
    std::vector<std::string> list;
    size_t pos = str.find(delim);

    while (pos != string::npos) {
        list.push_back(str.substr(i, pos - i));
        i = ++pos;
        pos = str.find(delim, pos);
    }

    std::string temp = str.substr(i, str.length());
    if (temp != "") {
        list.push_back(temp);
    }
    return list;
}

bool PlayerEngineCCOSAdaptor::getCcsAddress(std::string &dns_addr, std::string &src_addr) {
    const char *group0, *group1, *group2, *group3, *group4, *group5, *group6;

    DBusMessage * reply = nullptr;
    reply = CCIC_CallMethodSyncWithoutArgs("GetServiceGroups");//reply is DBusMessage

    if (!dbus_message_get_args(reply, nullptr,
                             DBUS_TYPE_STRING, &group0, //SERVICE_GROUP_DEFAULT : "apn,status,nameserver,ipaddress,netmask,gateway"
                                       DBUS_TYPE_STRING, &group1, //SERVICE_GROUP_CCS : "apn,status,nameserver,ipaddress,netmask,gateway"
                                       DBUS_TYPE_STRING, &group2, //SERVICE_GROUP_FOTA : "apn,status,nameserver,ipaddress,netmask,gateway"
                                       DBUS_TYPE_STRING, &group3, //SERVICE_GROUP_USERPAID : "apn,status,nameserver,ipaddress,netmask,gateway"

                                       DBUS_TYPE_STRING, &group4, //SERVICE_GROUP_OEMPAID : "apn,status,nameserver,ipaddress,netmask,gateway"
                                       DBUS_TYPE_STRING, &group5, //SERVICE_GROUP_VCRM : "apn,status,nameserver,ipaddress,netmask,gateway"
                                       DBUS_TYPE_STRING, &group6, //SERVICE_GROUP_CANIDS : "apn,status,nameserver,ipaddress,netmask,gateway"
                                       DBUS_TYPE_INVALID)) {
       m_logger->e("{}) {}:{} : failed to get dbus args", TAG, __FUNCTION__, __LINE__);
       return false;
    }

    const char *dummys[6];
    const char *ccsInfo = nullptr;

    if (!dbus_message_get_args(reply, nullptr,
                                       DBUS_TYPE_STRING, &dummys[0],
                                       DBUS_TYPE_STRING, &ccsInfo,
                                       DBUS_TYPE_STRING, &dummys[1],
                                       DBUS_TYPE_STRING, &dummys[2],
                                       DBUS_TYPE_STRING, &dummys[3],
                                       DBUS_TYPE_STRING, &dummys[4],
                                       DBUS_TYPE_STRING, &dummys[5],
                                       DBUS_TYPE_INVALID)) {
       m_logger->e("{}) {}:{} : failed to get dbus args", TAG, __FUNCTION__, __LINE__);
       return false;
    }

    std::vector<std::string> tempArr = split(std::string(ccsInfo), ',');
    m_logger->i("{}) {}:{} :info [{}] - splitSize[{}]", TAG, __FUNCTION__, __LINE__, ccsInfo, tempArr.size());
    if (tempArr.size() == 6)
    {
       std::string tempCcsDns;
       std::string tempCcsApn;
       int tempStatus = 0;
       try {
           tempStatus = stoi(tempArr[1]);
       } catch (std::exception &e) {
           m_logger->i("{}) {}:{} : exception {}", TAG, __FUNCTION__, __LINE__, e.what());
           return false;
       }
       tempCcsDns = tempArr[2];
       tempCcsApn = tempArr[3];
       m_logger->i("{}) {}:{} :parsed Data : status[{}], ip[{}], dns[{}]", TAG, __FUNCTION__, __LINE__, tempStatus, tempCcsApn.c_str(), tempCcsDns.c_str());
       dns_addr = tempCcsDns;
       src_addr = tempCcsApn;
       return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getUserpaidAddress(std::string &dns_addr, std::string &src_addr) {
    const char *group0, *group1, *group2, *group3, *group4, *group5, *group6;

    DBusMessage * reply = nullptr;
    reply = CCIC_CallMethodSyncWithoutArgs("GetServiceGroups");//reply is DBusMessage

    if (!dbus_message_get_args(reply, nullptr,
                             DBUS_TYPE_STRING, &group0, //SERVICE_GROUP_DEFAULT : "apn,status,nameserver,ipaddress,netmask,gateway"
                                       DBUS_TYPE_STRING, &group1, //SERVICE_GROUP_CCS : "apn,status,nameserver,ipaddress,netmask,gateway"
                                       DBUS_TYPE_STRING, &group2, //SERVICE_GROUP_FOTA : "apn,status,nameserver,ipaddress,netmask,gateway"
                                       DBUS_TYPE_STRING, &group3, //SERVICE_GROUP_USERPAID : "apn,status,nameserver,ipaddress,netmask,gateway"

                                       DBUS_TYPE_STRING, &group4, //SERVICE_GROUP_OEMPAID : "apn,status,nameserver,ipaddress,netmask,gateway"
                                       DBUS_TYPE_STRING, &group5, //SERVICE_GROUP_VCRM : "apn,status,nameserver,ipaddress,netmask,gateway"
                                       DBUS_TYPE_STRING, &group6, //SERVICE_GROUP_CANIDS : "apn,status,nameserver,ipaddress,netmask,gateway"
                                       DBUS_TYPE_INVALID)) {
       m_logger->e("{}) {}:{} : failed to get dbus args", TAG, __FUNCTION__, __LINE__);
       return false;
    }

    const char *dummys[6];
    const char *userpaidInfo = nullptr;

    if (!dbus_message_get_args(reply, nullptr,
                                       DBUS_TYPE_STRING, &dummys[0],
                                       DBUS_TYPE_STRING, &dummys[1],
                                       DBUS_TYPE_STRING, &dummys[2],
                                       DBUS_TYPE_STRING, &userpaidInfo,
                                       DBUS_TYPE_STRING, &dummys[3],
                                       DBUS_TYPE_STRING, &dummys[4],
                                       DBUS_TYPE_STRING, &dummys[5],
                                       DBUS_TYPE_INVALID)) {
       m_logger->e("{}) {}:{} : failed to get dbus args", TAG, __FUNCTION__, __LINE__);
       return false;
    }

    std::vector<std::string> tempArr = split(std::string(userpaidInfo), ',');
    m_logger->i("{}) {}:{} :info [{}] - splitSize[{}]", TAG, __FUNCTION__, __LINE__, userpaidInfo, tempArr.size());
    if (tempArr.size() == 6)
    {
       std::string tempUserpaidDns;
       std::string tempUserpaidApn;
       int tempStatus = 0;
       try {
           tempStatus = stoi(tempArr[1]);
       } catch (std::exception &e) {
           m_logger->e("{}) {}:{} : exception {}", TAG, __FUNCTION__, __LINE__, e.what());
           return false;
       }
       tempUserpaidDns = tempArr[2];
       tempUserpaidApn = tempArr[3];
       m_logger->i("{}) {}:{} :parsed Data : status[{}], ip[{}], dns[{}]", TAG, __FUNCTION__, __LINE__, tempStatus, tempUserpaidApn.c_str(), tempUserpaidDns.c_str());
       dns_addr = tempUserpaidDns;
       src_addr = tempUserpaidApn;
       return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::requestAPNChange(std::string url, uint32_t media_type, bool connect, uint64_t handle) {
    std::vector<std::string> addr;
    std::string host_url = "";
    std::string dns_addr = "";
    std::string src_addr = "";
    std::size_t found = 0;
    bool isDefaultAPN = false;

    getAPNDefaultStatusFromInfoMap(handle, isDefaultAPN);

    if (isDefaultAPN) {
        // CCS APN as default
        if (!getCcsAddress(dns_addr, src_addr)) {
            m_logger->e("{}) {}:{} : failed to get CCS address", TAG, __FUNCTION__, __LINE__);
            return false;
        }

        if (!connect) {
            m_logger->i("{}) {}:{} : reset default apn flag", TAG, __FUNCTION__, __LINE__);
            setAPNDefaultStatusToInfoMap(handle, false);
        }
    } else {
        if ((media_type == static_cast<uint32_t>(PlayerTypes::MediaType::MELON)) ||
                (media_type == static_cast<uint32_t>(PlayerTypes::MediaType::GENIE)) ||
                (media_type == static_cast<uint32_t>(PlayerTypes::MediaType::WELAAA_AUDIO_STREAMING)) ||
                (media_type == static_cast<uint32_t>(PlayerTypes::MediaType::GENESIS_AUDIO_STREAMING)) ||
                (media_type == static_cast<uint32_t>(PlayerTypes::MediaType::PODBBANG)) ||
                (media_type == static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_FUNAUDIO)) ||
                (media_type == static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_MINI_APP_AUDIO)) ||
                (media_type == static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_MINI_APP_VIDEO)) ||
                (media_type == static_cast<uint32_t>(PlayerTypes::MediaType::VIBE))) {
            if (!getUserpaidAddress(dns_addr, src_addr)) {
                m_logger->i("{}) {}:{} : failed to get userpaid address", TAG, __FUNCTION__, __LINE__);
                return false;
            }
        } else if ((media_type == static_cast<uint32_t>(PlayerTypes::MediaType::XIMALAYA)) ||
                (media_type == static_cast<uint32_t>(PlayerTypes::MediaType::QQMUSIC)) ||
                (media_type == static_cast<uint32_t>(PlayerTypes::MediaType::KAOLAFM)) ||
                (media_type == static_cast<uint32_t>(PlayerTypes::MediaType::GOLF_VIDEO))) {
            dns_addr = std::string(FOTA_ADDR_DNS);
            src_addr = std::string(FOTA_ADDR_OUTGOING);
        } else {
            return false;
        }
    }
    if (url.find("http://") != std::string::npos) {
        host_url = url.substr(7);
    } else if(url.find("https://") != std::string::npos) {
        host_url = url.substr(8);
    } else if(url.find("file:///Streaming/") != std::string::npos) {
        m_logger->i("{}) {}:{} : Do not support Streaming Caching, Skip APN", TAG, __FUNCTION__, __LINE__);
        return true;
    /*
     * To - do
     */
    } else {
        m_logger->i("{}) {}:{} : Unsupported url", TAG, __FUNCTION__, __LINE__);
        return false;
    }

    if (media_type == PlayerTypes::MediaType::GOLF_VIDEO) {
        host_url = "vod-progressive.akamaized.net";
    }

    if(host_url.empty()) {
        m_logger->i("{}) {}:{} : host url is empty", TAG, __FUNCTION__, __LINE__);
        return false;
    }

    found = host_url.find_first_of("/");
    if(found != std::string::npos) {
        host_url.erase(host_url.begin()+found, host_url.end());
    }

    m_logger->i("{}) {}:{} : dns_addr[{}], src_addr[{}], host url[{}]", TAG, __FUNCTION__, __LINE__, dns_addr.c_str(), src_addr.c_str(), host_url.c_str());

    std::map<std::string, std::vector<std::string>>::iterator map_iter = currentHostMap.find(host_url);

    if (connect == true) {
        if (getDNSAddress(host_url, dns_addr, addr) == 0) {

            if(map_iter == currentHostMap.end()) {
                for(std::string temp : addr) {
                    setRouteTable(temp, src_addr, connect);
                    currentHostMap[host_url].push_back(temp);
                    currentSrcMap[host_url] = src_addr;
                }
            } else {
                m_logger->i("{}) {}:{} : SetRouting - host url exists. [{}]", TAG, __FUNCTION__, __LINE__, host_url.c_str());
                for(std::string temp : addr) {
                    //std::vector<std::string> addr_list = currentHostMap[host_url]->second;
                    std::vector<string>::iterator iter = std::find(currentHostMap[host_url].begin(), currentHostMap[host_url].end(), temp);

                    if (iter != currentHostMap[host_url].end()) {
                        m_logger->i("{}) {}:{} : SetRouting - host_url[{}] , addr[{}] exists.", TAG, __FUNCTION__, __LINE__, host_url.c_str(), *iter);
                    } else {
                        setRouteTable(temp, src_addr, connect);
                        currentHostMap[host_url].push_back(temp);
                        currentSrcMap[host_url] = src_addr;
                    }
                }
            }
        } else {
            m_logger->i("{}) {}:{} : Failed to get dns address", TAG, __FUNCTION__, __LINE__);
            return false;
        }
    } else {
        if(map_iter != currentHostMap.end()) {
            std::vector<std::string>::iterator iter = map_iter->second.begin();

            for(;iter != map_iter->second.end(); ++iter) {
                m_logger->i("{}) {}:{} : UnsetRouting - host_url[{}] , addr[{}] exists.", TAG, __FUNCTION__, __LINE__, host_url.c_str(), *iter);
                setRouteTable(*iter, src_addr, connect);
            }

            map_iter->second.clear();
            currentHostMap.erase(host_url);
            currentSrcMap.erase(host_url);
        } else{
            m_logger->i("{}) {}:{} : UnsetRouting - host url does not exist. [{}]", TAG, __FUNCTION__, __LINE__, host_url.c_str());
        }
    }
    return true;
}

void PlayerEngineCCOSAdaptor::PlaybackErrorCallback(PlayerTypes::PlaybackError error, PlayerTypes::Track track, uint32_t media_id) {
    uint64_t handle = 0;
    bool reStore = true;
    PlaybackContentError err = ConvertPlaybackErrorToPlaybackContentError(error);

    if (getHandleFromMediaIDMap(media_id, handle) == true) {
        if (error == PlayerTypes::PlaybackError::NO_RESPONSE_ERROR) {
            if (isLastNoResponse) {
                m_logger->e("{}) {}:{} : Block Continous retry.. {})", TAG, __FUNCTION__, __LINE__, media_id);
                //err = PlaybackContentError::PLAYBACK_INTERNAL_ERROR;
            } else {
                m_logger->e("{}) {}:{} : Error player-engine restore logic ++: {})", TAG, __FUNCTION__, __LINE__, media_id);
                reStore = false;
                isLastNoResponse = true;
                (void)stop(handle, true);
                (void)load(handle);
                m_logger->e("{}) {}:{} : Error player-engine restore logic --: {})", TAG, __FUNCTION__, __LINE__, media_id);
            }
        }

        if (reStore)
            PlaybackErrorCallbackInternal(handle, err);
    } else {
        m_logger->e("{}) {}:{} : Handle did not exist in MediaIDMap", TAG, __FUNCTION__, __LINE__);
    }
}

void PlayerEngineCCOSAdaptor::PlaybackStatusChangeCallback(std::vector<PlayerTypes::Playback> status) {
    for(auto itr : status) {
        if(itr.getActive()) {
            uint32_t media_id = itr.getMedia_id();
            uint64_t handle = LLONG_MAX;
            {
                std::unique_lock<std::mutex> l(condLoadLock);
                condLoadWait.wait_for(l, std::chrono::milliseconds(500), [this]{ return isLoadFinished; });
            }

            if (getHandleFromMediaIDMap (media_id, handle) == true) {
                std::lock_guard<std::mutex> lock(PlayerStatusChangeCallbackMutex);
                PlayerState state = ConvertPlaybackStatusToPlayerState(itr);
                if (handle != LLONG_MAX && state != PlayerState::UNINIT) {
                    PlaybackStatusChangeCallbackInternal(state, handle);
                    itr.setActive(false);
                }
            }
        }
    }
}

PlayerState PlayerEngineCCOSAdaptor::ConvertPlaybackStatusToPlayerState(PlayerTypes::Playback status) {
    PlayerState state = PlayerState::MAX;
    PlayerTypes::PlaybackStatus playbackState = status.getStatus();

    switch(playbackState){
        case PlayerTypes::PlaybackStatus::PLAYING:
            state = PlayerState::PLAYING;
            break;
        case PlayerTypes::PlaybackStatus::PAUSED:
            state = PlayerState::PAUSED;
            break;

        case PlayerTypes::PlaybackStatus::STOPPED:
            state = PlayerState::STOPPED;
            break;
        case PlayerTypes::PlaybackStatus::READY:
            state = PlayerState::READY;
            break;
        case PlayerTypes::PlaybackStatus::UNINIT:
            state = PlayerState::UNINIT;
            break;
#if 0
        case PlayerTypes::PlaybackStatus::DONE:
            state = PlayerState::DONE;
            break;
#endif
    }
    return state;
}

void PlayerEngineCCOSAdaptor::PlaybackStatusChangeCallbackInternal(PlayerState status, uint64_t handle) {
    ccos::media::HMediaPlayingState HStatus = ccos::media::HMediaPlayingState::STOPPED;
    PlayerEngineAdaptationInfo info;
    uint32_t media_type = static_cast<uint32_t>(PlayerTypes::MediaType::AUDIO);

    if (getAdaptationInfoFromInfoMap(handle, info) == true) {
        media_type = info.player_type;

        switch(status){
            case PlayerState::PLAYING:
                HStatus = ccos::media::HMediaPlayingState::PLAYING;
                break;
            case PlayerState::PAUSED:
                HStatus = ccos::media::HMediaPlayingState::PAUSED;
                break;
            case PlayerState::STOPPED:
                HStatus = ccos::media::HMediaPlayingState::STOPPED;
                break;
            case PlayerState::READY:
                HStatus = ccos::media::HMediaPlayingState::READY;
                break;
            case PlayerState::EOS:
                HStatus = ccos::media::HMediaPlayingState::EOS;
                break;
            case PlayerState::BOS:
                HStatus = ccos::media::HMediaPlayingState::BOS;
                break;
            case PlayerState::SEEKING:
                HStatus = ccos::media::HMediaPlayingState::SEEKING;
                break;
            case PlayerState::TRICK_PLAYING:
                HStatus = ccos::media::HMediaPlayingState::TRICK_PLAYING;
                break;
            case PlayerState::UNINIT:
                HStatus = ccos::media::HMediaPlayingState::UNINIT;
                break;
            default:
                HStatus = ccos::media::HMediaPlayingState::FAILED_TO_PLAY;
                m_logger->e("{}) {}:{} : Error-wrong status is occurred({})",
                    TAG, __FUNCTION__, __LINE__, static_cast<uint32_t>(status));
                return;
        }

        if (HStatus == ccos::media::HMediaPlayingState::STOPPED) {
            switch(media_type) {
                case static_cast<uint32_t>(PlayerTypes::MediaType::KAOLAFM):
                case static_cast<uint32_t>(PlayerTypes::MediaType::MELON):
                case static_cast<uint32_t>(PlayerTypes::MediaType::QQMUSIC):
                case static_cast<uint32_t>(PlayerTypes::MediaType::GENIE):
                case static_cast<uint32_t>(PlayerTypes::MediaType::WELAAA_AUDIO_STREAMING):
                case static_cast<uint32_t>(PlayerTypes::MediaType::GENESIS_AUDIO_STREAMING):
                case static_cast<uint32_t>(PlayerTypes::MediaType::XIMALAYA):
                case static_cast<uint32_t>(PlayerTypes::MediaType::GOLF_VIDEO):
                case static_cast<uint32_t>(PlayerTypes::MediaType::PODBBANG):
                case static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_FUNAUDIO):
                case static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_MINI_APP_AUDIO):
                case static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_MINI_APP_VIDEO):
                case static_cast<uint32_t>(PlayerTypes::MediaType::VIBE):
                    if(info.isAPNChanged == true) {
                        std::lock_guard<std::mutex> lock(PERequestAPNChangeMutex);
                        requestAPNChange(info.url, media_type, false, handle);
                        setAPNStatusToInfoMap(handle, false);
                    }
                    break;
                default:
                    break;
            }
        }

        setPlayerStatusToInfoMap(handle, HStatus);

        if (PlayerContextStatusCallbackMap.find(handle) != PlayerContextStatusCallbackMap.end()) {
            {
                std::lock_guard<std::mutex> lock(PlayerContextStatusCallbackMap[handle].m);
                if (PlayerContextStatusCallbackMap[handle].Listener != nullptr) {
                    PlayerContextStatusCallbackMap[handle].Listener->onPlayingStateChanged(handle, HStatus);
                    m_logger->i("{}) {}:{} : handle({}), onPlayingStateChanged({})",
                            TAG, __FUNCTION__, __LINE__, handle, getHMediaPlayingStateString(HStatus));
                } else {
                    m_logger->i("{}) {}:{} : handle({}), onPlayingStateChanged failed, Listener is empty({})",
                            TAG, __FUNCTION__, __LINE__, handle, getHMediaPlayingStateString(HStatus));
                }
            }
            if (HStatus == ccos::media::HMediaPlayingState::STOPPED) {
                switch(media_type) {
                    case static_cast<uint32_t>(PlayerTypes::MediaType::STREAM):
                    case static_cast<uint32_t>(PlayerTypes::MediaType::KAKAOI):
                    case static_cast<uint32_t>(PlayerTypes::MediaType::KAKAOI2):
                    case static_cast<uint32_t>(PlayerTypes::MediaType::KAOLAFM):
                    case static_cast<uint32_t>(PlayerTypes::MediaType::QQMUSIC):
                    case static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_FUNAUDIO):
                    case static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_MINI_APP_AUDIO):
                    case static_cast<uint32_t>(PlayerTypes::MediaType::TENCENT_MINI_APP_VIDEO):
                    case static_cast<uint32_t>(PlayerTypes::MediaType::XIMALAYA):
                    case static_cast<uint32_t>(PlayerTypes::MediaType::MANUAL_VIDEO):
                    case static_cast<uint32_t>(PlayerTypes::MediaType::GOLF_VIDEO):
                    case static_cast<uint32_t>(PlayerTypes::MediaType::KIDS_VIDEO):
                    case static_cast<uint32_t>(PlayerTypes::MediaType::RECORDING_PLAY):
                    case static_cast<uint32_t>(PlayerTypes::MediaType::FACE_DETECTION):
                    case static_cast<uint32_t>(PlayerTypes::MediaType::DVRS_FRONT):
                    case static_cast<uint32_t>(PlayerTypes::MediaType::DVRS_REAR):
                        m_logger->i("{}) {}:{} : handle({}), need to close PE({})", TAG, __FUNCTION__, __LINE__, handle);
                        stop(handle, true);
                        break;
                    default:
                        break;
                 }
             }
        } else {
            m_logger->e("{}) {}:{} : handle({}), onPlayingStateChanged({}) can't get mutex",
                    TAG, __FUNCTION__, __LINE__, handle, getHMediaPlayingStateString(HStatus));
        }
    } else {
        m_logger->e("{}) {}:{} : Error-can't find the information", TAG, __FUNCTION__, __LINE__);
    }
}

bool PlayerEngineCCOSAdaptor::toggleSubtitle(uint64_t handle, bool show) {
    PlayerEngineAdaptationInfo info;
    uint32_t media_id = 0;

    if (getAdaptationInfoFromInfoMap(handle, info) == true) {
        if (getMediaIDFromMediaIDMap(handle, media_id) == true) {
            if (isProxyConnectionAvailable() == true){
                std::function<void(const CommonAPI::CallStatus&, const PlayerTypes::PlayerError&)> callback = std::bind(&PlayerEngineCCOSAdaptor::setSubtitleActivateAsyncCallback, this, std::placeholders::_1, std::placeholders::_2);
                this->playerProxy->setSubtitleActivateAsync(show, media_id, callback);
                info.subtitleActivated = show;
                return true;
            }
            else {
                m_logger->e("{}) {}:{} : Error-CommonAPI connection is not avilable", TAG, __FUNCTION__, __LINE__);
            }
        }
    } else {
        m_logger->e("{}) {}:{} : can't find the info related to handle : {}", TAG, __FUNCTION__, __LINE__, handle);
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setSubtitleEventListener(uint64_t handle, std::shared_ptr<ccos::media::IHMediaVideoConfigListener> listener) {
    PlayerEngineAdaptationInfo info;

    if (getAdaptationInfoFromInfoMap(handle, info) == true) {
        if(PlayerContextSubtitleListenerMap.find(handle) != PlayerContextSubtitleListenerMap.end()){
            m_logger->e("{}) {}:{} : Error-wrong handle. subtitle listener has already registered", TAG, __FUNCTION__, __LINE__);
            PlayerContextSubtitleListenerMap.erase(handle);
        }

        {
            std::lock_guard<std::mutex> lock(PlayerContextSubtitleListenerMap[handle].m);
            PlayerContextSubtitleListenerMap[handle].Listener = listener;
            m_logger->i("{}) {}:{} : Subtitle Event Listener is registered successfully({})", TAG, __FUNCTION__, __LINE__, handle);
        }
    } else {
        return false;
    }
    return true;
}

void PlayerEngineCCOSAdaptor::setSubtitleActivateAsyncCallback(const CommonAPI::CallStatus& status, const PlayerTypes::PlayerError& error) {

}

bool PlayerEngineCCOSAdaptor::setMediaType(uint64_t handle, PlayerTypes::MediaType media_type) {
    PlayerTypes::MediaType response;
    PlayerEngineAdaptationInfo info;
    CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::INVALID_VALUE;
    CommonAPI::CallInfo callInfo(1000);

    if (getAdaptationInfoFromInfoMap(handle, info) == true) {
        if (setPlayerTypeToInfoMap(handle, static_cast<uint32_t>(media_type))) {
            playerProxy->getMediaTypeAttribute().setValue(media_type, callStatus, response, &callInfo);
            if (callStatus != CommonAPI::CallStatus::SUCCESS) {
                m_logger->e("{}) {} : setMediaType fail - m_type=[{}]" ,TAG, __FUNCTION__, media_type);
                return false;
            }
            return true;
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setPlayerEventListener(uint64_t handle, std::shared_ptr<ccos::media::IHMediaPlayerListener> listener) {
    PlayerEngineAdaptationInfo info;

    if (getAdaptationInfoFromInfoMap(handle, info) == true) {
        {
            std::lock_guard<std::mutex> lock(PlayerContextStatusCallbackMap[handle].m);
            if(PlayerContextStatusCallbackMap.find(handle) != PlayerContextStatusCallbackMap.end()){
                //listener has already registered in callback map
                // WARNING: Duplicated listener registration for player event
                PlayerContextStatusCallbackMap.erase(handle);
            }

            PlayerContextStatusCallbackMap[handle].Listener = listener;
            m_logger->i("{}) {}:{} : PlayerEventListener is registered successfully({})", TAG, __FUNCTION__, __LINE__, handle);
        }
    } else {
        m_logger->e("{}) {}:{} : can't find the info related to handle : {}", TAG, __FUNCTION__, __LINE__, handle);
    }
    return true;
}

bool PlayerEngineCCOSAdaptor::setTranscoderEventListener(uint64_t handle, const std::shared_ptr<ccos::media::IHMediaTranscoderListener>& listener) {
    if (listener != nullptr) {
        try{
            mTranscoderListener = std::make_shared<ConvertedTransCoderListener>(listener);
            m_logger->i("{}) {}:{} : make new listener... ref_count=[{}]", TAG, __FUNCTION__, __LINE__, listener.use_count());
            setPlayerEventListener(handle, mTranscoderListener);
        } catch (const std::bad_alloc& exception) {
            m_logger->e("{}) {}:{} : failed to create new listener..", TAG, __FUNCTION__, __LINE__);
        }
    } else {
        mTranscoderListener = NULL;
        setPlayerEventListener(handle, nullptr);
    }
    return true;
}

bool PlayerEngineCCOSAdaptor::setVideoResolution(uint64_t handle, int32_t width, int32_t height) {
    PlayerEngineAdaptationInfo info;

    if (setVideoResolutionToInfoMap(handle, width, height)== true) {
        m_logger->i("{}) {}:{} : success to set a resolution. width : {}, height : {}",TAG, __FUNCTION__, __LINE__, width, height);
        return true;
    } else {
        m_logger->e("{}) {}:{} : can't find the info related to handle : {}", TAG, __FUNCTION__, __LINE__, handle);
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::parseJsonWarning(uint64_t handle, std::string s) {
    std::string str(s);
    ptree pt;
    std::istringstream is(str);
    read_json(is, pt);

    auto json_child = pt.get_optional<string>("Warning");

    if(json_child.is_initialized()){
        if(((*json_child).compare("AudioCodecNotSupported")) == 0){
            PlaybackErrorCallbackInternal(handle, PlaybackContentError::WARNING_AUDIO_CODEC_NOT_SUPPORTED);
            return true;
        }
    }
    else {
        m_logger->e("{}) {}:{} : fail to get Warning child in {}", TAG, __FUNCTION__, __LINE__, s);
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::parseJsonSeeking(uint64_t handle, std::string s) {
    std::lock_guard<std::mutex> lock(PlayerStatusChangeCallbackMutex);
    PlaybackStatusChangeCallbackInternal(PlayerState::SEEKING, handle);
    return true;
}

bool PlayerEngineCCOSAdaptor::parseJsonTrickPlaying(uint64_t handle, std::string s) {
    std::lock_guard<std::mutex> lock(PlayerStatusChangeCallbackMutex);
    PlaybackStatusChangeCallbackInternal(PlayerState::TRICK_PLAYING, handle);
    return true;
}

bool PlayerEngineCCOSAdaptor::parseJsonVideoResolution(uint64_t handle, std::string s) {
    std::string str(s);
    ptree pt;
    std::istringstream is(str);
    read_json(is,pt);

    boost::optional<ptree &>json_child = pt.get_child_optional("Resolution");

    if(json_child.is_initialized()){
        auto width_opt = (*json_child).get_optional<int32_t>("width");
        auto height_opt = (*json_child).get_optional<int32_t>("height");

        if(width_opt.is_initialized() && height_opt.is_initialized()){
            setVideoResolution(handle, *width_opt, *height_opt);
            return true;
        }
    }
    else{
        m_logger->e("{}) {}:{} : fail to get Resolution child in {}", TAG, __FUNCTION__, __LINE__, s);
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::parseJsonEOS(uint64_t handle, std::string s) {
    std::lock_guard<std::mutex> lock(PlayerStatusChangeCallbackMutex);
    PlaybackStatusChangeCallbackInternal(PlayerState::EOS,  handle);
    return true;
}

bool PlayerEngineCCOSAdaptor::parseJsonBOS(uint64_t handle , std::string s) {
    std::lock_guard<std::mutex> lock(PlayerStatusChangeCallbackMutex);
    PlaybackStatusChangeCallbackInternal(PlayerState::BOS, handle);
    return true;
}

bool PlayerEngineCCOSAdaptor::parseJsonSubtitleData(uint64_t handle, std::string s) {
    ptree pt;
    std::string str(s);
    std::istringstream is(str);
    read_json(is,pt);

    std::size_t found = 0;
    std::size_t found_in_loop = 0;
    std::string SubtitleDataString;
    uint64_t duration = 0;
    uint64_t position = 0;

    while ((found_in_loop = s.find("\'",found)) != std::string::npos) {
        s.replace(found_in_loop,found_in_loop+1,"\"");
        found = found_in_loop + 1;
    }

    if ((SubtitleDataString = pt.get<std::string>("SubtitleData","")) != "") {
        if(PlayerContextSubtitleListenerMap.find(handle) != PlayerContextSubtitleListenerMap.end()) {
            if ((getDurationFromInfoMap(handle, duration) == true) && (getPositionFromInfoMap(handle, position) == true)) {
                std::lock_guard<std::mutex> lock(PlayerContextSubtitleListenerMap[handle].m);
                if(m_SubtitleData) {
                    PlayerContextSubtitleListenerMap[handle].Listener->onDataUpdated(m_SubtitleData->subtitle_data, position, duration);
                    m_logger->e("{}) {}:{} : Subtitle Callback [{}]", TAG, __FUNCTION__, __LINE__, std::string(m_SubtitleData->subtitle_data));
                }
                return true;
            }
        } else {
           m_logger->e("{}) {}:{} : Error-wrong handle number in AdaptInfoMap", TAG, __FUNCTION__, __LINE__);

        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::parseJsonChannel(uint64_t handle, std::string s) {
    std::string str(s);
    ptree pt;
    std::istringstream is(str);
    read_json(is,pt);

    int32_t audio_mode = 0;
    int32_t new_audio_mode = 0;
    uint16_t audio_src_id = 0;
    ccos::HUInt32 mediaType = 2;

    PlayerEngineAdaptationInfo info;

    boost::optional<ptree &>json_child = pt.get_child_optional("ChannelInfo");

    if (json_child.is_initialized()) {
        auto channel = (*json_child).get_optional<int32_t>("channel");

        if (channel.is_initialized() && isMultiChannel) {
            if (PlayerContextStatusCallbackMap.find(handle) != PlayerContextStatusCallbackMap.end()) {
                if (PlayerContextStatusCallbackMap[handle].Listener != nullptr) {
                    std::lock_guard<std::mutex> lock(PlayerContextStatusCallbackMap[handle].m);
                    if (*channel >= 6) {
                        PlayerContextStatusCallbackMap[handle].Listener->onSpecificMediaPlayed(handle, mediaType);
                        m_logger->i("{}) {}:{} : handle({}), onSpecificMediaPlayed 6ch called", TAG, __FUNCTION__, __LINE__, handle);
                    }
                } else {
                    m_logger->e("{}) {}:{} : Error, Listener is null", TAG, __FUNCTION__, __LINE__);
                }
            } else {
                m_logger->e("{}) {}:{} : can't find handle[{}] in callback map.", TAG, __FUNCTION__, __LINE__, handle);
            }

            if (getAdaptationInfoFromInfoMap(handle, info) == true) {
                getVolumeTypeAndSrcId(info.player_type, &audio_src_id);

                if (audio_src_id != 0) {
                    if (requestGetSetMode(AM_REQ_GETMODE, audio_mode, audio_src_id)) {
                        // Do nothing
                    } else {
                        m_logger->i("{}) {}:{} : Info : GetMode retMode=[{}], consider as default 2ch", TAG, __FUNCTION__, __LINE__, audio_mode);
                        audio_mode = AM_REQ_SETMODE_STEREO;
                    }
                }

                if (*channel > 0) {
                    if (*channel >= 6) {
                        new_audio_mode = AM_REQ_SETMODE_6CH;
                    } else {
                        new_audio_mode = AM_REQ_SETMODE_STEREO;
                    }

                    if ((audio_src_id != 0) && (audio_mode != new_audio_mode)) {
                        if (requestGetSetMode(AM_REQ_SETMODE, new_audio_mode, audio_src_id)) {
                            setAudioChannelToInfoMap(handle, *channel);
                            m_logger->i("{}) {}:{} : Handle[{}] : audio channel is changed to [{}]", TAG, __FUNCTION__, __LINE__, handle, *channel);
                        } else {
                            // Do nothiing
                        }
                    }
                }
                return true;
            }
        }
    } else {
        m_logger->e("{}) {}:{} : fail to get channel in {}", TAG, __FUNCTION__, __LINE__, s);
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::parseJsonAddress(uint64_t handle, std::string s) {
    const std::string host_url = "enterprise.podbbang.com"; //hard-code
    std::string src_addr;
    std::string dns_addr;
    std::string addr = "";
    std::string str(s);
    ptree pt;
    std::istringstream is(str);
    read_json(is,pt);

    GError *gError = NULL;
    GVariant *ret = NULL;
#if 0 //enabled once redirect uri verification is complete
    bool isDefaultAPN = false;
    PlayerEngineAdaptationInfo info;
    uint32_t media_type = 0;

    getAPNDefaultStatusFromInfoMap(handle, isDefaultAPN);

    if (isDefaultAPN) {
        // CCS APN as default
        if (!getCcsAddress(dns_addr, src_addr)) {
            m_logger->e("{}) {}:{} : failed to get CCS address", TAG, __FUNCTION__, __LINE__);
            return false;
        }
    } else {
        if (getAdaptationInfoFromInfoMap(handle, info) == true) {
            media_type = info.player_type;
        } else {
            m_logger->i("{}) {}:{} : failed to get adaption info", TAG, __FUNCTION__, __LINE__);
            return false;
        }
        if (media_type == static_cast<uint32_t>(PlayerTypes::MediaType::PODBBANG)) {
            if (!getUserpaidAddress(dns_addr, src_addr)) {
                m_logger->i("{}) {}:{} : failed to get userpaid address", TAG, __FUNCTION__, __LINE__);
                return false;
            }
        } else {
            m_logger->i("{}) {}:{} : redirect URI is not supported", TAG, __FUNCTION__, __LINE__);
            return false;
        }
    }
#endif
    boost::optional<ptree &>json_child = pt.get_child_optional("AddressInfo");

    if (json_child.is_initialized()) {
        auto address = (*json_child).get_optional<string>("address");

        if (address.is_initialized()) {
            addr = *address;
            m_logger->e("{}) {}:{} : Address Info=[{}]", TAG, __FUNCTION__, __LINE__, addr.c_str());
#if 0 //enabled once redirect uri verification is complete
            PlayerEngineAdaptInfoMap[handle]->isAPNChanged = true;

            std::map<std::string, std::vector<std::string>>::iterator map_iter = currentHostMap.find(host_url);
            if(map_iter == currentHostMap.end()) {
                setRouteTable(addr, src_addr, true);
                currentHostMap[host_url].push_back(addr);
                currentSrcMap[host_url] = src_addr;
            } else {
                m_logger->i("{}) {}:{} : SetRouting - host url exists. [{}]", TAG, __FUNCTION__, __LINE__, host_url.c_str());
                std::vector<string>::iterator iter = std::find(currentHostMap[host_url].begin(), currentHostMap[host_url].end(), addr);
                if (iter != currentHostMap[host_url].end()) {
                    m_logger->i("{}) {}:{} : SetRouting - host_url[{}] , addr[{}] exists.", TAG, __FUNCTION__, __LINE__, host_url.c_str(), *iter);
                } else {
                    setRouteTable(addr, src_addr, true);
                    currentHostMap[host_url].push_back(addr);
                    currentSrcMap[host_url] = src_addr;
                }
            }
#endif
        } else {
            return false;
        }
    }

    return true;
}

bool PlayerEngineCCOSAdaptor::parseJsonContentType(uint64_t handle, std::string s) {
    std::string str(s);
    ptree pt;
    std::istringstream is(str);
    read_json(is,pt);
    auto type = pt.get_optional<std::string>("ContentType");
    ccos::HUInt32 mediaType = 0;
    bool stereoType = false;

    if(type.is_initialized()) {
        m_logger->i("Received ContentType: [{}]", *type);
        mediaType = ((*type).find("E-AC-JOC") != std::string::npos) ? 1 : 0;
        stereoType = ((*type).find("_2ch") != std::string::npos) ? true : false;
    } else {
        m_logger->e("{}) {}:{} : fail to get ContentType in s=[{}]", TAG, __FUNCTION__, __LINE__, s);
        return false;
    }

    if (PlayerContextStatusCallbackMap.find(handle) != PlayerContextStatusCallbackMap.end()) {
        if (PlayerContextStatusCallbackMap[handle].Listener != nullptr) {
            uint16_t audio_src_id = 0;
            int32_t audio_mode = AM_REQ_SETMODE_6CH_ATMOS;
            PlayerEngineAdaptationInfo info;
            if (getAdaptationInfoFromInfoMap(handle, info) == true) {
                getVolumeTypeAndSrcId(info.player_type, &audio_src_id);
            }

            std::lock_guard<std::mutex> lock(PlayerContextStatusCallbackMap[handle].m);
            PlayerContextStatusCallbackMap[handle].Listener->onSpecificMediaPlayed(handle, mediaType);
            m_logger->i("{}) {}:{} : handle({}), onSpecificMediaPlayed called", TAG, __FUNCTION__, __LINE__, handle);

            // SetMode for Atmos AMP
            if (!stereoType) {
                requestGetSetMode(AM_REQ_SETMODE, audio_mode, audio_src_id);
            }
            return true;
        } else {
            m_logger->e("{}) {}:{} : Error, Listener is null", TAG, __FUNCTION__, __LINE__);
        }
    } else {
        m_logger->e("{}) {}:{} : can't find handle[{}] in callback map.", TAG, __FUNCTION__, __LINE__, handle);
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getVideoResolution(uint64_t handle, int32_t& width, int32_t& height) {
    PlayerEngineAdaptationInfo info;

    if (getAdaptationInfoFromInfoMap(handle, info) == true) {
        if(info.playerStatus == ccos::media::HMediaPlayingState::READY ||
            info.playerStatus == ccos::media::HMediaPlayingState::PLAYING ||
            info.playerStatus == ccos::media::HMediaPlayingState::PAUSED){

            if(info.width > 0 && info.height > 0){
                width = info.width;
                height = info.height;

                m_logger->i("{}) {}:{} : success to get a resolution. width : {}, height : {}",TAG, __FUNCTION__, __LINE__, width, height);
                return true;
            }
        }
    } else {
        m_logger->e("{}) {}:{} : can't find the info related to handle : {}", TAG, __FUNCTION__, __LINE__, handle);
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setVideoContrast(uint64_t handle, int32_t contrast) {
    PlayerEngineAdaptationInfo info;
    uint32_t media_id = 0;

    if (getAdaptationInfoFromInfoMap(handle, info)) {
        if(contrast > CONTRAST_CONTROL_MAX_VALUE || contrast < CONTRAST_CONTROL_MIN_VALUE){
            m_logger->e("{}) {}:{} : Error-out of range({})", TAG, __FUNCTION__, __LINE__, contrast);
            return false;
        }

        /*because gap between user setting for contrast and SoC configuration is existed,
         * before sending contrast data to framework data should be converted */
        /* User contrast setting : -5 ~ 5 , SoC contrast change range : 0.1  ~ 2.0*/

        float contrast_scaled = (contrast == 5 ? 2.0 : 0.1 + ((0.2) * (contrast + 5)));

        /*set default contrast value to 1.0*/
        if(fabs(contrast_scaled - 1.1) <= FLT_EPSILON) {
            contrast_scaled = 1.0;
        }

        if (getMediaIDFromMediaIDMap(handle, media_id) == true) {
            if(isProxyConnectionAvailable()){
                std::function<void (const CommonAPI::CallStatus&, uint64_t rhandle, const PlayerTypes::PlayerError&)> callback
                    = std::bind(&PlayerEngineCCOSAdaptor::setVideoContrastCallback,this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
                playerProxy->setVideoContrastAsync(media_id, contrast_scaled);
                return setVideoContrastToInfoMap(handle, contrast);
            }
            else {
                m_logger->e("{}) {}:{} : Command API connection error" ,TAG, __FUNCTION__, __LINE__);
            }
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setVideoSaturation(uint64_t handle, int32_t saturation) {
    PlayerEngineAdaptationInfo info;
    uint32_t media_id = 0;

    if (getAdaptationInfoFromInfoMap(handle, info) == true) {
        if(saturation > SATURATION_CONTROL_MAX_VALUE || saturation < SATURATION_CONTROL_MIN_VALUE){
            m_logger->e("{}) {}:{} : Error-out of range({})", TAG, __FUNCTION__, __LINE__, saturation);
            return false;
        }

        /*because gap between user setting for saturation and SoC configuration is existed,
         * before sending saturation data to framework data should be converted */
        /* User saturation setting : -5 ~ 5 , SoC saturation change range : 0.1  ~ 2.0*/

        float saturation_scaled = (saturation == 5 ? 2.0 : 0.1 + ((0.2) * (saturation + 5)));

        /*set default saturation value to 1.0*/
        if(fabs(saturation_scaled - 1.1) <= FLT_EPSILON) {
            saturation_scaled = 1.0;
        }

        if (getMediaIDFromMediaIDMap(handle, media_id) == true) {
            if(isProxyConnectionAvailable()){
                std::function<void (const CommonAPI::CallStatus&, uint64_t rhandle, const PlayerTypes::PlayerError&)> callback
                    = std::bind(&PlayerEngineCCOSAdaptor::setVideoSaturationCallback,this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
                playerProxy->setVideoSaturationAsync(media_id, saturation_scaled);
                return setVideoSaturationToInfoMap(handle, saturation);
            }
            else {
                m_logger->e("{}) {}:{} : Command API connection error" ,TAG, __FUNCTION__, __LINE__);
            }
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setVideoBrightness(uint64_t handle, int32_t brightness) {
    PlayerEngineAdaptationInfo info;
    uint32_t media_id = 0;

    if (getAdaptationInfoFromInfoMap(handle, info) == true) {

        if(brightness < BRIGHTNESS_CONTROL_MIN_VALUE || brightness > BRIGHTNESS_CONTROL_MAX_VALUE){
            m_logger->e("{}) {}:{} : Error-out of range({})", TAG, __FUNCTION__, __LINE__, brightness);
            return false;
        }

        float brightness_scaled = brightness / 10.0;

        if (getMediaIDFromMediaIDMap(handle, media_id) == true) {
            if(isProxyConnectionAvailable()){
                std::function<void (const CommonAPI::CallStatus&, uint64_t rhandle, const PlayerTypes::PlayerError&)> callback
                    = std::bind(&PlayerEngineCCOSAdaptor::setVideoBrightnessCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
                playerProxy->setVideoBrightnessAsync(media_id, brightness_scaled);
                return setVideoBrightnessToInfoMap(handle, brightness);
            }
        }
    } else {
        m_logger->e("{}) {} : Command API connection error" ,TAG, __FUNCTION__);
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setAVoffset(uint64_t handle, int32_t milliseconds) {
    PlayerEngineAdaptationInfo info;
    uint32_t media_id = 0;

    if (getAdaptationInfoFromInfoMap(handle, info) == true) {
        /*
         * av-offset
         * The synchronisation offset between audio and video in nanoseconds
         * Integer64. Range: -9223372036854775808 - 9223372036854775807 Default: 0
        */

        if (getMediaIDFromMediaIDMap(handle, media_id) == true) {
            if(isProxyConnectionAvailable()){
                std::function<void (const CommonAPI::CallStatus&, uint64_t rhandle, const PlayerTypes::PlayerError&)> callback
                    = std::bind(&PlayerEngineCCOSAdaptor::setAVoffsetCallback ,this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
                playerProxy->setAVoffsetAsync(media_id, milliseconds);
                return true;
            } else {
                m_logger->e("{}) {}:{} : Command API connection error" ,TAG, __FUNCTION__, __LINE__);
            }
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setPlayerTypeToInfoMap(uint64_t handle, const uint32_t type) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->player_type = type;
        return true;
    }

    return false;
}

bool PlayerEngineCCOSAdaptor::setPlayerNameToInfoMap(uint64_t handle, const std::string name) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->player_name = name;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setPlayerStatusToInfoMap(uint64_t handle, const ccos::media::HMediaPlayingState status) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->playerStatus = status;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setDurationToInfoMap(uint64_t handle, const uint64_t duration) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter  = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->duration = duration;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setUrlToInfoMap(uint64_t handle, const std::string url) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->url = url;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setAPNStatusToInfoMap(uint64_t handle, const bool changed) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->isAPNChanged = changed;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setAPNDefaultStatusToInfoMap(uint64_t handle, const bool changed) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->isAPNDefault = changed;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setAudioSlotToInfoMap(uint64_t handle, const int16_t slot) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->audio_slot = slot;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setAudioChannelToInfoMap(uint64_t handle, const uint16_t channel) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->audio_channel = channel;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setPositionToInfoMap(uint64_t handle, const uint64_t pos) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->position = pos;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setVideoResolutionToInfoMap(uint64_t handle, const uint32_t width, uint32_t height) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->width = width;
        iter->second->height = height;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setVideoContrastToInfoMap(uint64_t handle, const int32_t contrast) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->contrast = contrast;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setVideoBrightnessToInfoMap(uint64_t handle, const int32_t brightness) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->brightness = brightness;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setVideoSaturationToInfoMap(uint64_t handle, const int32_t saturation) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->saturation = saturation;
        return true;
    }
    return false;
}


bool PlayerEngineCCOSAdaptor::setSpeedToInfoMap(uint64_t handle, const int32_t rate) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->speed = rate;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setSubtitleActivationToInfoMap(uint64_t handle, const bool activated) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->subtitleActivated = activated;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setSubscriptionActivationToInfoMap(uint64_t handle, const bool subscribed) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->subscriptionActivated = subscribed;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setSubtitleLanguageToInfoMap(uint64_t handle, const std::vector<std::string> language) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->subtitleLanguages.clear();
        iter->second->subtitleLanguages.assign(language.begin(), language.end());
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setSubtitleLanguageToInfoMap(uint64_t handle, const std::string language) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        if (iter->second->subtitleLanguages.size() > 0) {
            auto sub_iter = std::find(iter->second->subtitleLanguages.begin(), iter->second->subtitleLanguages.end(), language);

            if (sub_iter == iter->second->subtitleLanguages.end()) {
                iter->second->subtitleLanguages.push_back(language);
                return true;
            }
        } else {
            iter->second->subtitleLanguages.push_back(language);
            return true;
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setAudioTracksToInfoMap(uint64_t handle, const std::vector<std::string> tracks) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        iter->second->audioLanguages.clear();
        iter->second->audioLanguages.assign(tracks.begin(), tracks.end());
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::setAudioTrackToInfoMap(uint64_t handle, const std::string track) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        if (iter->second->audioLanguages.size() > 0) {
            auto sub_iter = std::find(iter->second->audioLanguages.begin(), iter->second->audioLanguages.end(), track);

            if (sub_iter == iter->second->audioLanguages.end()) {
                iter->second->audioLanguages.push_back(track);
                return true;
            }
        } else {
            iter->second->audioLanguages.push_back(track);
            return true;
        }
    }
    return false;
}

void PlayerEngineCCOSAdaptor::setVideoSaturationCallback(const CommonAPI::CallStatus& status, uint64_t rhandle, const PlayerTypes::PlayerError& error) {

}

void PlayerEngineCCOSAdaptor::setVideoContrastCallback(const CommonAPI::CallStatus& status, uint64_t rhandle, const PlayerTypes::PlayerError& error) {

}

void PlayerEngineCCOSAdaptor::setVideoBrightnessCallback(const CommonAPI::CallStatus& status, uint64_t rhandle, const PlayerTypes::PlayerError& error) {

}

void PlayerEngineCCOSAdaptor::setAVoffsetCallback(const CommonAPI::CallStatus& status, uint64_t rhandle, const PlayerTypes::PlayerError& error) {

}

int32_t PlayerEngineCCOSAdaptor::getDurationAttrIndex(uint32_t media_id, std::vector<PlayerTypes::Duration>& duration_list) {
    std::vector<PlayerTypes::Duration> dur_t;
    CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::INVALID_VALUE;
    CommonAPI::CallInfo callInfo(1000);

    playerProxy->getDurationAttribute().getValue(callStatus, dur_t, &callInfo);

    std::vector<PlayerTypes::Duration>::iterator itr;

    if (callStatus != CommonAPI::CallStatus::SUCCESS) {
        m_logger->e("{}) {} : getDuration Attr fail - m_id=[{}]" ,TAG, __FUNCTION__, media_id);
        return -1;
    }

    duration_list.assign(dur_t.begin(), dur_t.end());
    for(itr = dur_t.begin(); itr != dur_t.end(); itr++) {
        if(itr->getMedia_id() == media_id) {
             int32_t index = (itr - dur_t.begin());
             m_logger->e("{}) {} : getDuration Attr Index=[{}] - m_id=[{}]" ,TAG, __FUNCTION__, index,  media_id);
             return index;
        }
    }
    m_logger->e("{}) {} : Invalid get duration attr media_id=[{}]" ,TAG, __FUNCTION__, media_id);
    return -1;
}

int32_t PlayerEngineCCOSAdaptor::getMuteAttrIndex(uint32_t media_id, std::vector<PlayerTypes::MuteOption>& mute_list) {
    std::vector<PlayerTypes::MuteOption> mute_t;
    CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::INVALID_VALUE;
    CommonAPI::CallInfo callInfo(1000);

    playerProxy->getMuteAttribute().getValue(callStatus, mute_t, &callInfo);

    std::vector<PlayerTypes::MuteOption>::iterator itr;

    if (callStatus != CommonAPI::CallStatus::SUCCESS) {
        m_logger->e("{}) {} : getMute Attr fail - m_id=[{}]" ,TAG, __FUNCTION__, media_id);
        return -1;
    }

    mute_list.assign(mute_t.begin(), mute_t.end());
    for(itr = mute_t.begin(); itr != mute_t.end(); itr++) {
        if(itr->getMedia_id() == media_id) {
             int32_t index = (itr - mute_t.begin());
             m_logger->i("{}) {} : getMute Attr Index=[{}] - m_id=[{}]" ,TAG, __FUNCTION__, index,  media_id);
             return index;
        }
    }
    m_logger->e("{}) {} : Invalid get mute attr media_id=[{}]" ,TAG, __FUNCTION__, media_id);
    return -1;
}

int32_t PlayerEngineCCOSAdaptor::getRateAttrIndex(uint32_t media_id, std::vector<PlayerTypes::Rate>& rate_list) {
    std::vector<PlayerTypes::Rate> rate_t;
    CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::INVALID_VALUE;
    CommonAPI::CallInfo callInfo(1000);

    playerProxy->getRateAttribute().getValue(callStatus, rate_t, &callInfo);
    std::vector<PlayerTypes::Rate>::iterator itr;

    if (callStatus != CommonAPI::CallStatus::SUCCESS) {
        m_logger->e("{}) {} : getRate Attr fail - m_id=[{}]" ,TAG, __FUNCTION__, media_id);
        return -1;
    }

    rate_list.assign(rate_t.begin(), rate_t.end());
    for(itr = rate_t.begin(); itr != rate_t.end(); itr++) {
        if(itr->getMedia_id() == media_id) {
             int32_t index = (itr - rate_t.begin());
             m_logger->i("{}) {} : getRate Attr Index=[{}] - m_id=[{}]" ,TAG, __FUNCTION__, index,  media_id);
             return index;
        }
    }
    m_logger->e("{}) {} : Invalid get rate attr media_id=[{}]" ,TAG, __FUNCTION__, media_id);
    return -1;
}

int32_t PlayerEngineCCOSAdaptor::getSpeedAttrIndex(uint32_t media_id, std::vector<PlayerTypes::Speed>& speed_list) {
    std::vector<PlayerTypes::Speed> speed_t;
    CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::INVALID_VALUE;
    CommonAPI::CallInfo callInfo(1000);

    playerProxy->getSpeedAttribute().getValue(callStatus, speed_t, &callInfo);
    std::vector<PlayerTypes::Speed>::iterator itr;

    if (callStatus != CommonAPI::CallStatus::SUCCESS) {
        m_logger->e("{}) {} : getSpeed Attr fail - m_id=[{}]" ,TAG, __FUNCTION__, media_id);
        return -1;
    }

    speed_list.assign(speed_t.begin(), speed_t.end());
    for(itr = speed_t.begin(); itr != speed_t.end(); itr++) {
        if(itr->getMedia_id() == media_id) {
             int32_t index = (itr - speed_t.begin());
             m_logger->i("{}) {} : getSpeed Attr Index=[{}] - m_id=[{}]" ,TAG, __FUNCTION__, index,  media_id);
             return index;
        }
    }
    m_logger->e("{}) {} : Invalid get speed attr media_id=[{}]" ,TAG, __FUNCTION__, media_id);
    return -1;
}

int32_t PlayerEngineCCOSAdaptor::getPlaybackAttrIndex(uint32_t media_id, std::vector<PlayerTypes::Playback>& playback_list) {
    std::vector<PlayerTypes::Playback> playback_t;
    CommonAPI::CallStatus callStatus = CommonAPI::CallStatus::INVALID_VALUE;
    CommonAPI::CallInfo callInfo(1000);

    playerProxy->getPlaybackAttribute().getValue(callStatus, playback_t, &callInfo);
    std::vector<PlayerTypes::Playback>::iterator itr;

    if (callStatus != CommonAPI::CallStatus::SUCCESS) {
        m_logger->e("{}) {} : getPlayback Attr fail - m_id=[{}]" ,TAG, __FUNCTION__, media_id);
        return -1;
    }

    playback_list.assign(playback_t.begin(), playback_t.end());
    for(itr = playback_t.begin(); itr != playback_t.end(); itr++) {
        if(itr->getMedia_id() == media_id) {
             int32_t index = (itr - playback_t.begin());
             m_logger->i("{}) {} : getPlayback Attr Index=[{}] - m_id=[{}]" ,TAG, __FUNCTION__, index,  media_id);
             return index;
        }
    }
    m_logger->e("{}) {} : Invalid get playback attr media_id=[{}]" ,TAG, __FUNCTION__, media_id);
    return -1;
}

uint64_t PlayerEngineCCOSAdaptor::getAudioDurationByDatabase(std::string path) {
    std::string file_path;
    const std::string filePrefix("file://");

    size_t found = path.find(filePrefix);
    if (found != std::string::npos) {
        file_path = path.substr(found + filePrefix.size());
    } else {
        file_path = path;
    }
    static auto rules =
      std::unordered_map<icu_comparer::language, icu_comparer::rule>{
      // English
      {"en", "[reorder space digit Latn others]"}};

    auto provider = std::make_shared<icu_comparer_provider>(rxcpp::observable<>::just(std::string("en")), rules);
    auto logger = HMediaPlayerLogger::getInstance().get_logger();
    std::shared_ptr<LmsDatabase> db =
        std::make_shared<LmsDatabase>(logger, provider, "/rw_data/service/lightmediascanner/db.sqlite3");

    auto result = db->get_metadata(media::audio(file_path.c_str()));
    auto result_duration = result ? std::stoull(std::get<::media::duration>(*result).get()) : 0;

    return result_duration * 1000000ULL;
}

int32_t PlayerEngineCCOSAdaptor::getPlayNgByDatabase(std::string path) {
    std::string file_path;
    const std::string filePrefix("file://");

    size_t found = path.find(filePrefix);
    if (found != std::string::npos) {
        file_path = path.substr(found + filePrefix.size());
    } else {
        file_path = path;
    }

    auto logger = HMediaPlayerLogger::getInstance().get_logger();

    std::shared_ptr<ThumbnailExtractorDatabase> db =
        std::make_shared<ThumbnailExtractorDatabase>(logger, "/rw_data/service/lightmediascanner/thumbnail_db.sqlite3");

    int32_t result = db->get_playng(media::video(file_path.c_str()));

    return result;
}

int32_t PlayerEngineCCOSAdaptor::getReadCntByDatabase(std::string path) {
    std::string file_path;
    const std::string filePrefix("file://");

    size_t found = path.find(filePrefix);
    if (found != std::string::npos) {
        file_path = path.substr(found + filePrefix.size());
    } else {
        file_path = path;
    }

    auto logger = HMediaPlayerLogger::getInstance().get_logger();

    std::shared_ptr<ThumbnailExtractorDatabase> db =
        std::make_shared<ThumbnailExtractorDatabase>(logger, "/rw_data/service/lightmediascanner/thumbnail_db.sqlite3");

    int32_t result = db->get_read_cnt(media::video(file_path.c_str()));

    return result;
}

std::string PlayerEngineCCOSAdaptor::getPlayerStateString(PlayerState state) {
    auto iter = playerStateString.find(state);
    if (iter == playerStateString.end()) {
        return "UNKWON";
    }

    return playerStateString[state];
}

std::string PlayerEngineCCOSAdaptor::getHMediaPlayingStateString(ccos::media::HMediaPlayingState state) {
    auto iter = hmediaPlayingStateString.find(state);

    if (iter == hmediaPlayingStateString.end()) {
        return "UNKWON";
    }

    return hmediaPlayingStateString[state];
}

bool PlayerEngineCCOSAdaptor::getHandleFromInfoMap(uint64_t handle, uint64_t& value) {
    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);

    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){

        iter = PlayerEngineAdaptInfoMap.begin();
        for (; iter != PlayerEngineAdaptInfoMap.end(); ++iter) {
            uint64_t temp = iter->first;
            m_logger->e("{}) {}:{} : Handle Information : [{}]", TAG, __FUNCTION__, __LINE__, temp);
        }
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        if (iter->second) {
            value = iter->second->handle;
            return true;
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getPlayerTypeFromInfoMap(uint64_t handle, uint32_t& type) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        if (iter->second) {
            type = iter->second->player_type;
            return true;
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getPlayerNameFromInfoMap(uint64_t handle, std::string& name) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        if (iter->second) {
            name = iter->second->player_name;
            return true;
        }
    }
    return false;
}


bool PlayerEngineCCOSAdaptor::getPlayerStatusFromInfoMap(uint64_t handle, ccos::media::HMediaPlayingState& status) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        if (iter->second) {
            status = iter->second->playerStatus;
            return true;
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getDurationFromInfoMap(uint64_t handle, uint64_t& duration) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        if (iter->second) {
            duration = iter->second->duration;
            return true;
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getUrlToInfoMap(uint64_t handle, std::string& url) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        if (iter->second) {
            url = iter->second->url;
            return true;
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getAPNStatusFromInfoMap(uint64_t handle, bool& changed) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        if (iter->second) {
            changed = iter->second->isAPNChanged;
            return true;
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getAPNDefaultStatusFromInfoMap(uint64_t handle, bool& changed) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        if (iter->second) {
            changed = iter->second->isAPNDefault;
            return true;
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getAudioSlotFromInfoMap(uint64_t handle, int16_t& slot) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        slot = iter->second->audio_slot;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getAudioChannelFromInfoMap(uint64_t handle, uint16_t& channel) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        channel = iter->second->audio_channel;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getPositionFromInfoMap(uint64_t handle, uint64_t& pos) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        pos = iter->second->position;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getVideoResolutionFromInfoMap(uint64_t handle, uint32_t& width, uint32_t& height) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        width = iter->second->width;
        height = iter->second->height;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getSpeedFromInfoMap(uint64_t handle, int32_t& rate) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        rate = iter->second->speed;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getSubtitleActivationFromInfoMap(uint64_t handle, bool& activated) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        activated = iter->second->subtitleActivated;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getSubscriptionActivationFromInfoMap(uint64_t handle, bool& subscribed) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        subscribed = iter->second->subscriptionActivated;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getSubtitleLanguageToInfoMap(uint64_t handle, std::vector<std::string>& languages) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        languages.assign(iter->second->subtitleLanguages.begin(), iter->second->subtitleLanguages.end());
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getAudioTrackToInfoMap(uint64_t handle, std::vector<std::string>& tracks) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        tracks.assign(iter->second->audioLanguages.begin(), iter->second->audioLanguages.end());
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getAdaptationInfoFromInfoMap(uint64_t handle, PlayerEngineAdaptationInfo& info) {

    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if(iter == PlayerEngineAdaptInfoMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        info.handle = iter->second->handle;
        info.duration = iter->second->duration;
        info.position = iter->second->position;
        info.player_type = iter->second->player_type;
        info.speed = iter->second->speed;
        info.width = iter->second->width;
        info.height = iter->second->height;
        info.saturation = iter->second->saturation;
        info.contrast = iter->second->contrast;
        info.brightness = iter->second->brightness;
        info.audio_slot = iter->second->audio_slot;
        info.audio_channel = iter->second->audio_channel;
        info.player_name = iter->second->player_name;
        info.url = iter->second->url;
        info.playerStatus = iter->second->playerStatus;
        info.subtitleLanguages.assign(iter->second->subtitleLanguages.begin(), iter->second->subtitleLanguages.end());
        info.audioLanguages.assign(iter->second->audioLanguages.begin(), iter->second->audioLanguages.end());
        info.subtitleActivated = iter->second->subtitleActivated;
        info.subscriptionActivated = iter->second->subscriptionActivated;
        info.isAPNChanged = iter->second->isAPNChanged;
        info.isAPNDefault = iter->second->isAPNDefault;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getMediaIDFromMediaIDMap(uint64_t handle, uint32_t& id) {
    std::lock_guard<std::mutex> lock(PEMediaIDMapMutex);
    auto iter = PlayerHandleToMediaIDMap.find(handle);
    if(iter == PlayerHandleToMediaIDMap.end()){
        m_logger->e("{}) {}:{} : Error-wrong handle number({})", TAG, __FUNCTION__, __LINE__, handle);
    } else {
        id = iter->second;
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::getHandleFromMediaIDMap(uint32_t id, uint64_t& handle) {
    std::lock_guard<std::mutex> lock(PEMediaIDMapMutex);
    auto iter = PlayerHandleToMediaIDMap.begin();
    for(; iter != PlayerHandleToMediaIDMap.end(); ++iter) {
        if (iter->second == id) {
            m_logger->i("{}) {}:{} : find handle=[{}], media_id=[{}]", TAG, __FUNCTION__, __LINE__, iter->first, iter->second);
            handle = iter->first;
            return true;
        }
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::validateFile(const std::string& path) {
    std::string file_path;
    const std::string filePrefix("file://");
    boost::system::error_code error;

    size_t found = path.find(filePrefix);
    if (found != std::string::npos) {
        file_path = path.substr(found + filePrefix.size());
    } else {
        file_path = path;
    }

    if(boost::filesystem::exists(file_path, error)) {

        uint32_t size = boost::filesystem::file_size(file_path, error);

        if(error) {
            m_logger->e("{}) {} : Failed to get the file size, error[{}]" ,TAG, __FUNCTION__, error.message());
            return false;
        } else {
            m_logger->e("{}) {} : file size=[{}]" ,TAG, __FUNCTION__, size);
        }

        return true;
    } else {
        return false;
    }
}

bool PlayerEngineCCOSAdaptor::validateMultiChannel() {

    GVariant *ret = NULL;
    GError *gError = NULL;
    GVariantIter *iter = NULL;
    guchar type = 0;

    if (mConnection != NULL) {
        ret = g_dbus_connection_call_sync(mConnection, ACS_SERVICE_NAME, ACS_OBJECT_PATH, ACS_INTERFACE_NAME, ACS_ASK_AMPTYPE,
                            NULL,
                            G_VARIANT_TYPE("(ay)"),
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            &gError);
        if (gError) {
            m_logger->e("{}) {}:{} : Error getting amp type", TAG, __FUNCTION__, __LINE__);
            g_error_free(gError);
            return false;
        }

        if (ret) {
            g_variant_get(ret, "(ay)", &iter);
            (void)(g_variant_iter_loop(iter, "y", &type));
            m_logger->i("{}) {}:{} : getting the type from ACS, value=[{}]", TAG, __FUNCTION__, __LINE__, type);

            g_variant_iter_free(iter);

            if (type == 1U) {
                m_logger->i("{}) {}:{} : Amp Type : Mobis AMP (Not support 5.1CH)", TAG, __FUNCTION__, __LINE__);
                return false;
            } else if ( (type == 2U) || (type == 3U) || (type == 5U) ) {
                m_logger->i("{}) {}:{} : Amp Type : Harman AMP(support 5.1CH)", TAG, __FUNCTION__, __LINE__);
                return true;
            } else {
                m_logger->i("{}) {}:{} : Amp Type : Invalid AMP", TAG, __FUNCTION__, __LINE__);
                return true;
            }
        } else {
            m_logger->e("{}) {}:{} : Do not get AMP Type from ACS", TAG, __FUNCTION__, __LINE__);
            return false;
        }
    } else {
        return false;
    }
}

bool PlayerEngineCCOSAdaptor::requestGetSetMode(char* mode, int32_t& audio_mode, uint16_t audio_src_id) {
    if (mConnection == NULL || mode == NULL || audio_src_id == 0) {
        m_logger->e("{}) {}:{} : Connection or mode=[{}] srd_id=[{}] fails..", TAG, __FUNCTION__, __LINE__, mode, audio_src_id);
        return false;
    }

    std::string requestedMode(mode);
    bool ret = false;
    GError *gError = NULL;
    GVariant *gRet = NULL;
    int32_t cur_audio_mode = 0;
    int16_t audio_ret = 0;

    if (requestedMode == AM_REQ_GETMODE) {
        gRet = g_dbus_connection_call_sync(mConnection, AM_SERVICE_NAME, AM_OBJECT_PATH, AM_INTERFACE_NAME,
                                           AM_REQ_GETMODE,
                                           g_variant_new ("(q)", audio_src_id),
                                           G_VARIANT_TYPE_TUPLE,
                                           G_DBUS_CALL_FLAGS_NONE,
                                           -1,
                                           NULL,
                                           &gError);
        if (gError) {
            m_logger->e("{}) {}:{} : Error getting mode from audiomanager", TAG, __FUNCTION__, __LINE__);
            g_error_free(gError);
            return ret;
        }

        if (gRet) {
            g_variant_get(gRet, "(i)", &cur_audio_mode);
            audio_mode = cur_audio_mode;

            if (cur_audio_mode > 0) { // cur_audio_mode: 1(2ch TDM), 2(5.1ch), 3(ATMOS with 5.1ch)
                m_logger->i("{}) {}:{} : Info : GetMode mode=[{}]", TAG, __FUNCTION__, __LINE__, cur_audio_mode);
                ret = true;
            } else {
                m_logger->i("{}) {}:{} : Info : GetMode return fail..", TAG, __FUNCTION__, __LINE__);
            }
            g_variant_unref(gRet);
            gRet = NULL;
        } else {
            m_logger->e("{}) {}:{} : failed to send dbus message to audiomanager", TAG, __FUNCTION__, __LINE__);
        }
    } else if (requestedMode == AM_REQ_SETMODE) {
        gRet = g_dbus_connection_call_sync(mConnection, AM_SERVICE_NAME, AM_OBJECT_PATH, AM_INTERFACE_NAME,
                                           AM_REQ_SETMODE,
                                           g_variant_new ("(qi)", audio_src_id, audio_mode),
                                           G_VARIANT_TYPE_TUPLE,
                                           G_DBUS_CALL_FLAGS_NONE,
                                           -1,
                                           NULL,
                                           &gError);
        if (gError) {
            m_logger->e("{}) {}:{} : Error Setmode to audiomanager", TAG, __FUNCTION__, __LINE__);
            g_error_free(gError);
            return ret;
        }

        if (gRet) {
            g_variant_get(gRet, "(n)", &audio_ret);

            if (audio_ret == 0) {
                m_logger->i("{}) {}:{} : Info : SetMode({}) requested well", TAG, __FUNCTION__, __LINE__, audio_mode);
                ret = true;
            } else {
                m_logger->e("{}) {}:{} : Info : SetMode ret=[{}]", TAG, __FUNCTION__, __LINE__, audio_ret);
            }
            g_variant_unref(gRet);
            gRet = NULL;
        } else {
            m_logger->e("{}) {}:{} : failed to send dbus message to audiomanager", TAG, __FUNCTION__, __LINE__);
        }
    }
    return ret;
}

bool PlayerEngineCCOSAdaptor::createAdaptaionInfo(uint32_t type, std::string name, uint64_t handle_, bool force) {
    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle_);
    if (iter != PlayerEngineAdaptInfoMap.end()) {
        if (force == true) {
            std::shared_ptr<PlayerEngineAdaptationInfo> ptr = std::make_shared<PlayerEngineAdaptationInfo>(type, name, handle_);
            iter->second = ptr;
            return true;
        }
    } else {
        PlayerEngineAdaptInfoMap.insert(std::make_pair(handle_ , std::make_shared<PlayerEngineAdaptationInfo>(type, name, handle_)));
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::deleteAdaptationInfo(uint64_t handle) {
    std::lock_guard<std::mutex> lock(PEAdaptInfoMapMutex);
    auto iter = PlayerEngineAdaptInfoMap.find(handle);
    if (iter != PlayerEngineAdaptInfoMap.end()) {
        m_logger->i("{}) {}:{} : remove a AdaptationInfo, handle=[{}]", TAG, __FUNCTION__, __LINE__, handle);
        iter->second.reset();
    }

    PlayerEngineAdaptInfoMap.erase(handle);
    return true;
}

bool PlayerEngineCCOSAdaptor::createHandleToMediaIDMap(uint64_t handle, uint32_t id, bool force) {
    m_logger->i("{}) {}:{} : handle=[{}], media_id=[{}]", TAG, __FUNCTION__, __LINE__, handle, id);
    std::lock_guard<std::mutex> lock(PEMediaIDMapMutex);
    auto iter = PlayerHandleToMediaIDMap.find(handle);
    if (iter != PlayerHandleToMediaIDMap.end()) {
        if (force == true) {
            iter->second = id;
            return true;
        }
    } else {
        PlayerHandleToMediaIDMap.insert(std::pair<uint64_t, uint32_t>(handle , id));
        return true;
    }
    return false;
}

bool PlayerEngineCCOSAdaptor::deleteHandleFromMediaIDMap(uint64_t handle) {
    std::lock_guard<std::mutex> lock(PEMediaIDMapMutex);
    m_logger->i("{}) {}:{} : remove a MediaIDFromMediaIDMap, handle=[{}]", TAG, __FUNCTION__, __LINE__, handle);
    PlayerHandleToMediaIDMap.erase(handle);
    return true;
}
