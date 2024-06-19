#include "HMediaTranscoder.h"
#include <memory>
#include <string>

#include <PlayerEngineCCOSAdaptor.h>
#include <v1/org/genivi/mediamanager/PlayerTypes.hpp>

namespace ccos {
namespace media {

class HMediaTranscoder::Impl {
public:
    Impl();
    ~Impl();

    void setEventListener(const std::shared_ptr<IHMediaTranscoderListener>& l);
    HResult start(const HMediaTranscodeConfig& cfg);
    HResult pause();
    HResult resume();
    HResult stop();
    HResult getState(HMediaTranscoderState& state);
    HResult getStatus(HMediaTranscoderStatus& status);

    std::shared_ptr<Logger> m_logger;

private :
    uint64_t handle;
    std::shared_ptr<IHMediaTranscoderListener> listener;
};

HMediaTranscoder::Impl::Impl() {
    uint32_t converted_type = static_cast<uint32_t>(v1::org::genivi::mediamanager::PlayerTypes::MediaType::STREAM);
    auto Adaptor = PlayerEngineCCOSAdaptor::getInstance();
    handle = Adaptor->obtainPlayerContext(converted_type, "Transcoder");
    m_logger = HMediaPlayerLogger::getInstance().get_logger();
    //m_logger->i("{}) {}:{} : Impl Constructor=[{}]", TAG, __FUNCTION__, __LINE__, reinterpret_cast<uint64_t>(this));
}

HMediaTranscoder::Impl::~Impl() {
    auto Adaptor = PlayerEngineCCOSAdaptor::getInstance();
    Adaptor->setTranscoderEventListener(handle, nullptr);
}

void HMediaTranscoder::Impl::setEventListener(const std::shared_ptr<IHMediaTranscoderListener>& l) {
    listener = l;

    auto Adaptor = PlayerEngineCCOSAdaptor::getInstance();
    Adaptor->setTranscoderEventListener(handle, l);
}

HResult HMediaTranscoder::Impl::start(const HMediaTranscodeConfig& cfg) {
    m_logger->i("{}) {}:{} : Impl transcode start", TAG, __FUNCTION__, __LINE__);
    auto Adaptor = PlayerEngineCCOSAdaptor::getInstance();
    std::string sourceURL = cfg.getSourceURL();
    std::string targetURL = cfg.getTargetURL();
    std::string fileLocation = cfg.getFileLocation();
    std::string platformName = cfg.getPlatformName();
    HMediaCodecType targetCodec = HMediaCodecType::MAX;
    HMediaContainerType targetContainer = HMediaContainerType::MAX;

    if (sourceURL.size() == 0)
        return HResult::ERROR;
    if (targetURL.size() == 0)
        return HResult::ERROR;
    if ((targetCodec= cfg.getTargetCodec()) == HMediaCodecType::MAX)
        return HResult::ERROR;
    if ((targetContainer = cfg.getTargetContainer()) == HMediaContainerType::MAX)
        return HResult::ERROR;

    Adaptor->setURL(handle, sourceURL);
    if (!Adaptor->setTranscodeOutput(targetURL, targetCodec, targetContainer, fileLocation, platformName)) {
        m_logger->e("{}) {}:{} : setTranscodeOutput failed", TAG, __FUNCTION__, __LINE__);
        return HResult::ERROR;
    }
    if (0 != Adaptor->load(handle, false)) {
        m_logger->e("{}) {}:{} : load failed", TAG, __FUNCTION__, __LINE__);
        return HResult::ERROR;
    }

    return HResult::OK;
}

HResult HMediaTranscoder::Impl::pause() {
    auto Adaptor = PlayerEngineCCOSAdaptor::getInstance();
    if (Adaptor->pause(handle))
        return HResult::OK;
    else
        return HResult::ERROR;
}

HResult HMediaTranscoder::Impl::resume() {
    auto Adaptor = PlayerEngineCCOSAdaptor::getInstance();
    if (Adaptor->play(handle))
        return HResult::OK;
    else
        return HResult::ERROR;
}

HResult HMediaTranscoder::Impl::stop() {
    auto Adaptor = PlayerEngineCCOSAdaptor::getInstance();
    if (Adaptor->stop(handle, true))
        return HResult::OK;
    else
        return HResult::ERROR;
}

HResult HMediaTranscoder::Impl::getState(HMediaTranscoderState& state) {
    auto Adaptor = PlayerEngineCCOSAdaptor::getInstance();
    auto pStatus = Adaptor->getPlayingStatus(handle);

    if (pStatus.get() == HMediaPlayingState::UNINIT)
        state = HMediaTranscoderState::NOT_INITIALIZED;
    else if (pStatus.get() == HMediaPlayingState::READY)
        state = HMediaTranscoderState::IDLE;
    else if (pStatus.get() == HMediaPlayingState::PLAYING)
        state = HMediaTranscoderState::RUNNING;
    else if (pStatus.get() == HMediaPlayingState::PAUSED)
        state = HMediaTranscoderState::PAUSED;
    else if (pStatus.get() == HMediaPlayingState::STOPPED)
        state = HMediaTranscoderState::STOPPED;
    else // Error state
        state = HMediaTranscoderState::NOT_INITIALIZED;
    return HResult::OK;
}

HResult HMediaTranscoder::Impl::getStatus(HMediaTranscoderStatus& status) {
    auto Adaptor = PlayerEngineCCOSAdaptor::getInstance();
    auto pStatus = Adaptor->getPlayingStatus(handle);

    if (pStatus.get() == HMediaPlayingState::PAUSED || pStatus.get() == HMediaPlayingState::READY)
        status = HMediaTranscoderStatus::TRANSCODING_COMPLETE;
    else
        status = HMediaTranscoderStatus::ERROR;
    return HResult::OK;
}


HMediaTranscoder::HMediaTranscoder() {
    this->m_pImpl = std::make_unique<Impl>();
}

HMediaTranscoder::~HMediaTranscoder() {
}

void HMediaTranscoder::setEventListener(std::shared_ptr<IHMediaTranscoderListener> l) {
    Impl *pImpl = this->m_pImpl.get();

    if (pImpl != NULL) {
        pImpl->setEventListener(l);
    }
}

HResult HMediaTranscoder::start(const HMediaTranscodeConfig& cfg) {
    Impl *pImpl = this->m_pImpl.get();

    if (pImpl != NULL) {
        return pImpl->start(cfg);
    }
    return HResult::ERROR;
}

HResult HMediaTranscoder::pause() {
    Impl *pImpl = this->m_pImpl.get();

    if (pImpl != NULL) {
        return pImpl->pause();
    }
    return HResult::ERROR;
}

HResult HMediaTranscoder::resume() {
    Impl *pImpl = this->m_pImpl.get();

    if (pImpl != NULL) {
        return pImpl->resume();
    }
    return HResult::ERROR;
}

HResult HMediaTranscoder::stop() {
    Impl *pImpl = this->m_pImpl.get();

    if (pImpl != NULL) {
        return pImpl->stop();
    }
    return HResult::ERROR;
}

HResult HMediaTranscoder::getState(HMediaTranscoderState& state) {
    Impl *pImpl = this->m_pImpl.get();

    if (pImpl != NULL) {
        return pImpl->getState(state);
    }
    return HResult::ERROR;
}

HResult HMediaTranscoder::getStatus(HMediaTranscoderStatus& status) {
    Impl *pImpl = this->m_pImpl.get();

    if (pImpl != NULL) {
        return pImpl->getStatus(status);
    }
    return HResult::ERROR;
}

} // media
} // ccos
