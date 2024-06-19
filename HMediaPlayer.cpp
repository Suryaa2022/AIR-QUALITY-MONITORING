#include <HMediaPlayer.h>
#include <HMediaPlayerImpl.h>
#include <HMediaSubtitle.h>
#include <HMediaSubtitleImpl.h>

#include <PlayerEngineCCOSAdaptor.h>

#include <cstring>
#include <memory>
#include <type_traits>
#include <thread>
#include <chrono>

#include <unistd.h>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <CommonAPI/CommonAPI.hpp>

using namespace v1::org::genivi::mediamanager;
using namespace std::placeholders;
using namespace boost::property_tree;

namespace ccos {
namespace media {



HResult HMediaPlayer::Impl::subscribeEvents(){

    HUInt64 handle = getHandle();
    PlayerEngineCCOSAdaptor *Adaptor = PlayerEngineCCOSAdaptor::getInstance();

    Adaptor->subscribeEvents(static_cast<uint64_t>(handle));

    return HResult::OK;

}

HResult HMediaPlayer::Impl::unsubscribeEvents(){

    HUInt64 handle = getHandle();
    PlayerEngineCCOSAdaptor *Adaptor = PlayerEngineCCOSAdaptor::getInstance();

    Adaptor->unsubscribeEvents(static_cast<uint64_t>(handle));
    return HResult::OK;

}


HMediaPlayer::Impl::Impl(const HUInt64 &handle)
    : status(HMediaPlayingState::STOPPED),
    PlayerListener(nullptr),
    url(),
    Subtitle(nullptr),
    current_position(0),
    media_duration(0)
{
    /*save the handle to internal structure */
    //this->commonAPIProxyInit();
    this->setHandle(handle);

    HMediaPlayerType media_type = HMediaPlayerType::MAX;
    switch(handle){
        case 0:
            media_type = HMediaPlayerType::AUDIO_LOCAL_USBMUSIC;
            break;
        case 1:
            media_type = HMediaPlayerType::VIDEO_LOCAL_USBVIDEO;
            break;
        case 9:
            media_type = HMediaPlayerType::AUDIO_LOCAL_NATURE_SOUND;
            break;
        case 10:
            media_type = HMediaPlayerType::AUDIO_STREAMING_KAOLAFM;
            break;
        case 11:
            media_type = HMediaPlayerType::AUDIO_STREAMING_MELON;
            break;
        case 12:
            media_type = HMediaPlayerType::AUDIO_STREAMING_QQMUSIC;
            break;
        case 13:
            media_type = HMediaPlayerType::AUDIO_STREAMING_KAKAOI;
            break;
        case 14:
            media_type = HMediaPlayerType::AUDIO_STREAMING_GENIE;
            break;
        case 15:
            media_type = HMediaPlayerType::AUDIO_STREAMING_XIMALAYA;
            break;
        case 16:
            media_type = HMediaPlayerType::VIDEO_MOVING_PICTURE_MANUAL;
            break;
        case 17:
            media_type = HMediaPlayerType::VIDEO_STREAMING;
            break;
        case 18:
            media_type = HMediaPlayerType::AUDIO_LOCAL_MOOD_THERAPY;
            break;
        case 19:
            media_type = HMediaPlayerType::VIDEO_LOCAL_MOOD_THERAPY;
            break;
        case 20:
            media_type = HMediaPlayerType::VIDEO_KIDS_CARE_MODE;
            break;
        case 21:
            media_type = HMediaPlayerType::VIDEO_RECORDING_PLAY;
            break;
        case 22:
            media_type = HMediaPlayerType::AUDIO_STREAMING_PODBBANG;
            break;
        case 23:
            media_type = HMediaPlayerType::VIDEO_DVRS_FRONT;
            break;
        case 24:
            media_type = HMediaPlayerType::VIDEO_DVRS_REAR;
            break;
        case 25:
            media_type = HMediaPlayerType::AUDIO_STREAMING_KAKAOI2;
            break;
        case 26:
            media_type = HMediaPlayerType::AUDIO_FACE_DETECTION;
            break;
        case 27:
            media_type = HMediaPlayerType::AUDIO_STREAMING_VIBE;
            break;
        case 28:
            media_type = HMediaPlayerType::AUDIO_STREAMING_AIQUTING;
            break;
        case 29:
            media_type = HMediaPlayerType::AUDIO_STREAMING_TENCENT_MINI_APP;
            break;
        case 30:
            media_type = HMediaPlayerType::VIDEO_STREAMING_TENCENT_MINI_APP;
            break;
        case 31:
            media_type = HMediaPlayerType::AUDIO_STREAMING_WELAAA;
            break;
        case 32:
            media_type = HMediaPlayerType::AUDIO_STREAMING_KAKAOI3;
            break;
        case 33:
            media_type = HMediaPlayerType::AUDIO_STREAMING_GENESIS_MUSIC;
            break;
        default:
            media_type = HMediaPlayerType::AUDIO_LOCAL_USBMUSIC;
    }
}

HMediaPlayer::Impl::~Impl(){
//    unsubscribeEvents();

//    if(PlayerListener != 0){
//        delete PlayerListener;
//        PlayerListener = 0;
//    }
}

HResult HMediaPlayer::Impl::setPlaybackRate(const HMediaPlaybackDirection &dir, const HFloat &rate)
{
    auto Adaptor = PlayerEngineCCOSAdaptor::getInstance();
    float speed = 1.0f;

    (dir == HMediaPlaybackDirection::BACKWARD) ? speed = -rate : speed = rate;

    if(Adaptor->setPlaybackRate(this->getHandle(),speed)  == false){
        return HResult::ERROR;
    }

    return HResult::OK;
}

HResult HMediaPlayer::Impl::setPositionAsync(HUInt64 position)
{
    HUInt64 pos = position;
    PlayerEngineCCOSAdaptor* Adaptor = PlayerEngineCCOSAdaptor::getInstance();

    if(Adaptor->setPositionAsync(getHandle(), static_cast<uint64_t>(pos))){
        return HResult::OK;
    }

    return HResult::ERROR;
}

HResult HMediaPlayer::Impl::seek(const HInt64& time)
{
    PlayerEngineCCOSAdaptor* Adaptor = PlayerEngineCCOSAdaptor::getInstance();

    if(Adaptor->seek(getHandle(), static_cast<int32_t>(time))){
        return HResult::OK;
    }

    return HResult::ERROR;
}


HResult HMediaPlayer::Impl::getPosition(HUInt64& position)
{
    PlayerEngineCCOSAdaptor* Adaptor = PlayerEngineCCOSAdaptor::getInstance();

    auto position_t = Adaptor->getPosition(getHandle());

    if(position_t){
        position = position_t.get();
        return HResult::OK;
    }

    return HResult::ERROR;
}

HUInt64 HMediaPlayer::Impl::getDuration(){
    PlayerEngineCCOSAdaptor* Adaptor = PlayerEngineCCOSAdaptor::getInstance();

    auto duration_t = Adaptor->getDuration(getHandle());
    uint64_t duration = 0;

    if(duration_t){

        duration = duration_t.get();

        return duration;
    }
    else
       return 0;
}

HMediaPlayingState HMediaPlayer::Impl::getPlayingState()
{

    PlayerEngineCCOSAdaptor* Adaptor = PlayerEngineCCOSAdaptor::getInstance();
    HMediaPlayingState Status = this->status; //HMediaPlayingState::STOPPED;

    auto Status_t = Adaptor->getPlayingStatus(getHandle());

    if(Status_t){
        Status = Status_t.get();
        this->status = Status;

    }
    else {
        //TODO : print error message
        //getting Playing status is failed
        Status = this->status;
    }

    return Status;
}


HUInt64 HMediaPlayer::Impl::getHandle(){
    PlayerEngineCCOSAdaptor *Adaptor = PlayerEngineCCOSAdaptor::getInstance();

    uint64_t handle;

    handle = Adaptor->getHandle(m_handle);

    if(m_handle != handle)
        m_handle = handle;

    return static_cast<HUInt64>(handle);
}

void HMediaPlayer::Impl::reset(){
    /* call PlayStatus change callback function with new play state */
    PlayerEngineCCOSAdaptor *Adaptor = PlayerEngineCCOSAdaptor::getInstance();

    if(Adaptor->stop(getHandle())){

    }else{

    }
}

void HMediaPlayer::Impl::setEventListener(std::shared_ptr<IHMediaPlayerListener> listener)
{
    PlayerEngineCCOSAdaptor *Adaptor = PlayerEngineCCOSAdaptor::getInstance();

    if(Adaptor != nullptr) {
        Adaptor->setPlayerEventListener(getHandle(),listener);
    }
}

HResult HMediaPlayer::Impl::setMediaType(HMediaPlayerType type)
{
    PlayerTypes::MediaType media_type;
    PlayerEngineCCOSAdaptor *Adaptor = PlayerEngineCCOSAdaptor::getInstance();

    switch(type){
        case HMediaPlayerType::AUDIO_LOCAL_USBMUSIC:
            media_type = PlayerTypes::MediaType::AUDIO;
            break;
        case HMediaPlayerType::VIDEO_LOCAL_USBVIDEO:
            media_type = PlayerTypes::MediaType::VIDEO;
            break;
        case HMediaPlayerType::AUDIO_LOCAL_NATURE_SOUND:
            media_type = PlayerTypes::MediaType::NATURE_SOUND;
            break;
        case HMediaPlayerType::AUDIO_STREAMING_KAOLAFM:
            media_type = PlayerTypes::MediaType::KAOLAFM;
            break;
        case HMediaPlayerType::AUDIO_STREAMING_MELON:
            media_type = PlayerTypes::MediaType::MELON;
            break;
        case HMediaPlayerType::AUDIO_STREAMING_QQMUSIC:
            media_type = PlayerTypes::MediaType::QQMUSIC;
            break;
        case HMediaPlayerType::AUDIO_STREAMING_KAKAOI:
            media_type = PlayerTypes::MediaType::KAKAOI;
            break;
        case HMediaPlayerType::AUDIO_STREAMING_GENIE:
            media_type = PlayerTypes::MediaType::GENIE;
            break;
        case HMediaPlayerType::AUDIO_STREAMING_XIMALAYA:
            media_type = PlayerTypes::MediaType::XIMALAYA;
            break;
        case HMediaPlayerType::VIDEO_MOVING_PICTURE_MANUAL:
            media_type = PlayerTypes::MediaType::MANUAL_VIDEO;
            break;
        case HMediaPlayerType::VIDEO_STREAMING:
            media_type = PlayerTypes::MediaType::GOLF_VIDEO;
            break;
        case HMediaPlayerType::AUDIO_LOCAL_MOOD_THERAPY:
            media_type = PlayerTypes::MediaType::MOOD_THERAPY_AUDIO;
            break;
        case HMediaPlayerType::VIDEO_LOCAL_MOOD_THERAPY:
            media_type = PlayerTypes::MediaType::MOOD_THERAPY_VIDEO;
            break;
        case HMediaPlayerType::VIDEO_KIDS_CARE_MODE:
            media_type = PlayerTypes::MediaType::KIDS_VIDEO;
            break;
        case HMediaPlayerType::VIDEO_RECORDING_PLAY:
            media_type = PlayerTypes::MediaType::RECORDING_PLAY;
            break;
        case HMediaPlayerType::AUDIO_STREAMING_PODBBANG:
            media_type = PlayerTypes::MediaType::PODBBANG;
            break;
        case HMediaPlayerType::VIDEO_DVRS_FRONT:
            media_type = PlayerTypes::MediaType::DVRS_FRONT;
            break;
        case HMediaPlayerType::VIDEO_DVRS_REAR:
            media_type = PlayerTypes::MediaType::DVRS_REAR;
            break;
        case HMediaPlayerType::AUDIO_STREAMING_KAKAOI2:
            media_type = PlayerTypes::MediaType::KAKAOI2;
            break;
        case HMediaPlayerType::AUDIO_FACE_DETECTION:
            media_type = PlayerTypes::MediaType::FACE_DETECTION;
            break;
        case HMediaPlayerType::AUDIO_STREAMING_VIBE:
            media_type = PlayerTypes::MediaType::VIBE;
            break;
        case HMediaPlayerType::AUDIO_STREAMING_AIQUTING:
            media_type = PlayerTypes::MediaType::TENCENT_FUNAUDIO;
            break;
        case HMediaPlayerType::AUDIO_STREAMING_TENCENT_MINI_APP:
            media_type = PlayerTypes::MediaType::TENCENT_MINI_APP_AUDIO;
            break;
        case HMediaPlayerType::VIDEO_STREAMING_TENCENT_MINI_APP:
            media_type = PlayerTypes::MediaType::TENCENT_MINI_APP_VIDEO;
            break;
        case HMediaPlayerType::AUDIO_STREAMING_WELAAA:
            media_type = PlayerTypes::MediaType::WELAAA_AUDIO_STREAMING;
            break;
        case HMediaPlayerType::AUDIO_STREAMING_KAKAOI3:
            media_type = PlayerTypes::MediaType::KAKAOI3;
            break;
        case HMediaPlayerType::AUDIO_STREAMING_GENESIS_MUSIC:
            media_type = PlayerTypes::MediaType::GENESIS_AUDIO_STREAMING;
            break;
        default:
            return HResult::INVALID;
    }

    if(Adaptor->setMediaType(getHandle(),media_type)){
        return HResult::OK;
    }

    return HResult::ERROR;
}

HResult HMediaPlayer::Impl::setURL(const std::string& url)
{
    HResult ret = HResult::OK;
    PlayerEngineCCOSAdaptor *Adaptor = PlayerEngineCCOSAdaptor::getInstance();
    //this->url = url;
    if(Adaptor->setURL(getHandle(),url)){

    }else {
        ret = HResult::ERROR;
    }
    return ret;
}

HResult HMediaPlayer::Impl::load(bool needDownmix)
{
    HResult ret = HResult::INVALID;
    PlayerEngineCCOSAdaptor *Adaptor = PlayerEngineCCOSAdaptor::getInstance();
    int32_t result = 0;

    result = Adaptor->load(getHandle(), true /* Audio focus */, needDownmix);

    if(result == -1){
        ret = HResult::ERROR;
    } else if (result == -2) {
        ret = HResult::CONNECTION_FAIL;
    } else {
        ret = HResult::OK;
    }

    return ret;
}

HResult HMediaPlayer::Impl::switchChannel(bool useDownmix)
{
    HResult ret = HResult::OK;
    PlayerEngineCCOSAdaptor *Adaptor = PlayerEngineCCOSAdaptor::getInstance();

    bool result = true;

    result = Adaptor->switchChannel(getHandle(), useDownmix);

    if(!result){
        ret = HResult::ERROR;
    } else {
        ret = HResult::OK;
    }

    return ret;
}

HResult HMediaPlayer::Impl::play()
{
    HResult ret = HResult::OK;
    PlayerEngineCCOSAdaptor *Adaptor = PlayerEngineCCOSAdaptor::getInstance();

    if(Adaptor->play(getHandle())){

    }
    else {
        ret = HResult::ERROR;
    }
    return ret;
}

HResult HMediaPlayer::Impl::pause()
{
    HResult ret = HResult::OK;
    PlayerEngineCCOSAdaptor *Adaptor = PlayerEngineCCOSAdaptor::getInstance();

    if(Adaptor->pause(getHandle())){

    }else {
        ret = HResult::ERROR;
    }


    return ret;
}

HResult HMediaPlayer::Impl::stop()
{
    HResult ret = HResult::OK;
    PlayerEngineCCOSAdaptor *Adaptor = PlayerEngineCCOSAdaptor::getInstance();

    if(Adaptor->stop(getHandle())){

    }else {
        ret = HResult::ERROR;
    }
    return ret;
}

HResult HMediaPlayer::Impl::setAVoffset(const HInt32& milliseconds)
{
    HResult ret = HResult::OK;
    PlayerEngineCCOSAdaptor *Adaptor = PlayerEngineCCOSAdaptor::getInstance();

    if(Adaptor->setAVoffset(getHandle(), milliseconds)){

    }else {
        ret = HResult::ERROR;
    }
    return ret;
}



HMediaPlayer::HMediaPlayer(const HUInt64& handle, const std::string clientName)
{
    /* save a handle allocated by the HMediaPlayerProvider */
    /* initialize the HMediaPlayer class using handle */

    this->m_pImpl = std::make_unique<Impl>(handle);

}

HMediaPlayer::~HMediaPlayer()
{
    this->m_pImpl.reset();
}

HUInt64 HMediaPlayer::getHandle() const
{
    Impl* Player = this->m_pImpl.get();

    /* return the handle */

    if(Player != nullptr){
        return Player->getHandle();
    }

    /*starting value of handle is 10000*/
    /*so we can indicate zero is a problem */
    return 0;
}


void HMediaPlayer::reset()
{
    Impl *Player = this->m_pImpl.get();

    if(Player != nullptr){
        Player->reset();
    }
    else {

    }
}


void HMediaPlayer::setEventListener(std::shared_ptr<IHMediaPlayerListener> listener)
{
    Impl *Player = this->m_pImpl.get();

    if(Player != NULL){
        Player->setEventListener(listener);
    }
}

HResult HMediaPlayer::setURL(const std::string& url)
{
    Impl *Player = this->m_pImpl.get();
    if(Player != NULL){
        return Player->setURL(url);
    }

    return HResult::ERROR;
}

HResult HMediaPlayer::load(bool needDownmix)
{
    Impl *Player = this->m_pImpl.get();

    if(Player != NULL){
        return Player->load(needDownmix);
    }

    return HResult::ERROR;
}

HResult HMediaPlayer::switchChannel(bool useDownmix)
{
    Impl *Player = this->m_pImpl.get();

    if(Player != NULL){
        return Player->switchChannel(useDownmix);
    }

    return HResult::ERROR;
}

HResult HMediaPlayer::play()
{
    Impl *Player = this->m_pImpl.get();

    if(Player != NULL){
        return Player->play();
    }
    return HResult::ERROR;
}

HResult HMediaPlayer::playASync()
{
    try {
        std::thread pThread([&]() {play();});
        pThread.detach();
    } catch (const std::runtime_error& exception) {
        return HResult::ERROR;
    }

    return HResult::OK;
}

HResult HMediaPlayer::pause()
{
    Impl *Player = this->m_pImpl.get();

    if(Player != NULL){
        return Player->pause();
    }

    return HResult::ERROR;

}

HResult HMediaPlayer::pauseASync()
{
    try {
        std::thread pThread([&]() {pause();});
        pThread.detach();

    } catch (const std::runtime_error& exception) {
        return HResult::ERROR;
    }
    return HResult::OK;
}

HResult HMediaPlayer::stop()
{
    Impl *Player = this->m_pImpl.get();

    if(Player != NULL){
        return Player->stop();
    }
    return HResult::ERROR;
}

HResult HMediaPlayer::stopASync()
{
    try {
        std::thread pThread([&]() {stop();});
        pThread.detach();
    } catch (const std::runtime_error& exception) {
        return HResult::ERROR;
    }

    return HResult::OK;
}

HResult HMediaPlayer::getPosition(HUInt64& position) const
{
    Impl *Player = this->m_pImpl.get();

    if(Player != NULL){
        return Player->getPosition(position);

    }
    return HResult::ERROR;
}

HUInt64 HMediaPlayer::getDuration() const
{
    Impl *Player = this->m_pImpl.get();
    HUInt64 duration = 0;

    if(Player != NULL){
        try {
            duration = Player->getDuration();
        } catch (...) {
            duration = 0;
        }
    }

    return duration;
}


HResult HMediaPlayer::setPositionAsync(HUInt64& position)
{
    Impl *Player = this->m_pImpl.get();

    if(Player != NULL){
        return Player->setPositionAsync(position);
    }
    return HResult::ERROR;
}

HResult HMediaPlayer::seek(const HInt64& time)
{
    Impl *Player = this->m_pImpl.get();

    if(Player != NULL){
        return Player->seek(time);
    }
    return HResult::ERROR;
}

HResult HMediaPlayer::setPlaybackRate(const HMediaPlaybackDirection &dir, const HFloat &rate)
{
    Impl *Player = this->m_pImpl.get();

    if(Player != NULL){
        return Player->setPlaybackRate(dir, rate);
    }

    return HResult::ERROR;
}

HMediaPlayingState HMediaPlayer::getPlayingState() const
{
    Impl *Player = this->m_pImpl.get();

    if(Player != NULL){
        return Player->getPlayingState();
    }

    return HMediaPlayingState::FAILED_TO_PLAY;
}

HResult HMediaPlayer::setAVoffset(const HInt32& milliseconds)
{
    Impl *Player = this->m_pImpl.get();

    if(Player != NULL){
        return Player->setAVoffset(milliseconds);
    }

    return HResult::ERROR;
}

} // namespace media
} // namespace ccos
