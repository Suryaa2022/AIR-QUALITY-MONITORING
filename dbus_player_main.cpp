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

#include <clocale>
#include <execinfo.h>
#include <fstream>
#include <signal.h>
#include <thread>
#include <sstream>
#include <sys/prctl.h>
#include "logger/player_logger.h"

static genivimedia::DBusPlayerService* service = nullptr;

/**
* @fn void LogMemoryMap()
* @brief Displays memory map.
* @section function_flow Function flow
* - Opens maps file of current process.
* - Gets a contents of file and leaves it as logs.
*
* @section global_variable_none Global Variables : None
* @section dependencies_none Dependencies : None
* @return None
*/
static void LogMemoryMap() {
  char buff[1024];
  FILE *fp;

  std::stringstream ssFilePath;
  ssFilePath <<"/proc/"<<getpid()<< "/maps";
  std::string filepath = ssFilePath.str();

  fp = fopen(filepath.c_str(), "r");
  if (fp == NULL) {
    MMLogError("fopen() error");
    return;
  }

  while(fgets(buff, 1024, fp))
    MMLogError("%s", buff);

  fclose(fp);
}

/**
* @fn void LogBacktrace()
* @brief Displays backtrace.
* @section function_flow Function flow
* - Stores a backtrace in the array pointed to by buffer.
* - Translates the addresses into an array of strings.
* - If an array of strings is null, return.
* - Otherwise, prints an array of strings.
* - Releases an array of strings.
*
* @section global_variable_none Global Variables : None
* @section dependencies_none Dependencies : None
* @return None
*/
static void LogBacktrace() {
  void *array[50];
  size_t size;
  char **strings;
  size_t i;

  size = backtrace(array, 50);
  strings = backtrace_symbols(array, size);
  if (strings == NULL) {
    MMLogError("backtrace symbol error");
    return;
  }

  for(i = 0; i < size; i++) {
    MMLogError("%s", strings[i]);
  }

  free(strings);
}

/**
* @fn void SigHandler(int sig)
* @brief Handler for signal.
* @section function_flow Function flow
* - Calls LogBacktrace() and LogMemoryMap().
* - Terminates the process normally with EXIT_FAILURE code.
*
* @param[in] sig : A signal number.
* @section global_variable_none Global Variables : None
* @section dependencies_none Dependencies : None
* @return None
*/
static void SigHandler(int sig) {
  MMLogError("Caught Signal : %d", sig);

  LogBacktrace();
  LogMemoryMap();

  if(service != nullptr){
      service->Exit();
  }

  std::exit(EXIT_FAILURE);
}

static void SigTermHandler(int sig){
  MMLogError("Caught SigTermHandler : %d", sig);
  if(service != nullptr){
    service->Exit();
  }
  std::exit(EXIT_SUCCESS);
}

/**
* @fn void RegisterSignalHandler()
* @brief Connects signals and handler.
* @section function_flow Function flow
* - Connects handler and SIGABRT, SIGSEGV, SIGPIPE, SIGBUS and SIGTERM signals.
*
* @section global_variable_none Global Variables : None
* @section dependencies_none Dependencies : None
* @return None
*/
static void RegisterSignalHandler() {
  struct sigaction sa;
  sa.sa_handler = SigTermHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_restorer = NULL;
  sa.sa_flags = 0;
  sigaction(SIGTERM, &sa, NULL);

//  sigaction(SIGABRT, &sa, NULL);
//  sigaction(SIGSEGV, &sa, NULL);
//  sigaction(SIGPIPE, &sa, NULL);
//  sigaction(SIGBUS, &sa, NULL);

  signal(SIGABRT, SigHandler);
  signal(SIGSEGV, SigHandler);
  signal(SIGPIPE, SigHandler);
  signal(SIGBUS,  SigHandler);
  signal(SIGTERM, SigHandler);
}

/**
* @fn void ShowVerionInfo()
* @brief Displays git version information.
* @section function_flow_none Function flow : None
* @section global_variable_none Global Variables : None
* @section dependencies_none Dependencies : None
* @return None
*/
static void ShowVerionInfo() {
  MMLog::MMLogInfo("==============================================");
  MMLog::MMLogInfo("PlayerEngine[Multi] S/W Info : Leopard v%s", LEOPARD_VERSION);
  MMLog::MMLogInfo("Reference Version : %s", GIT_VERSION);
  MMLog::MMLogInfo("Commit Date : %s", GIT_DATE);
  MMLog::MMLogInfo("Build Date  : %s %s", __TIME__, __DATE__);
  MMLog::MMLogInfo("==============================================");
}

/**
* @fn : int main(int argc, char **argv)
* @brief Displays version information.
* @section function_flow Function flow
* - Registers signal handler.
* - Sets locale to current locale.
* - Registers PR_SET_PDEATHSIG to die when parent process dies.
* - Registers log instance to output logs by creating DltLogger instance.
* - Registers log FileLogger instance if PLAYER_ENGINE_LOG_PATH is set.
* - Prints information of configuration and version information.
* - Creates DBusPlayerService instance to commmunicate with media manager process via Dbus interface.
* - Creates a new GMainLoop and runs a main loop.
* - Destroys DBusPlayerService if a main loop exits.
* - Unregisters logs.
*
* @param[in] argc : A count of the command line arguments.
* @param[in] argv : An array of the command line arguments.
* @section global_variable_none Global Variables : None
* @section dependencies_none Dependencies : None
* @return int
*/

int main(int argc, char **argv) {
  RegisterSignalHandler();
  std::setlocale(LC_ALL, "");

  prctl(PR_SET_PDEATHSIG, SIGHUP);

  char* log_path = getenv("PLAYER_ENGINE_LOG_PATH");
  if (log_path)
    MMLog::RegisterLogger(new FileLogger(log_path));
  MMLog::RegisterLogger(new DltLogger("PESVC", "player engine service", "PESVC-CTX", "player engine service context"));

  ShowVerionInfo();

  genivimedia::DBusPlayerService* service = new genivimedia::DBusPlayerService();
  service->Run();
  delete service;

  MMLog::UnregisterLogger();
  return 0;
}

