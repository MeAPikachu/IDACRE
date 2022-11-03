#include "../include/V1725.hh"
#include "../include/MongoLog.hh"
#include "../include/Options.hh"
#include "../include/StraxFormatter.hh"
#include <algorithm>
#include <cmath>
#include <CAENDigitizer.h>
#include <CAENVMElib.h>
#include <CAENDigitizerType.h>
#include <sstream>
#include <list>
#include <utility>
#include <bitset>
#include <algorithm>

class Options;
class MongoLog;
class V1725;

V1725::V1725(std::shared_ptr<MongoLog>& log, std::shared_ptr<Options>& opts, int bid, unsigned address){
  std::cout << "V1725::V1725 Create Object V1725" << std::endl ;
  // Basic information : flog , fopts , boardid , VME address 
  fBoardHandle = -1;
  fLog = log;
  fOptions = opts;
  

  fAqCtrlRegister = 0x8100;
  fAqStatusRegister = 0x8104;
  fSwTrigRegister = 0x8108;
  fResetRegister = 0xEF24;
  fClearRegister = 0xEF28;
  fChStatusRegister = 0x1088;
  fChDACRegister = 0x1098;
  fChTrigRegister = 0x1060;
  fSNRegisterMSB = 0xF080;
  fSNRegisterLSB = 0xF084;
  fBoardFailStatRegister = 0x8178;
  fReadoutStatusRegister = 0xEF04;
  fBoardErrRegister = 0xEF00;
  fInputDelayRegister = 0x8034;
  fInputDelayChRegister = 0x1034;
  fPreTrigRegister = 0x8038;
  fPreTrigChRegister = 0x1038;
  fError = false;

  fBID = bid;
  fBaseAddress = address;
  fRolloverCounter = 0;
  fLastClock = 0;
  fBLTSafety = opts->GetDouble("blt_safety_factor", 1.5);
  fBLTalloc = opts->GetBLTalloc();

  // there's a more elegant way to do this, but I'm not going to write it
  
  fBufferSize = 0x800000; // 8 MB total memory 
  fSampleWidth = 2;
  fClockCycle = 2;
  fNChannels = 16;
  fArtificialDeadtimeChannel = 792;
  fDefaultDelay = 0xA * 2 * fSampleWidth; // see register document
  fDefaultPreTrig = 6 *  fSampleWidth; // see register document

  fClockPeriod = std::chrono::nanoseconds((1l<<31)*fClockCycle);
  fRegisterFlags = 1;

  RawFile = (FILE**)calloc(1, sizeof(FILE*));
  std::stringstream raw_fmt;
  raw_fmt << opts->GetString("strax_output_path") << "/raw " << fBID << ".bin";
  std::string raw_new= raw_fmt.str();  
  *RawFile= fopen(raw_new.c_str(),"w");


  WaveFile= (FILE**)calloc(fNChannels, sizeof(FILE*));

  int channel;
  for (channel=0; channel<fNChannels; channel++)
  {
    std::stringstream wave_fmt ;
    wave_fmt << opts->GetString("strax_output_path") << "/raw"<< fBID << "_"<< channel << ".txt";
    std::string wave_new = wave_fmt.str();
    WaveFile[channel] = fopen(wave_new.c_str(),"w");
  }



}

V1725::~V1725(){
  End();
  if (fBLTCounter.empty()) return;
  std::stringstream msg;
  msg << "BLT report for board " << fBID;
  for (auto p : fBLTCounter) msg << " | " << p.first << " " << int(std::log2(p.second));
  msg << " | " << long(fTotReadTime.count());
  fLog->Entry(MongoLog::Local, msg.str());
}





int V1725::Init(int link, int crate) {
  std::cout << "Initialize the V1725 Connection " << std::endl ; 
  int a= CAEN_DGTZ_OpenDigitizer( CAEN_DGTZ_OpticalLink , crate, link, fBaseAddress , &fBoardHandle);
  // int a = CAENVME_Init(cvV2718, link, crate, &fBoardHandle);
  std::cout << "CAEN_DGTZ_OPENDIGITZER " << a << std::endl ; 
  if(a != cvSuccess){
    fLog->Entry(MongoLog::Warning, "Board %i failed to init, error %i handle %i link %i bdnum %i",
            fBID, a, fBoardHandle, link, crate);
    fBoardHandle = -1;
    std::cout << "CAEN_DGTZ_OPENDIGITIZER  failed" << std::endl ; 
    return -1;
  }

  CAEN_DGTZ_GetMaxNumEventsBLT(fBoardHandle,&BLTn);
  // Here is some code from the DAW_Demo that helps get the board serial number ;
  CAEN_DGTZ_BoardInfo_t BoardInfo;
  CAEN_DGTZ_GetInfo(fBoardHandle,&BoardInfo);
  std::cout << "Board serial number : " << BoardInfo.SerialNumber << std::endl;
  fLog->Entry(MongoLog::Local, "Board serial number %d" , BoardInfo.SerialNumber );


  fLog->Entry(MongoLog::Debug, "Board %i initialized with handle %i (link/crate)(%i/%i)",
	      fBID, fBoardHandle, link, crate);
  uint32_t word=0;
  int my_bid=0;
  fROBuffer.assign(fBufferSize*3, 0); // double buffer for safety

  if (Reset()) 
  {
    fLog->Entry(MongoLog::Error, "Board %i unable to pre-load registers", fBID);
    return -1;
  } else 
  {
    fLog->Entry(MongoLog::Local, "Board %i reset", fBID);
  }
  std::cout << "Board reset successfully" << std::endl ; 

  fDelayPerCh.assign(fNChannels, fDefaultDelay);
  // vector assign means that the number and default value of it .
  fPreTrigPerCh.assign(fNChannels, fDefaultPreTrig);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  std::cout<< "Board sleep for 10 ms successfully" << std::endl ; 

  if (fOptions->GetInt("do_sn_check", 0) != 0) {
    if ((word = ReadRegister(fSNRegisterLSB)) == 0xFFFFFFFF) {
      fLog->Entry(MongoLog::Error, "Board %i couldn't read its SN lsb", fBID);
      return -1;
    }
    my_bid |= word&0xFF;
    if ((word = ReadRegister(fSNRegisterMSB)) == 0xFFFFFFFF) {
      fLog->Entry(MongoLog::Error, "Board %i couldn't read its SN msb", fBID);
      return -1;
    }
    my_bid |= ((word&0xFF)<<8);
    if (my_bid != fBID) {
      fLog->Entry(MongoLog::Local, "Link %i crate %i should be SN %i but is actually %i",
        link, crate, fBID, my_bid);
    }
  }



  std::cout << "SN_Check succeeded" << std::endl; 
  return 0;
}

int V1725::SINStart(){
  fLastClockTime = std::chrono::high_resolution_clock::now();
  fRolloverCounter = 0;
  fLastClock = 0;
  return WriteRegister(fAqCtrlRegister,0x105);
}
int V1725::SoftwareStart(){
  fLastClockTime = std::chrono::high_resolution_clock::now();
  fRolloverCounter = 0;
  fLastClock = 0;
  return WriteRegister(fAqCtrlRegister, 0x104);
}
int V1725::AcquisitionStop(bool){
  return WriteRegister(fAqCtrlRegister, 0x100);
}
int V1725::SWTrigger(){
  return WriteRegister(fSwTrigRegister, 0x1);
}
bool V1725::EnsureReady(int ntries, int tsleep){
  return MonitorRegister(fAqStatusRegister, 0x100, ntries, tsleep, 0x1);
}
bool V1725::EnsureStarted(int ntries, int tsleep){
  return MonitorRegister(fAqStatusRegister, 0x4, ntries, tsleep, 0x1);
}
bool V1725::EnsureStopped(int ntries, int tsleep){
  return MonitorRegister(fAqStatusRegister, 0x4, ntries, tsleep, 0x0);
}
uint32_t V1725::GetAcquisitionStatus(){
  return ReadRegister(fAqStatusRegister);
}

int V1725::CheckErrors(){
  auto pll = ReadRegister(fBoardFailStatRegister);
  auto ros = ReadRegister(fReadoutStatusRegister);
  unsigned ERR = 0xFFFFFFFF;
  if ((pll == ERR) || (ros == ERR)) return -1;
  int ret = 0;
  if (pll & (1 << 4)) ret |= 0x1;
  if (ros & (1 << 2)) ret |= 0x2;
  return ret;
}

int V1725::Reset() {
  int ret = WriteRegister(fResetRegister, 0x1);
  ret += WriteRegister(fBoardErrRegister, 0x30);
  return ret;
}

std::tuple<uint32_t, long> V1725::GetClockInfo(std::u32string_view sv) {
  auto it = sv.begin();
  do {
    if ((*it)>>28 == 0xA) {
      uint32_t ht = *(it+3)&0x7FFFFFFF;
      return {ht, GetClockCounter(ht)};
    }
  } while (++it < sv.end());
  fLog->Entry(MongoLog::Message, "No clock info for %i?", fBID);
  return {0xFFFFFFFF, -1};
}

int V1725::GetClockCounter(uint32_t timestamp){
  // The V1725 has a 31-bit on board clock counter that counts 10ns samples.
  // So it will reset every 21 seconds. We need to count the resets or we
  // can't run longer than that. We can employ some clever logic
  // and real-time time differences to handle clock rollovers and catch any
  // that we happen to miss the usual way

  auto now = std::chrono::high_resolution_clock::now();
  std::chrono::nanoseconds dt = now - fLastClockTime;
  fLastClockTime += dt; // no operator=

  int n_missed = dt / fClockPeriod;
  if (n_missed > 0) {
    fLog->Entry(MongoLog::Message, "Board %i missed %i rollovers", fBID, n_missed);
    fRolloverCounter += n_missed;
  }

  if (timestamp < fLastClock) {
    // actually rolled over
    fRolloverCounter++;
    fLog->Entry(MongoLog::Local, "Board %i rollover %i (%x/%x)",
        fBID, fRolloverCounter, fLastClock, timestamp);
  } else {
    // not a rollover
  }
  fLastClock = timestamp;
  return fRolloverCounter;
}

int V1725::WriteRegister(unsigned int reg, uint32_t value){
  bool echo = fRegisterFlags & 0x1;
  int ret = 0;
  
  if (reg == fInputDelayRegister)
    fDelayPerCh.assign(fNChannels, 2*fSampleWidth*value);
  else if ((reg & fInputDelayChRegister) == fInputDelayChRegister)
    fDelayPerCh[(reg>>16)&0xF] = 2*fSampleWidth*value;
  else if (reg == fPreTrigRegister)
    fPreTrigPerCh.assign(fNChannels, 2*fSampleWidth*value);
  else if ((reg & fPreTrigChRegister) == fPreTrigChRegister)
    fPreTrigPerCh[(reg>>16)&0xF] = 2*fSampleWidth*value;
  
  if((ret = CAEN_DGTZ_WriteRegister(fBoardHandle, fBaseAddress+reg,
			value)) != cvSuccess){
    fLog->Entry(MongoLog::Warning,
		"Board %i write returned %i (ret), reg 0x%04x, value 0x%08x",
		fBID, ret, reg, value);
    return -1;
  }
  if (echo) fLog->Entry(MongoLog::Local, "Board %i wrote 0x%x to 0x%04x", fBID, value, reg);
  // would love to confirm the write, but not all registers are read-able and you get a -1 if you try
  // and I don't feel like coding in the entire register document so we know which are which
  return 0;
}

unsigned int V1725::ReadRegister(unsigned int reg){
  unsigned int temp;
  int ret = 0;
  if((ret = CAEN_DGTZ_ReadRegister(fBoardHandle ,fBaseAddress+reg, &temp)) != cvSuccess){
    fLog->Entry(MongoLog::Warning, "Board %i read returned: %i (ret) 0x%x (val) for reg 0x%04x", fBID, ret, temp, reg);
    return 0xFFFFFFFF;
  }
  return temp;
}

int V1725::Read(std::unique_ptr<data_packet>& outptr){
  using namespace std::chrono;
  auto t_start = high_resolution_clock::now();
  // auto status = GetAcquisitionStatus();

  char * buffer= NULL ;
  CAEN_DGTZ_730_DAW_Event_t **Event= NULL ;
  uint32_t * NumEvents ;

  Event = (CAEN_DGTZ_730_DAW_Event_t**)calloc(fBoardHandle, sizeof(CAEN_DGTZ_730_DAW_Event_t*));
  // std::cout << "Prepare to allocate buffer for V1725" << std::endl ;
  // Allocate Readout buffer and DPP event buffer . 
  CAEN_DGTZ_MallocReadoutBuffer(fBoardHandle,&buffer,&AllocatedSize);
  CAEN_DGTZ_MallocDPPEvents(fBoardHandle,(void**)&Event[0],&AllocatedSize);
  NumEvents = (uint32_t*)calloc(1, sizeof(int)); 

  CAEN_DGTZ_ErrorCode ret ; 
  ret=CAEN_DGTZ_ReadData(fBoardHandle,CAEN_DGTZ_SLAVE_TERMINATED_READOUT_MBLT,buffer,&BufferSize);

  if (ret!=0)
  {
    std::cout << fBID  << "CAEN_DGTZ_ReadData Failed  : " << ret  << std::endl ;
  }

  if (BufferSize >0)
  {
    std::cout << fBID <<"Get some data !!" << std::endl;
    ret=CAEN_DGTZ_GetDPPEvents(fBoardHandle,buffer,BufferSize/4,(void**)&Event[0],&NumEvents[0]);

    if (ret!=0){std::cout << "CAEN_DGTZ_GetDPPEvents Failed" << std::endl ;}


    fprintf(*RawFile,buffer);
    
    int sample ; 
    int channel ; 
    for (channel=0 ; channel < fNChannels ; channel ++ )
    {
	    for (sample=0 ; sample < 2*Event[0]->Channel[channel]->size ; sample++)
	    {
      // std::cout << Event[0]->Channel[0]->size << std::endl ;
      fprintf(WaveFile[channel],"%*d\t",7,sample);
      fprintf(WaveFile[channel],"%*u\n",12, *(Event[0]->Channel[0]->DataPtr + sample));
	    //fprintf(*RawFile,"%*d\t",7,sample);
	    //
	    }}
  }




  // copy from the digitizer's buffer into something we can send downstream
  /*
  int words = total_bytes/sizeof(char32_t);
  std::cout << "words" << words << std::endl ; 
  if(words>0){
    std::u32string s;
    s.reserve(words);
    s.append((char32_t*)fROBuffer.data(), words);
    fBLTCounter[int(std::ceil(std::log2(words)))]++;
    auto [ht, cc] = GetClockInfo(s);
    outptr = std::make_unique<data_packet>(std::move(s), ht, cc);
  }
  */
  fTotReadTime += duration_cast<nanoseconds>(high_resolution_clock::now()-t_start);

  return BufferSize/4;
}

int V1725::LoadDAC(std::vector<uint16_t>& dac_values){
  // Loads DAC values into registers
  std::cout << "Function V1725::LoadADC" << std::endl ; 
  for(unsigned ch=0; ch<fNChannels; ch++){
    if ((ReadRegister(fChStatusRegister + 0x100*ch) & 0x4) || WriteRegister(fChDACRegister + 0x100*ch, dac_values[ch])){
      fLog->Entry(MongoLog::Error, "Board %i ch %i failed to set DAC (0x%x)", fBID, ch, dac_values[ch]);
      return -1;
    }
  }
  std::cout << "Finish Function V1725::LoadDAC" << std::endl ;
  return 0;
}

int V1725::SetThresholds(std::vector<uint16_t> vals) {
  int ret = 0;
  for (unsigned ch = 0; ch < fNChannels; ch++)
    ret += WriteRegister(fChTrigRegister + 0x100*ch, vals[ch]);
  return ret;
}

int V1725::End(){
  if(fBoardHandle>=0)
    CAEN_DGTZ_CloseDigitizer(fBoardHandle);
  fBoardHandle=-1;
  fBaseAddress=0;
  return 0;
}

bool V1725::MonitorRegister(uint32_t reg, uint32_t mask, int ntries, int sleep, uint32_t val){
  uint32_t rval = 0;
  if(val == 0) rval = 0xffffffff;
  for(int counter = 0; counter < ntries; counter++){
    rval = ReadRegister(reg);
    if(rval == 0xffffffff)
      break;
    if((val == 1 && (rval&mask)) || (val == 0 && !(rval&mask)))
      return true;
    usleep(sleep);
  }
  fLog->Entry(MongoLog::Warning,"Board %i MonitorRegister failed for 0x%04x with mask 0x%04x and register value 0x%04x, wanted 0x%04x",
          fBID, reg, mask, rval,val);
  return false;
}

std::tuple<int, int, bool, uint32_t> V1725::UnpackEventHeader(std::u32string_view sv) {
  // returns {words this event, channel mask, board fail, header timestamp}
  return {sv[0]&0xFFFFFFF, sv[1]&0xFF, sv[1]&0x4000000, sv[3]&0x7FFFFFFF};
}

std::tuple<int64_t, int, uint16_t, std::u32string_view> V1725::UnpackChannelHeader(std::u32string_view sv, long rollovers, uint32_t header_time, uint32_t, int, int, short ch) {
  // returns {timestamp (ns), words this channel, baseline, waveform}
  long ch_time = sv[1]&0x7FFFFFFF;
  int words = sv[0]&0x7FFFFF;
  // More rollover logic here, because channels are independent and the
  // processing is multithreaded. We leverage the fact that readout windows are
  // short and polled frequently compared to the rollover timescale, so there
  // will never be a large difference in timestamps in one data packet
  if (ch_time > 15e8 && header_time < 5e8 && rollovers != 0) rollovers--;
  else if (ch_time < 5e8 && header_time > 15e8) rollovers++;
  return {((rollovers<<31)+ch_time)*fClockCycle - fDelayPerCh[ch] - fPreTrigPerCh[ch], words, 0, sv.substr(2, words-2)};
}

int V1725::BaselineStep(std::vector<uint16_t>& dac_values, std::vector<int>& channel_finished, std::vector<double>& bl_per_channel, int step) {
  /* One step in the baseline sequence. The DAC values passed in have already been loaded onto the board.
   * This function starts the board, triggers it, stops it, reads it out, and processes the result.
   * :param dac_values: input vector of DAC values that this iteration will use
   * :param channel_finished: input/output vector of how close to convergence a each channel is
   * :param bl_per_channel: output vector of what baseline values were read this step
   * :param step: the step number
   * :returns: <0 for critical failure, >0 for noncritical failure, 0 otherwise
   */
  std::cout << "V172t::BaselineStep get baseline parameters" << std::endl ; 
  int triggers_per_step = fOptions->GetInt("baseline_triggers_per_step", 3);
  //std::cout << 1 << std::endl ; 
  std::chrono::milliseconds ms_between_triggers(fOptions->GetInt("baseline_ms_between_triggers", 10));
  //std::cout << 2 << std::endl ; 
  int adjustment_threshold = fOptions->GetInt("baseline_adjustment_threshold", 10);
  //std::cout << 3 << std::endl ; 
  int min_adjustment = fOptions->GetInt("baseline_min_adjustment", 0xC);
  //std::cout << 4 << std::endl ; 
  int rebin_factor = fOptions->GetInt("baseline_rebin_log2", 1); // log base 2
  //std::cout << 5 << std::endl ; 
  int bins_around_max = fOptions->GetInt("baseline_bins_around_max", 3);
  //std::cout << 6 << std::endl ; 
  int target_baseline = fOptions->GetInt("baseline_value", 16000);
  //std::cout << 7 << std::endl ; 
  // int rather than uint16_t for type promotion reasons
  int min_dac = fOptions->GetInt("baseline_min_dac", 0), max_dac = fOptions->GetInt("baseline_max_dac", 1<<16);
  //std::cout << 8 << std::endl ; 
  int convergence = fOptions->GetInt("baseline_convergence_threshold", 3);
  //std::cout << 9 << std::endl ; 
  int counts_total(0), counts_around_max(0);
  double fraction_around_max = fOptions->GetDouble("baseline_fraction_around_max", 0.8), baseline;
  //std::cout << 10 << std::endl ; 
  // 14-bit ADC to 16-bit DAC. Not 4 because we want some damping to prevent overshoot
  double adc_to_dac = fOptions->GetDouble("baseline_adc_to_dac", -3.);
  //std::cout << 11 << std::endl ; 


  uint32_t words_in_event, channel_mask, words_in_channel;
  int channels_in_event, words_read;
  if (!EnsureReady(1000, 1000)) {
    fLog->Entry(MongoLog::Warning, "Board %i not ready for baselines", fBID);
    return -1;
  }
  SoftwareStart();
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  if (!EnsureStarted(1000, 1000)) {
    fLog->Entry(MongoLog::Warning, "Board %i can't start baselines", fBID);
    return -1;
  }
  for (int trig = 0; trig < triggers_per_step; trig++) {
    SWTrigger();
    std::this_thread::sleep_for(ms_between_triggers);
  }

  AcquisitionStop();
  if (!EnsureStopped(1000, 1000)) {
    fLog->Entry(MongoLog::Warning, "Board %i won't stop", fBID);
    return -1;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  std::unique_ptr<data_packet> dp;
  if ((words_read = Read(dp)) <= 0) {
    fLog->Entry(MongoLog::Warning, "Board %i readout error", fBID);
    return -2;
  }
  if (words_read <= 4) {
    fLog->Entry(MongoLog::Local, "Board %i missing data?? %i", fBID, words_read);
    return 1;
  }
  // we now have a data packet with something in it, let's process it
  std::vector<std::vector<int>> hist(fNChannels, std::vector<int>(1 << (14 - rebin_factor), 0));
  auto it = dp->buff.begin();
  while (it < dp->buff.end()) {
    if ((*it) >> 28 == 0xA) {
      std::u32string_view sv(dp->buff.data() + std::distance(dp->buff.begin(), it), (*it)&0xFFFFFFF);
      std::tie(words_in_event, channel_mask, std::ignore, std::ignore) = UnpackEventHeader(sv);
      if (words_in_event == 4) {
        it += 4;
        continue;
      }
      if (channel_mask == 0) { // should be impossible?
        it += 4;
        continue;
      }
      channels_in_event = std::bitset<16>(channel_mask).count();
      it += words_in_event;
      sv.remove_prefix(4);
      for (unsigned ch = 0; ch < fNChannels; ch++) {
        if (!(channel_mask & (1 << ch))) continue;
        std::u32string_view wf;
        std::tie(std::ignore, words_in_channel, std::ignore, wf) = UnpackChannelHeader(sv,
            0, 0, 0, words_in_event, channels_in_event, ch);
        for (auto w : wf) {
          for (auto val : {w&0x3fff, (w>>16)&0x3fff}) {
            if (val != 0 && val != 0x3fff)
              hist[ch][val >> rebin_factor]++;
          }
        }
        sv.remove_prefix(words_in_channel);
      } // for channels
    } else { // if header
      ++it;
    }
  } // while in buffer
  // we split here so we actually accumulate all the events we triggered
  // and analyze once, rather than analyze once per trigger and only keep the last one
  for (unsigned ch = 0; ch < fNChannels; ch++) {
    if (channel_finished[ch] >= convergence) continue;

    auto max_it = std::max_element(hist[ch].begin(), hist[ch].end());
    auto max_start = std::max(max_it - bins_around_max, hist[ch].begin());
    auto max_end = std::min(max_it + bins_around_max + 1, hist[ch].end());
    counts_total = std::accumulate(hist[ch].begin(), hist[ch].end(), 0);
    counts_around_max = std::accumulate(max_start, max_end, 0);
    if (counts_total == 0) {
      fLog->Entry(MongoLog::Local, "%i.%i.%i: no samples (%x)", fBID, ch, step, dac_values[ch]);
      // this will produce a nan which is fine because this causes a reset on that channel
    } else if (counts_around_max < fraction_around_max*counts_total) {
      fLog->Entry(MongoLog::Local, "%i.%i.%i: %i out of %i counts around max %i",
          fBID, ch, step, counts_around_max, counts_total, (max_it - hist[ch].begin())<<rebin_factor);
      continue;
    }
    std::vector<int> bin_ids(max_end - max_start, 0);
    std::iota(bin_ids.begin(), bin_ids.end(), max_start - hist[ch].begin());
    // calculated weighted average
    baseline = std::inner_product(max_start, max_end, bin_ids.begin(), 0) << rebin_factor;
    baseline /= counts_around_max;
    bl_per_channel[ch] = baseline;

    float off_by = target_baseline - baseline;
    if (off_by != off_by) { // dirty nan check
      // log at the debug rather than warning level because we understand where NaNs come from and how to handle them
      fLog->Entry(MongoLog::Debug, "%i.%i.%i: NaN alert (%x)",
          fBID, ch, step, dac_values[ch]);
      dac_values[ch] = fOptions->GetInt("baseline_start_dac", 10000); // reset this channel, dun goof'd
      channel_finished[ch] = 0;
    } else if (abs(off_by) < adjustment_threshold) {
      ++channel_finished[ch];
      if (channel_finished[ch] == convergence)
        fLog->Entry(MongoLog::Local, "%i.%i.%i converged: %.1f | %x", fBID, ch, step, baseline, dac_values[ch]);
    } else {
      channel_finished[ch] = std::max(0, channel_finished[ch]-1);
      int adjustment = off_by * adc_to_dac;
      if (abs(adjustment) < min_adjustment)
        adjustment = std::copysign(min_adjustment, adjustment);
      dac_values[ch] = std::clamp(dac_values[ch] + adjustment, min_dac, max_dac);
      fLog->Entry(MongoLog::Local, "%i.%i.%i adjust %i to %x (%.1f)", fBID, ch, step, adjustment, dac_values[ch], baseline);
    }

  } // for each channel
  std::cout << "Exit the V1725::BaselineStep" << std::endl ; 
  return 0;
}
