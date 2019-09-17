#pragma once
#include <memory>
#include <memory>
#include <unordered_map>
#include "linux-perf-events.h"

typedef LinuxEvents<PERF_TYPE_HARDWARE> EventClass;

class LinuxEventsWrapper {
  public:
    LinuxEventsWrapper(const std::vector<int> event_codes) {
      for(int ecode: event_codes) {
        event_obj.emplace(ecode, std::shared_ptr<EventClass>(new EventClass(ecode)));
        event_res.emplace(ecode, 0);
      }
    }
    void start() {
      for (const auto& [ecode, ptr]: event_obj) {
        ptr->start();  
      }
    }
    void end() {
      for (const auto& [ecode, ptr]: event_obj) {
        event_res[ecode] = ptr->end();  
      }
    }
    // Throws an exception if the code is not present
    unsigned long get_result(int ecode) {
      return event_res.at(ecode);
    }
  private:
    std::unordered_map<int, std::shared_ptr<EventClass>> event_obj;
    std::unordered_map<int, unsigned long> event_res;
};
