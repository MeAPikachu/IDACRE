import pymongo
from pymongo import MongoClient
from bson.objectid import ObjectId
import os


# the run_mode data is stored at the options collection . 
client = MongoClient("mongodb://192.168.1.88:27017")
db = client['daq']
collection = db['options']

def time_to_hex(t):
	# nanosecond
	samples = t/4
	factored = int(samples/4)
	return hex(factored)

run_mode = {
	# the run_mode name must be unique 
    "name": "V1725_default_firmware_settings",
	"number": 0 , 
    "user": "root",
    "description": "V1725_number_0",
    "detector" : "tpc",
	"detectors": {"detector" : "tpc",
				  "hostname" : "RelicsDAQ_reader_0",
				  "RelicsDAQ_reader_0": "tpc",
				  "RelicsDAQ": "tpc" , 
				  "active" : "true",
				  "stop_after" : "600"}, # second
	"mongo_uri": "mongodb://192.168.1.88:27017/admin",
	"mongo_database": "daq",
	"run_start":0,
	"processing_threads" : 30 , 
	"blt_safety_factor" : 1.5 ,
	"blt_alloc" : 23 , 
	"do_sn_check" : 0 , 
	
	"strax_header_size": 31,
	"strax_chunk_length": 5000000000,
	"strax_chunk_overlap": 500000000,
	"strax_fragment_payload_bytes":220 ,
	"strax_buffer_num_chunks": 2 ,
	"strax_chunk_phase_limit" : 2 ,
	"strax_output_path": "/home/data/tpc",
	"strax_fragment_length": 220,
	"compressor" : "lz4" , 
	
	"transfer_batch" : 8 , 
	"us_between_reads" : 10 , 

	
	"baseline_max_steps": 3,
	"baseline_convergence_threshold": 3 ,
	"baseline_start_dac" : 10000 , 
	"baseline_dac_mode": "fixed",
	"baseline_fallback_mode": "fit" , 
	"baseline_fixed_value" : 8192, 
	"baseline_value": 8192,
	"baseline_reference_run": -1 ,
	"baseline_reference_run.tpc": -1 , 
	"baseline_triggers_per_step": 3 , 
	"baseline_ms_between_triggers" : 10 ,
	"baseline_adjustment_threshold" : 10 ,
	"baseline_min_adjustment" : 0xC , 
	"baseline_rebin_log2" :1  ,
	"baseline_bins_around_max" : 3,
	"baseline_value" : 0 , 
	"baseline_min_dac" : 0 ,
	"baseline_max_dac" : 1<<16 ,
	"baseline_convergence_threshold" : 3 ,
	"baseline_fraction_around_max" : 0.8 , 
	"baseline_adc_to_dac" :   -3.0 , 
	
	"firmware_version": 4.22,
    "boards":
    [
        {"crate": 0, "link": 0, "board": 0,
            "vme_address": "0", "type": "V1730", "host": "RelicsDAQ_reader_0"},
    ],
    "registers" : [
			{
					"comment" : "board reset register",
					"board" : 0,
					"reg" : "EF24",
					"val" : "0"
			},
			{
				"comment" : "Channel 0 Trigger Threshold",
				"board" : 0,
				"reg" : "8028",
				"val" : "c8"
			},
			{
				"comment" : "Channel 0 Fixed Baseline",
				"board" : 0,
				"reg" : "1064",
				"val" : "2000"
			},
			{
				"comment": "Channel enable mask. FF= all channels on for 8 channels ADC , FFFF for 16 channels ADC ",
				"board": 0,
				"reg": "8120",
				"val": "0xFFFF"
			},
			{
				"comment": "Channel enable mask. FF= all channels on for 8 channels ADC , FFFF for 16 channels ADC ",
				"board": 0,
				"reg": "8120",
				"val": "0xFFFF"
			},
			{
				"comment": "Channel enable mask. FF= all channels on for 8 channels ADC , FFFF for 16 channels ADC ",
				"board": 0,
				"reg": "8120",
				"val": "0xFFFF"
			},				
	],
	
	"thresholds":{"0":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]},
    "channels":{"0":[0, 1, 2, 3, 4, 5, 6,7,8,9,10,11,12,13,14,15]},
}

if collection.find_one({"name": run_mode['name']}) is not None:
    print("Please provide a unique name!")

try:
    collection.insert_one(run_mode)
except Exception as e:
    print("Insert failed. Maybe your JSON is bad. Error follows:")
    print(e)


# we need to make sure some convention is satisfied
detector_from_path = run_mode['strax_output_path'].split('/')
detector_from_path=[i for i in detector_from_path if i !='']

if detector_from_path[0]!='home':
	print('Not conventional path. Should be /home/data/<detector>')
	exit(0)

if run_mode['detector'] not in detector_from_path:
	print('Inconsistent save path and detector name. They should be the same')
	exit(0)



print("New runmode has been added as a document of collection 'options'")
