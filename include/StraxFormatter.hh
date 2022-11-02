#ifndef _STRAXINSERTER_HH_
#define _STRAXINSERTER_HH_

#include <cstdlib>
#include <cstdint>
#include <string>
#include <map>
#include <mutex>
#include <experimental/filesystem>
#include <numeric>
#include <atomic>
#include <vector>
#include <thread>
#include <condition_variable>
#include <list>
#include <memory>
#include <string_view>
#include <chrono>
#include <functional>






struct data_packet{
  data_packet() : clock_counter(0), header_time(0) {}
  data_packet(std::u32string s, uint32_t ht, long cc) :
      buff(std::move(s)), clock_counter(cc), header_time(ht) {}
  data_packet(const data_packet& rhs)=delete;
  data_packet(data_packet&& rhs) : buff(std::move(rhs.buff)),
      clock_counter(rhs.clock_counter), header_time(rhs.header_time), digi(rhs.digi) {}
  ~data_packet() {buff.clear(); digi.reset();}

  data_packet& operator=(const data_packet& rhs)=delete;
  data_packet& operator=(data_packet&& rhs) {
    buff=std::move(rhs.buff);
    clock_counter=rhs.clock_counter;
    header_time=rhs.header_time;
    digi=rhs.digi;
    return *this;
  }

  std::u32string buff;
  long clock_counter;
  uint32_t header_time;
  std::shared_ptr<V1725> digi;
};




#endif