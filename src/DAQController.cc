#include "../include/DAQController.hh"
#include "../include/V1725.hh"
#include "../include/DAXHelpers.hh"
#include "../include/Options.hh"
#include "../include/StraxFormatter.hh"
#include "../include/MongoLog.hh"
#include <algorithm>
#include <bitset>
#include <chrono>
#include <cmath>
#include <numeric>
#include <iostream>
#include <fstream>

#include <bsoncxx/builder/stream/document.hpp>

DAQController::DAQController(std::shared_ptr<MongoLog>& log, std::string hostname){
  fLog=log;
  fOptions = nullptr;
  fStatus = DAXHelpers::Idle;
  fReadLoop = false;
  fNProcessingThreads=8;
  fDataRate=0.;
  fHostname = hostname;
  fPLL = 0;
}



  int DAQController::Arm(std::shared_ptr<Options>& options)
  {
    std::cout << "DAQController::Arm" << std::endl ; 
    // The code here is to write the register into the flashADC 
    fOptions= options ; 
    fNProcessingThreads = fOptions->GetInt("processing_threads");
    fLog->Entry(MongoLog::Local, "Beginning electronics initialization with %i threads",fNProcessingThreads);

    fPLL = 0;
    fStatus = DAXHelpers::Arming;
    int num_boards = 0;

    for (auto& d : fOptions->GetBoards("V17XX"))
    {
      std::shared_ptr<V1725> digi;
      fLog->Entry(MongoLog::Local, "Arming new digitizer %i", d.board);



      try{
      if (d.type == "V1725")
      {digi = std::make_shared<V1725>(fLog, fOptions, d.board, d.vme_address);}
      else if( d.type== "V1730")
      {digi = std::make_shared<V1725>(fLog, fOptions, d.board, d.vme_address);}
      else 
      {
        digi = std::make_shared<V1725>(fLog, fOptions, d.board, d.vme_address);
        std::cout << "Sorry , for the time being , this board is not supported" <<  std::endl ;
      }

      // The Init function is to CAEN_DGTZ_OPENDITIZER 
      if (digi->Init(d.link,d.crate)!=0)
      {
        std::cout << "Failed to Init" << std::endl ; 
        throw std::runtime_error("Board init failed");
      }
      fDigitizers[d.link].emplace_back(digi);
      num_boards++;
      }
      catch(const std::exception& e) {
      fLog->Entry(MongoLog::Warning, "Failed to initialize digitizer %i: %s", d.board,
          e.what());
      fDigitizers.clear();
      return -1;
      }
    }

    fLog->Entry(MongoLog::Local, "This host has %i boards", num_boards);
    fLog->Entry(MongoLog::Local, "Sleeping for two seconds");
    // For the sake of sanity and sleeping through the night,
    // do not remove this statement.
    sleep(2); // <-- this one. Leave it here.
    // Seriously. This sleep statement is absolutely vital.
    fLog->Entry(MongoLog::Local, "That felt great, thanks.");

    std::map<int, std::vector<uint16_t>> dac_values;
    std::vector<std::thread> init_threads; 
    init_threads.reserve(fDigitizers.size()); // reserve the memory for the threads . 
    std::map<int,int> rets;


      // Parallel digitizer programming to speed baselining
    for( auto& link : fDigitizers ) {
      rets[link.first] = 1;
      init_threads.emplace_back(&DAQController::InitLink, this,std::ref(link.second), std::ref(dac_values), std::ref(rets[link.first]));
      std::cout << "init_threads.emplace_back successfully" << std::endl ; 
    }
    for (auto& t : init_threads) {
      if (t.joinable()) 
      {
        std::cout << "Threads are joinable" << std::endl ; 
        t.join();
      }
    }// : is a range based for 

    if (std::any_of(rets.begin(), rets.end(), [](auto& p) {return p.second != 0;})) {
      fLog->Entry(MongoLog::Warning, "Encountered errors during digitizer programming");
      if (std::any_of(rets.begin(), rets.end(), [](auto& p) {return p.second == -2;}))
        fStatus = DAXHelpers::Error;
      else
        fStatus = DAXHelpers::Idle;
      return -1;
    } else
      fLog->Entry(MongoLog::Debug, "Digitizer programming successful");
    std::cout << "Digitizer programming successful" << std::endl ; 

    if (fOptions->GetString("baseline_dac_mode") == "fit" ) 
    {
      fOptions->UpdateDAC(dac_values);
      std::cout << "Digitizer::Arm baseline UpdateDAC " << std::endl ; 
    }

    for(auto& link : fDigitizers ) {
      for(auto& digi : link.second){
        digi->AcquisitionStop();
      }
    }
    std::cout << "DAQController::Arm AcquisitionStop " << std::endl ; 

    if (OpenThreads()) {
      fLog->Entry(MongoLog::Warning, "Error opening threads");
      std::cout << "Failed to open the threads " << std::endl ; 
      fStatus = DAXHelpers::Idle;
      return -1;
    }
    std::cout << "DAQController::Arm OpenThreads finished" << std::endl ; 
    //std::cout << "Have another sleep!!" << std::endl ; 
    sleep(2);

    //std::cout << "A good sleep !!  " << std::endl ; 
    fStatus = DAXHelpers::Armed;
    fLog->Entry(MongoLog::Local, "Arm command finished, returning to main loop");
    std::cout << "DAQController::Arm exit" << std::endl ; 

    return 0; 
  }



  int DAQController::Start()
  {
      if(fOptions->GetInt("run_start", 0) == 0)
  {
    for(auto& link : fDigitizers ){
      for(auto& digi : link.second){
        if(digi->EnsureReady()!= true || digi->SoftwareStart() || digi->EnsureStarted() != true){
          fLog->Entry(MongoLog::Warning, "Board %i not started?", digi->bid());
          return -1;
        } else
        {
          fLog->Entry(MongoLog::Local, "Board %i started", digi->bid());
          std::cout << "Board " << digi->bid() << "started" << std::endl ; 
        }
      }
    }
  } 
  else {
    for (auto& link : fDigitizers)
      for (auto& digi : link.second)
        if (digi->SINStart() || !digi->EnsureReady())
          fLog->Entry(MongoLog::Warning, "Board %i not ready to start?", digi->bid());
        else
          fLog->Entry(MongoLog::Local, "Board %i is ARMED and DANGEROUS", digi->bid());
  }
  fStatus = DAXHelpers::Running;
  return 0;
  }


  int DAQController::Stop()
  {
    std::cout << "DAQController::Stop" << std::endl; 
    fReadLoop = false; // at some point.

    for( auto const& link : fDigitizers )
    {
      for(auto digi : link.second)
      {
       digi->AcquisitionStop(true);

      // Ensure digitizer is stopped
        if(digi->EnsureStopped() != true){
	      fLog->Entry(MongoLog::Warning,"Timed out waiting for %i to stop after SW stop sent", digi->bid());
        }
      }
    } 

    CloseThreads();

    for(auto& link : fDigitizers )
    {
    for(auto& digi : link.second){
      digi->End();
      digi.reset();
    }
    link.second.clear();
    }
    fDigitizers.clear();

    fPLL = 0;
    fLog->SetRunId(-1);
    fOptions.reset();
    fLog->Entry(MongoLog::Local, "Finished end sequence");
    fStatus = DAXHelpers::Idle;

    return 0; 
  }



  void DAQController::InitLink(std::vector<std::shared_ptr<V1725>>& digis, std::map<int, std::vector<uint16_t>>& dac_values, int& ret)
  {
    std::cout << "DAQController::InitLink" << std::endl ;
    std::string baseline_mode = fOptions->GetString("baseline_dac_mode", "n/a");
    std::cout << "Baseline_Mode: " << baseline_mode << std::endl; 

    if (baseline_mode == "n/a")
    baseline_mode = fOptions->GetNestedString("baseline_dac_mode."+fOptions->Detector(), "fixed");
    int nominal_dac = fOptions->GetInt("baseline_fixed_value", 7000);
    //std::cout << "Got the baseline_fixed_value  " <<  nominal_dac  <<std::endl; 
    if (baseline_mode == "fit") {
      std::cout << "We do not support the fit mode !!!" << std::endl ;
      /*
      if ((ret = FitBaselines(digis, dac_values)) < 0) {
        fLog->Entry(MongoLog::Warning, "Errors during baseline fitting");
        return;
      } 
      else if (ret > 0) {
      fLog->Entry(MongoLog::Debug, "Baselines didn't converge so we'll use Plan B");
      }
      */
    }
    //std::cout << "DAQController::InitLink Finished fit the baseline DAQController::FitBaselines" << std::endl ; 
  
    for(auto& digi : digis ){
      fLog->Entry(MongoLog::Local, "Board %i beginning specific init", digi->bid());
      digi->ResetFlags();

      std::cout << "Board " << digi->bid() << " Baseline_mode : " << baseline_mode <<  std::endl ; 
      // Multiple options here
      int bid = digi->bid(), success(0);
      if (baseline_mode == "fit")
      {
      } 
      else if(baseline_mode == "cached") 
      {
        dac_values[bid] = fOptions->GetDAC(bid, digi->GetNumChannels(), nominal_dac);
        fLog->Entry(MongoLog::Local, "Board %i using cached baselines", bid);
      } 
      else if(baseline_mode == "fixed")
      {
        fLog->Entry(MongoLog::Local, "Loading fixed baselines with value 0x%04x", nominal_dac);
        dac_values[bid].assign(digi->GetNumChannels(), nominal_dac);
      } 
      else {
        fLog->Entry(MongoLog::Warning, "Received unknown baseline mode '%s', valid options are 'fit', 'cached', and 'fixed'", baseline_mode.c_str());
        ret = -1;
        return;
      }    

      success += digi->LoadDAC(dac_values[bid]);
      std::cout << "fOptions successfully got the dac_values" << std::endl;
      // Load all the other fancy stuff
      // bid is short for the board id . 
      success += digi->SetThresholds(fOptions->GetThresholds(bid));
      std::cout << "fOptions sucessfully get thresholds" << std::endl;  

  // Here are some default settings about the ADCs .
  int Nhandle = digi->fBoardHandle ; 
  int ret =0; 
	uint32_t regvalue;
	//reset the digitizer is very important 
	ret |= CAEN_DGTZ_Reset(Nhandle);
	ret |= CAEN_DGTZ_SetIOLevel(Nhandle, CAEN_DGTZ_IOLevel_TTL ); //  TTL mode 
	ret |= CAEN_DGTZ_SetSWTriggerMode(Nhandle, CAEN_DGTZ_TRGMODE_ACQ_ONLY); // Trigger mode acquisition only
	ret |= CAEN_DGTZ_SetExtTriggerInputMode(Nhandle, CAEN_DGTZ_TRGMODE_ACQ_ONLY); // External trigger set to the acquisition only 
	ret |= CAEN_DGTZ_SetChannelEnableMask(Nhandle, 0xFFFF); // Enable the 16 channels . 
	ret |= CAEN_DGTZ_WriteRegister(Nhandle, 0x8028, 0); // Set the Gainfactor to zero
	ret |= CAEN_DGTZ_SetMaxNumEventsBLT(Nhandle, 1023); // Max Num Events is 1023
	int pulse_type=0 ; 
	ret = CAEN_DGTZ_ReadRegister(Nhandle, 0x8000, &regvalue);
	ret |= CAEN_DGTZ_WriteRegister(Nhandle, 0x8000, (regvalue & ~(0x00000008)) | (uint32_t)(pulse_type << 3));
	// enable extended timestamp (bits [22:21] = "10")
	ret |= CAEN_DGTZ_ReadRegister(Nhandle, 0x811C, &regvalue);
	ret |= CAEN_DGTZ_WriteRegister(Nhandle, 0x811C, regvalue | 0x400000);
	// set acquisition mode
	ret |= CAEN_DGTZ_SetAcquisitionMode(Nhandle, CAEN_DGTZ_SW_CONTROLLED);
	// register 0x8100: set bit 2 to 1 if not in sw-controlled mode
	ret |= CAEN_DGTZ_ReadRegister(Nhandle, 0x8100, &regvalue);
	ret |= CAEN_DGTZ_WriteRegister(Nhandle, 0x00008100, regvalue | 0x000100);


	int RecordLength=36;
	int DCoffset=0;
	int preTrgg=0;
	int BaseMode=0; // 1 means the Fixed mode 
	int Baseline=8192;
	int NSampAhead=0;
	int MaxTail=10;
	int threshold=20;
	int polarity=1;
	int ST_Enable=1;

	int i ; 
	for (i=0; i<16;i++)
	{
		int channel=i;
		ret |= CAEN_DGTZ_SetRecordLength(Nhandle, RecordLength); // ATTENZIONE: ricontrollare la funzione nelle CAENDIGI																				   // set DC offset
		ret |= CAEN_DGTZ_SetChannelDCOffset(Nhandle, i, DCoffset);

		// pretrigger
		ret |= CAEN_DGTZ_WriteRegister(Nhandle, 0x1038 | (i << 8), preTrgg);

		//DAW baseline register
		ret |= CAEN_DGTZ_ReadRegister(Nhandle, 0x1080 | (i << 8), &regvalue);
		if (BaseMode) 
			ret |= CAEN_DGTZ_WriteRegister(Nhandle, 0x1080 | (i << 8), (regvalue & (0xff8fffff)));
		else 
			ret |= CAEN_DGTZ_WriteRegister(Nhandle, 0x1080 | (i << 8), regvalue | 0x00100000);
		ret |= CAEN_DGTZ_ReadRegister(Nhandle, 0x1064 | (channel << 8), &regvalue);
		regvalue = (regvalue & (uint32_t)(~(0x00003fff))) | (uint32_t)(Baseline & 0x3fff); // replace only the two bits affecting the selected couple's logic.
		ret |= CAEN_DGTZ_WriteRegister(Nhandle, 0x1064 | (channel << 8), regvalue);

		//NSampAhead
		ret |= CAEN_DGTZ_WriteRegister(Nhandle, 0x1078 | (i << 8),NSampAhead);

		// MaxTail
		ret |= CAEN_DGTZ_WriteRegister(Nhandle, 0x107C | (i << 8), MaxTail);

		//DAW Trigger Threshold
		ret |= CAEN_DGTZ_WriteRegister(Nhandle, 0x1060 | (i << 8), (uint32_t)(threshold & 0x3FFF));
		
		//DAW signal logic register
		ret |= CAEN_DGTZ_ReadRegister(Nhandle, 0x1080 | (channel << 8), &regvalue);
		(polarity) ? (regvalue = regvalue & ~(0x00010000)) : (regvalue = regvalue | 0x00010000);
		ret |= CAEN_DGTZ_WriteRegister(Nhandle, 0x1080 | (channel << 8), regvalue);

		// Software Trigger Enable
		
		if (ST_Enable)
		{
		ret |= CAEN_DGTZ_ReadRegister(Nhandle, 0x1080 | (channel << 8), &regvalue);
		ret |= CAEN_DGTZ_WriteRegister(Nhandle, 0x1080 | (channel << 8), regvalue & ~(uint32_t)(0x1000000));
		}
		else
		{
		ret |= CAEN_DGTZ_ReadRegister(Nhandle, 0x1080 | (channel << 8), &regvalue);
		ret |= CAEN_DGTZ_WriteRegister(Nhandle, 0x1080 | (channel << 8), regvalue | (uint32_t)(0x1000000));
		}
  }
		

		// Test pulse enable

      for(auto& regi : fOptions->GetRegisters(bid))
      {
        unsigned int reg = DAXHelpers::StringToHex(regi.reg);
        unsigned int val = DAXHelpers::StringToHex(regi.val);
        success+=digi->WriteRegister(reg, val);
      }
      // We write the registers during the initlink . 
      std::cout << "fOptions successfully got registers" << std::endl ;  

      fLog->Entry(MongoLog::Local, "Board %i programmed", digi->bid());
      if(success!=0)
      {
        fLog->Entry(MongoLog::Warning, "Failed to configure digitizers.");
        ret = -1;
        return;
      }
    }

    ret = 0;
    std::cout << "DAQController::InitLink Exit" << std::endl ;
    return ; 
  }


int DAQController::OpenThreads()
{
  const std::lock_guard<std::mutex> lg(fMutex);
  std::cout << "Function DAQController::OpenThreads" << std::endl ; 
  fProcessingThreads.reserve(fNProcessingThreads);

  // For the time being , we do not have threads for processing , the readout and the process are the same thread 
  fReadoutThreads.reserve(fDigitizers.size()); // .reserve could modify the capacity of the vector . 
  
  int fDigitizers_count =0 ; 
  for (auto& p : fDigitizers)
  {
    fDigitizers_count += 1; 
    fReadoutThreads.emplace_back(&DAQController::ReadData, this, p.first);
  }
  std::cout << "fDigitizers number: " << fDigitizers_count << std::endl ; 
  std::cout << "DAQController::OpenThreads Exit" << std::endl; 

  return 0 ; 
}

void DAQController::CloseThreads()
{
  const std::lock_guard<std::mutex> lg(fMutex);
  fLog->Entry(MongoLog::Local, "Ending RO threads");
  for (auto& t : fReadoutThreads) if (t.joinable()) t.join();
  std::map<int,int> board_fails;

  if (std::accumulate(board_fails.begin(), board_fails.end(), 0,[=](int tot, auto& iter) 
  {return std::move(tot) + iter.second;})) 
  {
    std::stringstream msg;
    msg << "Found board failures: ";
    for (auto& iter : board_fails) msg << iter.first << ":" << iter.second << " | ";
    fLog->Entry(MongoLog::Warning, msg.str());
  }

  //std::cout << link << std::endl; 
    
  return ; 
}


void DAQController::ReadData(int link)
{
  fReadLoop = true;
  fDataRate = 0;
  uint32_t board_status = 0;
  int readcycler = 0;
  int err_val = 0;
  std::list<std::unique_ptr<data_packet>> local_buffer;
  std::unique_ptr<data_packet> dp;
  std::vector<int> mutex_wait_times;
  mutex_wait_times.reserve(1<<20);
  int words = 0;
  unsigned transfer_batch = fOptions->GetInt("transfer_batch", 8);
  int bytes_this_loop(0);
  fRunning[link] = true;
  std::chrono::microseconds sleep_time(fOptions->GetInt("us_between_reads", 10));
  //int c = 0;
  //const int num_threads = fNProcessingThreads;

    while(fReadLoop){
    for(auto& digi : fDigitizers[link]) {
      // periodically report board status
      if(readcycler == 0){
        board_status = digi->GetAcquisitionStatus();
        fLog->Entry(MongoLog::Local, "Board %i has status 0x%04x",
            digi->bid(), board_status);
      }
      if (digi->CheckFail()) {
        err_val = digi->CheckErrors();
        fLog->Entry(MongoLog::Local, "Error %i from board %i", err_val, digi->bid());
        std::cout << "Error " << err_val << " from board" << digi->bid() << std::endl ; 
        if (err_val == -1 || err_val == 0) {
        } else {
          fStatus = DAXHelpers::Error; // stop command will be issued soon
          if (err_val & 0x1) {
            fLog->Entry(MongoLog::Local, "Board %i has PLL unlock", digi->bid());
            fPLL++;
          }
          if (err_val & 0x2) fLog->Entry(MongoLog::Local, "Board %i has VME bus error", digi->bid());
        }
      }
      if((words = digi->Read(dp))<0){ 
        dp.reset();
        fStatus = DAXHelpers::Error;
        break;
      } 
      else if(words>0){
        //std::cout << words << std::endl ; 
        //dp->digi = digi;
        //local_buffer.emplace_back(std::move(dp));
        //bytes_this_loop += words*sizeof(char32_t);
      }
    } // for digi in digitizers
    if (local_buffer.size() && (readcycler % transfer_batch == 0)) {
      fDataRate += bytes_this_loop;
      auto t_start = std::chrono::high_resolution_clock::now();
      auto t_end = std::chrono::high_resolution_clock::now();
      mutex_wait_times.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
            t_end-t_start).count());
      bytes_this_loop = 0;
    }
    if (++readcycler > 10000) readcycler = 0;
    std::this_thread::sleep_for(sleep_time);
  } // while run
  if (mutex_wait_times.size() > 0)
   {
    std::sort(mutex_wait_times.begin(), mutex_wait_times.end());
    fLog->Entry(MongoLog::Local, "RO thread %i mutex report: min %i max %i mean %i median %i num %i",
        link, mutex_wait_times.front(), mutex_wait_times.back(),
        std::accumulate(mutex_wait_times.begin(), mutex_wait_times.end(), 0l)/mutex_wait_times.size(),
        mutex_wait_times[mutex_wait_times.size()/2], mutex_wait_times.size());
  }
  fRunning[link] = false;
  fLog->Entry(MongoLog::Local, "RO thread %i returning", link);
  std::cout << "DAQController::ReadData exit" << std::endl ; 

  return ; 
}
