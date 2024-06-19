#include <HMediaPlayerProvider.h>
#include <HMediaPlayer.h>
#include <HMediaPlayerImpl.h>
#include <HMediaTypes.h>
#include <PlayerEngineCCOSAdaptor.h>

#include <memory>
#include <map>
#include <string.h>


#include <unistd.h>
#include <cstring>
#include <memory>
#include <type_traits>
#include <thread>
#include <chrono>

#include <v1/org/genivi/mediamanager/PlayerTypes.hpp>

namespace ccos {
namespace media {

#define MAX_PLAYER_INSTANCE 6

using namespace v1::org::genivi::mediamanager;

static HMediaPlayerProvider* sInstance = nullptr;


class HMediaPlayerProvider::Impl {
    private:
        std::map <HUInt64, std::pair<std::shared_ptr<HMediaPlayer>,HMediaPlayerInfoType*>> handle_info;
        HResult updatePlayerHandleInfoForContextCreation(uint64_t handle);

    public:
        std::shared_ptr<Logger> m_logger;
        unsigned int PlayerInstanceCount;

        Impl();
        ~Impl();
        std::shared_ptr<HMediaPlayer> createMediaPlayerContext(const std::string &name, const HMediaPlayerType &type);
        /*TODO: we should define result type from MediaManager , use the result value to boolean type temporarily*/
        HResult destructMediaPlayerContext(const HUInt64& handle); 
        std::shared_ptr<HMediaPlayer> obtainMediaPlayer(const std::string& name, const HMediaPlayerType& type, const HZoneType& zone, const std::string& clientName);
        HResult releaseMediaPlayer(const std::shared_ptr<HMediaPlayer> player);
        HResult queryPlayer(const HUInt64& handle, HMediaPlayerInfoType& playerInfo);
        void copyData(const std::unique_ptr<Impl>& rhs);

    friend class HMediaPlayer;
    //friend class HMediaPlayerImpl;
};


HMediaPlayerProvider::Impl::Impl()
    :m_logger(HMediaPlayerLogger::getInstance().get_logger()),
    PlayerInstanceCount(0)
{
    m_logger->i("{}) {}", TAG, __FUNCTION__);
}


HMediaPlayerProvider::Impl::~Impl()
{
    m_logger->i("{}) {}", TAG, __FUNCTION__);
}

std::shared_ptr<HMediaPlayer> HMediaPlayerProvider::Impl::createMediaPlayerContext(
        const std::string& name, const HMediaPlayerType& type) {
    uint64_t handle;
    uint32_t converted_type;
    std::map <HUInt64 ,std::pair<std::shared_ptr<HMediaPlayer>, HMediaPlayerInfoType*>>::iterator iter;
    std::shared_ptr<HMediaPlayer> Player;
    v1::org::genivi::mediamanager::PlayerTypes::PlayerError error;

    PlayerEngineCCOSAdaptor* Adaptor = PlayerEngineCCOSAdaptor::getInstance();
    m_logger->i("{}) {} : name({})", TAG, __FUNCTION__, name.c_str());

    switch(type){
        case HMediaPlayerType::AUDIO_LOCAL_USBMUSIC:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::AUDIO);
            break;
        case HMediaPlayerType::VIDEO_LOCAL_USBVIDEO:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::VIDEO);
            break;
        case HMediaPlayerType::AUDIO_LOCAL_NATURE_SOUND:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::NATURE_SOUND);
            break;
        case HMediaPlayerType::AUDIO_STREAMING_KAOLAFM:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::KAOLAFM);
            break;
        case HMediaPlayerType::AUDIO_STREAMING_MELON:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::MELON);
            break;
        case HMediaPlayerType::AUDIO_STREAMING_QQMUSIC:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::QQMUSIC);
            break;
        case HMediaPlayerType::AUDIO_STREAMING_KAKAOI:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::KAKAOI);
            break;
        case HMediaPlayerType::AUDIO_STREAMING_GENIE:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::GENIE);
            break;
        case HMediaPlayerType::AUDIO_STREAMING_XIMALAYA:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::XIMALAYA);
            break;
        case HMediaPlayerType::VIDEO_MOVING_PICTURE_MANUAL:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::MANUAL_VIDEO);
            break;
        case HMediaPlayerType::VIDEO_STREAMING:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::GOLF_VIDEO);
            break;
        case HMediaPlayerType::AUDIO_LOCAL_MOOD_THERAPY:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::MOOD_THERAPY_AUDIO);
            break;
        case HMediaPlayerType::VIDEO_LOCAL_MOOD_THERAPY:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::MOOD_THERAPY_VIDEO);
            break;
        case HMediaPlayerType::VIDEO_KIDS_CARE_MODE:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::KIDS_VIDEO);
            break;
        case HMediaPlayerType::VIDEO_RECORDING_PLAY:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::RECORDING_PLAY);
            break;
        case HMediaPlayerType::AUDIO_STREAMING_PODBBANG:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::PODBBANG);
            break;
        case HMediaPlayerType::VIDEO_DVRS_FRONT:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::DVRS_FRONT);
            break;
        case HMediaPlayerType::VIDEO_DVRS_REAR:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::DVRS_REAR);
            break;
        case HMediaPlayerType::AUDIO_STREAMING_KAKAOI2:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::KAKAOI2);
            break;
        case HMediaPlayerType::AUDIO_FACE_DETECTION:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::FACE_DETECTION);
            break;
        case HMediaPlayerType::AUDIO_STREAMING_VIBE:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::VIBE);
            break;
        case HMediaPlayerType::AUDIO_STREAMING_AIQUTING:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::TENCENT_FUNAUDIO);
            break;
        case HMediaPlayerType::AUDIO_STREAMING_TENCENT_MINI_APP:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::TENCENT_MINI_APP_AUDIO);
            break;
        case HMediaPlayerType::VIDEO_STREAMING_TENCENT_MINI_APP:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::TENCENT_MINI_APP_VIDEO);
            break;
        case HMediaPlayerType::AUDIO_STREAMING_WELAAA:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::WELAAA_AUDIO_STREAMING);
            break;
        case HMediaPlayerType::AUDIO_STREAMING_KAKAOI3:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::KAKAOI3);
            break;
        case HMediaPlayerType::AUDIO_STREAMING_GENESIS_MUSIC:
            converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::GENESIS_AUDIO_STREAMING);
            break;
        default:
            m_logger->i("{}) {} : Unsupported type request : name({})", TAG, __FUNCTION__, name.c_str());
            return nullptr;
    }
    handle = Adaptor->obtainPlayerContext(converted_type, name);

    if (handle == UINT_MAX) {
        m_logger->i("{}) {} : Failed to initialize common api. Returns nullptr", TAG, __FUNCTION__);
        return nullptr;
    }

    if ((iter = handle_info.find(handle)) != handle_info.end()){
        Player = std::get<0>(iter->second);
    } else {
        if(PlayerInstanceCount >= MAX_PLAYER_INSTANCE){
            if(updatePlayerHandleInfoForContextCreation(handle) == HResult::INVALID){
                m_logger->e("{}) {} : There is no created instance. Fail to update instance", TAG, __FUNCTION__);
                Adaptor->releasePlayerContext(handle);
                m_logger->i("{}) {} : releasePlayerContext done.", TAG, __FUNCTION__);
                return nullptr;
            }
        }

        Player = std::make_shared<HMediaPlayer>(static_cast<HUInt64>(handle), name);

        HMediaPlayerInfoType* InfoType = new HMediaPlayerInfoType();
        InfoType->setPlayerName(name);
        InfoType->setPlayerType(type);
    
        auto PlayerInfo = std::pair<std::shared_ptr<HMediaPlayer>,HMediaPlayerInfoType*>(Player,InfoType);
        auto mapData = std::pair<HUInt64,std::pair<std::shared_ptr<HMediaPlayer>,HMediaPlayerInfoType*>>(handle,PlayerInfo);

        handle_info.insert(mapData);

        PlayerInstanceCount = PlayerInstanceCount + 1;
    }

    return Player;
}

HResult HMediaPlayerProvider::Impl::updatePlayerHandleInfoForContextCreation(uint64_t handle)
{
    uint64_t context_index = (handle & (std::numeric_limits<unsigned long>::max() - std::numeric_limits<unsigned int>::max())) >> 32;
    auto iter = handle_info.begin();

    for(; iter != handle_info.end(); iter++){
    uint64_t ctx_idx = (iter->first & (std::numeric_limits<unsigned long>::max() - std::numeric_limits<unsigned int>::max())) >> 32;
        if(context_index == ctx_idx){
            auto newInfo = std::pair<std::shared_ptr<HMediaPlayer>,HMediaPlayerInfoType*>(std::get<0>(iter->second),std::get<1>(iter->second));
            auto newMapData = std::pair<HUInt64,std::pair<std::shared_ptr<HMediaPlayer>, HMediaPlayerInfoType*>>(handle,newInfo);
            handle_info.erase(static_cast<HUInt64>(iter->first));
            handle_info.insert(newMapData);

            break;
        }
    }

    if(iter == handle_info.end()){
      return HResult::INVALID;
    }

    return HResult::OK;
}

HResult HMediaPlayerProvider::Impl::destructMediaPlayerContext(const HUInt64& handle)
{
    std::map <HUInt64 , std::pair<std::shared_ptr<HMediaPlayer>,HMediaPlayerInfoType*>>::iterator iter;
    //HMediaPlayerInfoType *Info;
    v1::org::genivi::mediamanager::PlayerTypes::PlayerError error;
    PlayerEngineCCOSAdaptor *Adaptor = PlayerEngineCCOSAdaptor::getInstance();

    m_logger->i("{}) {} : handle({})", TAG, __FUNCTION__, handle);

    if((iter = handle_info.find(handle)) != handle_info.end()){
        Adaptor->releasePlayerContext(handle);

        handle_info.erase(handle);
        if(PlayerInstanceCount > 0)
            PlayerInstanceCount = PlayerInstanceCount - 1;
        else
            PlayerInstanceCount = 0;

        m_logger->i("{}) {} : handle({}), PlayerInstanceCount({})", TAG, __FUNCTION__, handle, PlayerInstanceCount);
        return HResult::OK;
    }

    return HResult::INVALID;
}

std::shared_ptr<HMediaPlayer> HMediaPlayerProvider::Impl::obtainMediaPlayer(const std::string& name, const HMediaPlayerType& type, const HZoneType& zone, const std::string& clientName)
{
    std::string fullName = name;
    if (!clientName.empty())
        fullName.append(clientName);
    std::shared_ptr<HMediaPlayer> Player = this->createMediaPlayerContext(fullName,type);
    return Player;
}

HResult HMediaPlayerProvider::Impl::releaseMediaPlayer(const std::shared_ptr<HMediaPlayer> player)
{
     return destructMediaPlayerContext(player->getHandle());
}

HResult HMediaPlayerProvider::Impl::queryPlayer(const HUInt64& handle, HMediaPlayerInfoType& playerInfo)
{
    m_logger->i("{}) {} : handle({})", TAG, __FUNCTION__, handle);

    std::map <HUInt64 ,std::pair<std::shared_ptr<HMediaPlayer>, HMediaPlayerInfoType*>>::iterator iter;
    if((iter = handle_info.find(handle)) != handle_info.end()){
        playerInfo.setPlayerName(std::get<1>(iter->second)->getPlayerName());
        playerInfo.setPlayerType(std::get<1>(iter->second)->getPlayerType());

        return HResult::OK;
    }

    return HResult::INVALID;
}

void HMediaPlayerProvider::Impl::copyData(const std::unique_ptr<Impl>& rhs) {
    this->PlayerInstanceCount = rhs->PlayerInstanceCount;
    this->handle_info.insert(rhs->handle_info.begin(), rhs->handle_info.end());
}

HMediaPlayerProvider::HMediaPlayerProvider()
{
    this->m_pImpl = std::make_unique<Impl>();
}

/*
HMediaPlayerProvider::HMediaPlayerProvider(const HMediaPlayerProvider& rhs) {
    this->m_pImpl = std::make_unique<Impl>();
    this->m_pImpl->m_logger = HMediaPlayerLogger::getInstance().get_logger();
    this->m_pImpl->PlayerInstanceCount = rhs.m_pImpl->PlayerInstanceCount;
    this->m_pImpl->copyData(rhs.m_pImpl);
}
HMediaPlayerProvider::HMediaPlayerProvider(HMediaPlayerProvider&& rhs) {
    this->m_pImpl = std::move(rhs.m_pImpl);
}
*/

HMediaPlayerProvider::~HMediaPlayerProvider() {
    this->m_pImpl->m_logger->i("{}) {}", TAG, __FUNCTION__);
    this->m_pImpl.reset();
}

HMediaPlayerProvider& HMediaPlayerProvider::getInstance() {
    if(sInstance == nullptr) {
        sInstance = new HMediaPlayerProvider;
        std::atexit([]{
            if(sInstance != nullptr) {
                delete sInstance;
                sInstance = nullptr;
            }
		});
        sInstance->m_pImpl->m_logger->i("{}) {} : sInstance({})", TAG, __FUNCTION__, reinterpret_cast<uint32_t&>(sInstance));
    }
    return *sInstance;
}


/**
 * @brief call this function to obtain the existing player's handle
 * @param[in] name of the player
 * @param[out] already existing handle
 * @retval HResult::OK if successfully obtained a player handle
 * @retval HResult::INVALID if the argument is not valid
 * @retval HResult::ERROR if internal error occured
 */
std::shared_ptr<HMediaPlayer> HMediaPlayerProvider::obtainMediaPlayer(const std::string& name, const HMediaPlayerType& type, const HZoneType& zone, const std::string& clientName) {
    Impl* ProviderImpl = this->m_pImpl.get();
    std::shared_ptr<HMediaPlayer> Player;

    if(ProviderImpl == nullptr){
        return nullptr;
    }

    ProviderImpl->m_logger->i("{}) {} : name({}), c_name({})", TAG, __FUNCTION__, name.c_str(), clientName.c_str());

    /*request MediaManager to get a handle by passing name, and return it */
    /*I think this function just requests to increase a reference count of a MediaPlayer instance from MediaManager */
    if((Player = ProviderImpl->obtainMediaPlayer(name, type, zone, clientName)) != nullptr){
        ProviderImpl->m_logger->i("{}) {} : Player({})", TAG, __FUNCTION__, reinterpret_cast<uint64_t&>(Player));
        return Player;
    }

    return nullptr;
}

/**
 * @brief this member function is used to release the player
 * @param[in] handle of an existing player
 * @retval HResult::OK if successfully released the handle of the player
 * @retval HResult::INVALID if the argument is not valid
 * @retval HResult::ERROR if internal error occured
 */
HResult HMediaPlayerProvider::releaseMediaPlayer(const std::shared_ptr<HMediaPlayer> player) {
    Impl* ProviderImpl = this->m_pImpl.get();
    /*request MediaManager to decrease a reference count of the Player class instance */
    /*if the reference count become zero then How does SW process it?*/
    if(ProviderImpl == nullptr){
        return HResult::ERROR;
    }

    if(ProviderImpl->releaseMediaPlayer(player) == HResult::OK){
        ProviderImpl->m_logger->i("{}) {} : Player({})", TAG, __FUNCTION__, reinterpret_cast<const uint64_t&>(player));
        return HResult::OK;
    }
    else {
        return HResult::ERROR;
    }

}

/**
 * @brief to get information about the player
 * @details it requests the player information corresponding to the handle
 * @param[in] handle of an existing player
 * @param[out] reference to an instance of HMediaPlayerInfoType
 * @retval HResult::OK if successfully done querying information
 * @retval HResult::INVALID if the argument is not valid
 * @retval HResult::ERROR if internal error occured
 */
HResult HMediaPlayerProvider::queryPlayer(const HUInt64& handle, HMediaPlayerInfoType& playerInfo) {

    Impl* ProviderImpl = this->m_pImpl.get();
    if(ProviderImpl == nullptr){
        return HResult::ERROR;
    }

    if((ProviderImpl->queryPlayer(handle, playerInfo) == HResult::OK)){
        ProviderImpl->m_logger->i("{}) {} : handle({})", TAG, __FUNCTION__, handle);
        return HResult::OK;
    }
    else {
        return HResult::ERROR;
    }
}


} // media
} // ccos
