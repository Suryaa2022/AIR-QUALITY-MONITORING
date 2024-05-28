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

#include "player/pipeline/seek_control.h"

#include <cstdlib>
#include <stdlib.h>
#include "logger/player_logger.h"

namespace genivimedia {

static const std::string kSeekControlStartMsg("StartSeekControl");
static const std::string kSeekControlDoneMsg("DoneSeekControl");
static const std::string kSeekControlExitMsg("ExitSeekControl");

SeekControl::SeekControl(int interval)
  : interval_(interval),
    callback_(),
    queue_(g_async_queue_new()),
    thread_(&SeekControl::Loop, this) {
  LOG_INFO("");
}

SeekControl::~SeekControl() {
  Exit();
  g_async_queue_unref(queue_);
  LOG_INFO("");
}

void SeekControl::RegisterCallback(SeekControlCallback callback) {
  callback_ = callback;
}

bool SeekControl::Start() {
  LOG_INFO("");
  try{
    g_async_queue_push(queue_, (gpointer)kSeekControlStartMsg.c_str());
  }catch(...) {
    LOG_INFO("Exception occur");
  }
  return true;
}

void SeekControl::Done() {
  LOG_INFO("");
  try{
    g_async_queue_push(queue_, (gpointer)kSeekControlDoneMsg.c_str());
  }catch(...) {
    LOG_INFO("Exception occur");
  }
}

void SeekControl::Exit() {
  LOG_INFO("");
  try{
    g_async_queue_push(queue_, (gpointer)kSeekControlExitMsg.c_str());
  }catch(...) {
    LOG_INFO("Exception occur");
  }
  thread_.join();
}

void SeekControl::Loop(SeekControl* instance) {
  LOG_INFO("");
  bool message_received = false;
  char* msg;

  do {
    msg = (char *)g_async_queue_timeout_pop(instance->queue_, instance->interval_);
    if (msg) {
      if(kSeekControlStartMsg.compare(msg) == 0) {
        LOG_INFO("receive seek start message");
        message_received = true;
      } else if (kSeekControlExitMsg.compare(msg) == 0) {
        LOG_INFO("receive exit message");
        return;
      } else {
        LOG_INFO("receive seek done message");
        message_received = false;
      }
    } else {
      if (message_received) {
        LOG_INFO("timeout");
        instance->callback_(0);
        message_received = false;
      }
    }
  } while (true);
}

}// namespace genivimedia

