#ifndef _DAQCONTROLLER_HH_
#define _DAQCONTROLLER_HH_

#include <thread>
#include <atomic>
#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include <mutex>
#include <list>
#include <mongocxx/collection.hpp>

class StraxFormatter;
class MongoLog;
class Options;
class V1725;

class DAQController{
public:
  DAQController(std::shared_ptr<MongoLog>&, std::string hostname="DEFAULT");
  virtual int Arm(std::shared_ptr<Options>&);
  virtual int Start();
  virtual int Stop();

protected:
  std::string fHostname;
  std::shared_ptr<MongoLog> fLog;
  std::shared_ptr<Options> fOptions;
  int fStatus;

private:
  void ReadData(int link);
  void InitLink(std::vector<std::shared_ptr<V1725>>&, std::map<int, std::vector<uint16_t>>&, int&);
  int OpenThreads();
  void CloseThreads();
  //void ReadData(int link);


  //std::vector<std::unique_ptr<StraxFormatter>> fFormatters;
  std::vector<std::thread> fProcessingThreads;
  std::vector<std::thread> fReadoutThreads;
  std::map<int, std::vector<std::shared_ptr<V1725>>> fDigitizers;
  std::mutex fMutex;

  std::atomic_bool fReadLoop;
  std::map<int, std::atomic_bool> fRunning;
  int fNProcessingThreads;

  // For reporting to frontend
  volatile std::atomic_long fDataRate;
  std::atomic_int fPLL;
};

#endif
