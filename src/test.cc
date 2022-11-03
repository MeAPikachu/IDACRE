#include "../include/test.hh"
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
#include <iostream>

// Para is a copy of Chengjie's code DAW_Test .
int para()
{
    int Nhandle=1; 
	int handle=1; 
    CAEN_DGTZ_ConnectionType LinkType = CAEN_DGTZ_OpticalLink ; 
    int LinkNum=0;
    int ConetNum=0;
    int BaseAddress=0;

    // Init the OpenDigitizer
	int open; 
    open = CAEN_DGTZ_OpenDigitizer(LinkType, LinkNum, ConetNum, BaseAddress , &Nhandle);
	std::cout << open << std::endl; 

    // Get the Board information 
    CAEN_DGTZ_BoardInfo_t BoardInfo;
    if (CAEN_DGTZ_GetInfo(Nhandle,&BoardInfo) !=0 )
    {
        std::cout << "failed to get the basic info" << std::endl;
    }
    std::cout<< "Board Model:" << BoardInfo.ModelName << std::endl;
	std::cout<< "Serial Number:" << BoardInfo.SerialNumber << std::endl;  

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
	std::cout << "Finish set up the Digitizer" << std::endl; 


	// event size 
	uint32_t BLTn;
	ret|=CAEN_DGTZ_GetMaxNumEventsBLT(Nhandle, &BLTn);
	std::cout << "Event Size:"<< BLTn << std::endl; 

	// event per block .
	uint32_t maxEvents;
	long int lsize = 0xffffff;
	ret|=CAEN_DGTZ_GetMaxNumEventsBLT(handle, &maxEvents);
	long int size = 4 * ((((lsize * 2) + 3)*16) + 4)*maxEvents;
	std::cout << std::hex <<"Events per block:"<<size << std::endl;



	CAEN_DGTZ_730_DAW_Event_t      **Event = NULL; 
	// as many events as the maximum number obtainable with a BLT transfer access must be allocated 
	if ((Event = (CAEN_DGTZ_730_DAW_Event_t**)calloc(Nhandle, sizeof(CAEN_DGTZ_730_DAW_Event_t*))) == NULL) 
		std::cout << "Event Allocated failed" << std::endl;
	uint32_t AllocatedSize ; 
	// ret |= CAEN_DGTZ_MallocDPPEvents(Nhandle, Event, &AllocatedSize);
	// this line of code does not work well ... 
	
	char *buffer = NULL;
	ret |= CAEN_DGTZ_MallocReadoutBuffer(Nhandle, &buffer, &AllocatedSize);
	ret |= CAEN_DGTZ_MallocDPPEvents(Nhandle,(void**)&Event[0],&AllocatedSize);
	
	uint32_t * NumEvents; 
	NumEvents = (uint32_t*)calloc(1, sizeof(int));

	FILE **RawFile;
	RawFile = (FILE**)calloc(1, sizeof(FILE*));

	*RawFile= fopen("/home/data/tpc/new.txt","w");
	if (*RawFile == NULL)
	{
		std::cout << "RawFile Opened failed" << std::endl; 
	}

	CAEN_DGTZ_SWStartAcquisition(Nhandle);
	CAEN_DGTZ_ReadRegister(Nhandle, 0x8104, &regvalue);
	std::cout << regvalue << std::endl; 

	uint32_t BufferSize=0;
	uint32_t count=0; 
	for (i=0;i<=1000;i++)
	{
		CAEN_DGTZ_ReadData(Nhandle, CAEN_DGTZ_SLAVE_TERMINATED_READOUT_MBLT, buffer, &BufferSize);	
		if (BufferSize>0)
		{
			CAEN_DGTZ_GetDPPEvents(Nhandle,buffer,BufferSize/4,(void**)&Event[0],&NumEvents[0]);
			std::cout<<"Get the data!!" << BufferSize << &buffer << std::endl;
			count ++ ; 

			// fwrite(buffer,BufferSize,1,RawFile[0]);
			std::cout <<  Event[0][NumEvents[0]-1].Channel[0]->truncate << std::endl ;

			int sample ; 
			for (sample=0 ; sample < 2*Event[0]->Channel[0]->size ; sample++)
			{
				fprintf(*RawFile,"%*d\t",7,sample);
				fprintf(*RawFile,"%*u\n",12, *(Event[0]->Channel[0]->DataPtr + sample));
			}
		}
	}
	std::cout << "Count :" << count << std::endl;
	CAEN_DGTZ_SWStopAcquisition(Nhandle);
	CAEN_DGTZ_ReadRegister(Nhandle, 0x8104, &regvalue);
	std::cout << regvalue << std::endl; 
  CAEN_DGTZ_CloseDigitizer(Nhandle);
  return 0 ; 
}