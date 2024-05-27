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

#include "player/creator.h"

#include <boost/filesystem.hpp>
#include <fstream>

#include "logger/player_logger.h"
#include "player/pipeline/conf.h"
#include "player/pipeline/common.h"
#include "player/pipeline/video_pipeline.h"
#include "player/pipeline/dvrs_pipeline.h"
#include "player/pipeline/audio_pipeline.h"
#include "player/pipeline/streaming_pipeline.h"
#include "player/pipeline/VisualOn_pipeline.h"
#include "player/pipeline/null_pipeline.h"
#include "player/pipeline/thumbnail_pipeline.h"
#include "player/pipeline/transcode_pipeline.h"
#if defined(PLATFORM_BROXTON)
#include "player/pipeline/broxton/dvd_pipeline.h"
#endif
#if defined(NV_VIDEO_PIPELINE)
#include "player/pipeline/nvidia/nvvideo_pipeline.h"
#endif

namespace genivimedia {

static const std::string kDvdDevice("dvd://");
static const std::string kFilePrefix("file://");
static const std::string kThumbnailPrefix("thumbnail://");
static const std::string kHTTPPrefix("http://");
static const std::string kHTTPSPrefix("https://");

PipelineCreator::PipelineCreator() :
  raw_uri_(),
  error_reason_(ERROR_NONE),
  media_type_(TYPE_UNSUPPORTED) {
}

PipelineCreator::~PipelineCreator() {
}

Pipeline* PipelineCreator::CreatePipeline(int& media_type, const std::string& uri) {
  error_reason_ = ERROR_NONE;
  media_type_ = media_type;

  int ret_media_type = ParsePipelineType(uri);
  media_type = ret_media_type;

  if (ret_media_type == TYPE_AUDIO) {
    return new AudioPipeline();
  } else if (ret_media_type == TYPE_STREAMING) {
    return new StreamingPipeline();
  } else if (ret_media_type == TYPE_3RD_AUDIO) {
    LOG_INFO("Going to create a pipeline for VisualOn pipeline");
    return new VisualOnPipeline();
  } else if (ret_media_type == TYPE_VIDEO) {
    return new VideoPipeline();
  } else if (ret_media_type == TYPE_THUMBNAIL) {
    return new ThumbnailPipeline();
  } else if (ret_media_type == TYPE_UNSUPPORTED) {
    return new NullPipeline();
#ifdef PLATFORM_BROXTON
  } else if (ret_media_type == TYPE_DVD) {
    return new DvdPipeline();
#endif
  } else if (ret_media_type == TYPE_TRANSCODE) {
    LOG_INFO("TranscodePipeline");
    return new TranscodePipeline();
  } else if (ret_media_type == TYPE_DVRS) {
    return new DVRSPipeline();
  } else {
    return new NullPipeline();
  }
}

int PipelineCreator::GetErrorReason() {
  return error_reason_;
}

int PipelineCreator::ParsePipelineType(const std::string& uri) {
  int media_type = ParseMediaTypeFromExtension(uri);
  LOG_INFO("return media type is [%d]", media_type);
  switch (media_type) {
    case TYPE_AUDIO:
    case TYPE_3RD_AUDIO:
      if (!Conf::GetFeatures(SUPPORT_AUDIO)) {
        error_reason_ = ERROR_FILE_NOT_SUPPORTED;
        media_type = TYPE_UNSUPPORTED;
      }
      break;
    case TYPE_VIDEO:
      if (!Conf::GetFeatures(SUPPORT_VIDEO)) {
        error_reason_ = ERROR_FILE_NOT_SUPPORTED;
        media_type = TYPE_UNSUPPORTED;
      }
      break;
    case TYPE_THUMBNAIL:
      if (!Conf::GetFeatures(SUPPORT_THUMB)) {
        error_reason_ = ERROR_FILE_NOT_SUPPORTED;
        media_type = TYPE_UNSUPPORTED;
      }
      break;
    case TYPE_DVD:
      if (!Conf::GetFeatures(SUPPORT_DECK)) {
        error_reason_ = ERROR_FILE_NOT_SUPPORTED;
        media_type = TYPE_UNSUPPORTED;
      }
      break;
    case TYPE_TRANSCODE:
      // ToDo: Add Feature SUPPORT_TRANSCODE
      break;
    default:
      error_reason_ = ERROR_FILE_NOT_SUPPORTED;
      break;
  }
  LOG_INFO("media_type[%d]", media_type);
  return media_type;
}

int PipelineCreator::ParseMediaTypeFromExtension(const std::string& uri) {

  std::size_t found = uri.find(kDvdDevice);
  std::size_t http_found= std::string::npos;
  std::size_t https_found= std::string::npos;
  if (found != std::string::npos)
    return TYPE_DVD;

  if (!CheckFileExist(uri)) {
    error_reason_ = ERROR_FILE_NOT_FOUND;
    return TYPE_UNSUPPORTED;
  }

  boost::filesystem::path check_uri{uri};
  std::string src_ext = check_uri.extension().string();
  std::string ext;
  ext.resize(src_ext.size());
  std::transform(src_ext.begin(), src_ext.end(), ext.begin(), ::tolower);
  LOG_INFO("given uri[%s], file extension[%s] , media_type_[%d]", uri.c_str(), ext.c_str(), media_type_);

  http_found = uri.find(kHTTPPrefix);
  https_found = uri.find(kHTTPSPrefix);

  int ret_media_type = TYPE_UNSUPPORTED;
  if (media_type_ == TYPE_VIDEO) {
    std::vector<std::string> video = Conf::GetSupportedFormat(VIDEO_FILE_FORMAT);
    auto iter = std::find(video.begin(), video.end(), ext);
    if (iter != video.end()) {
      LOG_INFO("Type local video");
      ret_media_type = TYPE_VIDEO;
      found = uri.find(kThumbnailPrefix);
      if (found != std::string::npos)
        ret_media_type = TYPE_THUMBNAIL;
    } else if (ext.compare(".avimanual") == 0) {
      LOG_INFO("Type manual video");
      ret_media_type = TYPE_VIDEO;
    }

    if (http_found != std::string::npos || https_found != std::string::npos) {
      LOG_INFO("Type http(s) video");
      ret_media_type = TYPE_VIDEO;
    }
  } else if (media_type_ == TYPE_AUDIO || media_type_ == TYPE_3RD_AUDIO || media_type_ == TYPE_STREAMING) {
    std::vector<std::string> audio = Conf::GetSupportedFormat(AUDIO_FILE_FORMAT);
    auto itera = std::find(audio.begin(), audio.end(), ext);

    if (itera != audio.end()) {
      LOG_INFO("Type local audio");
      ret_media_type = TYPE_AUDIO;
    }

    if (http_found != std::string::npos || https_found != std::string::npos) {
      LOG_INFO("Type http(s) audio");
      ret_media_type = media_type_;
    } else if (media_type_ == TYPE_3RD_AUDIO) {
      LOG_INFO("Type cached file");
      ret_media_type = media_type_;
    } else if (media_type_ == TYPE_STREAMING) {
      LOG_INFO("Type golf video cached file");
      ret_media_type = media_type_;
    }
  } else if (media_type_ == TYPE_TRANSCODE) {
    LOG_INFO("Type transcoding audio");
    ret_media_type = media_type_;
  } else if (media_type_ == TYPE_DVRS) {
    LOG_INFO("Type dvrs media");
    ret_media_type = media_type_;
  }

  if(TYPE_UNSUPPORTED == ret_media_type)
    LOG_ERROR("unsupported file format");
  return ret_media_type;
}

bool PipelineCreator::CheckFileExist(const std::string& uri) {

  bool found_string = false;
  bool found_http_string = false;

  std::size_t found = uri.find(kFilePrefix);
  std::size_t http_found = uri.find(kHTTPPrefix);
  std::size_t https_found = uri.find(kHTTPSPrefix);

  if (found != std::string::npos) {
    raw_uri_ = uri.substr(found+kFilePrefix.size());
    found_string = true;
  } else if (http_found != std::string::npos || https_found != std::string::npos) {
    found_http_string = true;
  } else {
    found = uri.find(kThumbnailPrefix);
    if (found != std::string::npos) {
      raw_uri_ = uri.substr(found+kThumbnailPrefix.size());
      found_string = true;
    }
  }

  if (found_string) {
    std::fstream fs;
    fs.open (raw_uri_.c_str(), std::fstream::in);
    if (!fs.is_open()) {
      LOG_ERROR("failed to open file[%s]", raw_uri_.c_str());
      return false;
    }
    fs.close();
  } else if (found_http_string) {
     return true;
  } else {
    return false;
  }
  return true;
}



}  // namespace genivimedia

