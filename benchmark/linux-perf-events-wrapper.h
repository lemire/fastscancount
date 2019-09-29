#pragma once
#include <memory>
#include <memory>
#include <unordered_map>
#ifdef __linux__
#include "linux-perf-events.h"
#endif

#ifdef __linux__
typedef LinuxEvents<PERF_TYPE_HARDWARE> EventClass;
#endif

class LinuxEventsWrapper {
  public:
    LinuxEventsWrapper(const std::vector<int> event_codes) {
#ifdef __linux__
      for(int ecode: event_codes) {
        event_obj.emplace(ecode, std::shared_ptr<EventClass>(new EventClass(ecode)));
        event_res.emplace(ecode, 0);
      }
#endif
    }
    void start() {
#ifdef __linux__
      for (const auto& [ecode, ptr]: event_obj) {
        ptr->start();  
      }
#endif
    }
    void end() {
#ifdef __linux__
      for (const auto& [ecode, ptr]: event_obj) {
        event_res[ecode] = ptr->end();  
      }
#endif
    }
    // Throws an exception if the code is not present
    unsigned long get_result(int ecode) {
#ifdef __linux__
      return event_res.at(ecode);
#else
      return 0;
#endif
    }
  private:
#ifdef __linux__
    std::unordered_map<int, std::shared_ptr<EventClass>> event_obj;
    std::unordered_map<int, unsigned long> event_res;
#endif
};
