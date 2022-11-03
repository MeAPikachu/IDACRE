#include <iostream>
#include <fstream>
/* iostream and fstream is to output the variables 
sudo apt install libcxxtools-dev , dev is short for developper*/

#include <csignal>
/*csingal library is to handle the basic signal functions 
void (*signal(int sig, void (*func)(int)))(int)
int sig includes SIGABRT, SIGFPE,SIGILL , SIGINT , SIGSEGV, SIGTERM and will execute the func when the sig changes*/

#include <unistd.h>
/*unistd.h is to handle the API between the redax and the POSIX (Linux) , also related to the commandline args */

#include <getopt.h>
/*getopt is to get the string from the command line */

#include <chrono>
#include <time.h>
/*Chrono Library is to log the time / date information just like the time module of Python */

#include <thread>
#include <atomic>
/* The thread library is to allow building multiple threads softwares . 
Atomic library defines a type called atomic so that we could handle it multithreading .*/

#include <mongocxx/collection.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#include <mongocxx/database.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/pool.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
//Bson is a useful file format mainly used by the mongodb . 

#include "../include/DAQController.hh"
#include "../include/MongoLog.hh"
#include "../include/Options.hh"
#include "../include/V1725.hh"
#include "../include/test.hh"


#ifndef IDACRE_BUILD_COMMIT
#define IDACRE_BUILD_COMMIT "V1.0"
#endif
// REDAX_Build_commit means that whether or not the build has been committed 
int PrintVersion() {
  std::cout << "Version: Idacre commit " << IDACRE_BUILD_COMMIT << "\n";
  return 0;
}

/*
void UpdateStatus(std::shared_ptr<mongocxx::pool> pool, std::string dbname,
    std::unique_ptr<DAQController>& controller) {
  using namespace std::chrono;
  //chrono is to process the time related functions .
  
  auto client = pool->acquire();
  auto db = (*client)[dbname];
  auto collection = db["status"];
  auto next_sec = ceil<seconds>(system_clock::now());
  
  //  pool is about the variable stored within computer's memory
  // "auto" is to detect the type of the variable automatically . 
  //  We will write the status data into the client[dbname]['status']
  
  const auto dt = seconds(1);
  std::this_thread::sleep_until(next_sec);

  
  while (b_run == true) {
    // I think it is important to undertand the b_run now...
    try{
      controller->StatusUpdate(&collection);
      // The function is within the controller.cc that to update the status stored in the db['status']
      // std::cout<<"Sucessfully update the status "<< std::endl; 
    }catch(const std::exception &e){
      std::cout<<"Can't connect to DB to update."<<std::endl;
      std::cout<<e.what()<<std::endl;
      
      //Exceptions is to record the exception (errors ) that is defined via the std::exception originate from the try  . 
      //If we could not update the status via the Redax , we shall output the errors . 
    }
    next_sec += dt;
    std::this_thread::sleep_until(next_sec);
  }
  std::cout<<"Status update returning\n";
  // When the b_run is False , we shall also return some feedback . 
}
// UpdatesStatus functiond is to write the status into the database once per second .. 
*/


int PrintUsage() {
  std::cout<< "\n" 
    << "PrintUsage : Welcome to redax\n"
    << "Accepted command-line arguments:\n"
    << "--id <id number>: id number of this readout instance, required\n"
    << "--uri <mongo uri>: full MongoDB URI, required\n"
    << "--db <database name>: name of the database to use, default \"daq\"\n"
    << "--logdir <directory>: where to write the logs, default pwd\n"
    << "--reader: this instance is a reader\n"
    << "--cc: this instance is a crate controller\n"
    << "--log-retention <value>: how many days to keep logfiles, default 7, 0 = forever\n"
    << "--help: print this message\n"
    << "--version: print version information and return\n"
    << "Notice : real host name : host+__reader/cc__+id   "
    << "\n";
  return 1;
}
// Printusage is to output the basic user manual of the sofware . 

std::atomic_bool b_run = true;
// Define an atomic variable called the b_run that indicates the program is running 
std::string hostname = "";
// hostname is set to null by default 

void SignalHandler(int signum) {
    std::cout << "\n SingalHandler"  << "\nReceived signal "<<signum<<std::endl;
    b_run = false;
    return;
}


int main(int argc , char** argv )
{
    std::cout << std::endl ;
    std::cout << "======IDACRE Version 1.0======" << std::endl; 

    mongocxx::instance instance{};

    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    /*
    SIGINT: signal interrupt 
    SIGTERM: signal terminate 
    When the program is interrupted or terminated , it will make sense ( b_run == false)
    */

    std::string current_run_id="none", log_dir = "";
    std::string dbname = "daq", suri = "", sid = "";
    bool reader = false, cc = false;
    int log_retention = 7; // days, 0 = someone else's problem
    int c=0, opt_index;

    enum {arg_id, arg_uri, arg_db, arg_logdir, arg_reader, arg_cc, arg_retention, arg_help, arg_version };
    struct option longopts[] = {
    {"id", required_argument, 0, arg_id},
    {"uri", required_argument, 0, arg_uri},
    {"db", required_argument, 0, arg_db},
    {"logdir", required_argument, 0, arg_logdir},
    {"reader", no_argument, 0, arg_reader},
    {"cc", no_argument, 0, arg_cc},
    {"log-retention", required_argument, 0, arg_retention},
    {"help", no_argument, 0, arg_help},
    {"version", no_argument, 0, arg_version},
    {0, 0, 0, 0}
    };
  

    while ((c = getopt_long(argc, argv, "", longopts, &opt_index)) != -1) {
    switch(c) {
      case arg_id:
        sid = optarg; break;
      case arg_uri:
        suri = optarg; break;
      case arg_db:
        dbname = optarg; break;
      // dbname is set to daq by default 
      case arg_logdir:
        log_dir = optarg; break;
      case arg_reader:
        reader = true; break;
      // If we have --reader command line , then the reader will be set true 
      case arg_cc:
        cc = true; break;
      // If we have --cc command line  , then the cc will be set true 
      // We could not set both the cc and reader true . 
      case arg_retention:
        log_retention = std::stoi(optarg); break;
      // for the time being , I do not know the log_retention meaning 
      case arg_help:
        return PrintUsage();
      case arg_version:
        return PrintVersion();
      default:
        std::cout<<"Received unknown arg\n";
        return PrintUsage();
    }
    }
    std::cout << "Log retention day : " << log_retention << std::endl;     


    // check the basic information 
    if (suri == "" || sid == "") return PrintUsage();
    // We need to specify the uri and id at the same time so that it could run . 
    if (reader == cc) {
    std::cout<<"Specify --reader XOR --cc\n";
    return 1;}
    // By default , the reader and cc are all set to false . 


    // We will consider commands addressed to this PC's ID 
    const int HOST_NAME_MAX = 64; // should be #defined in unistd.h but isn't???
    char chostname[HOST_NAME_MAX];
    gethostname(chostname, HOST_NAME_MAX);
    hostname=chostname;
    hostname+= (reader ? "_reader_" : "_controller_") + sid;
    PrintVersion();
    std::cout<<"Server starting with ID: "<<hostname<<std::endl;
    // The detailed hostname includes the hostname+_reader_+sid , not just the host name like the "RelicsDAQ"

    // MongoDB Connectivity for control database. Bonus for later:
    // exception wrap the URI parsing and client connection steps
    mongocxx::uri uri(suri.c_str());
    auto pool = std::make_shared<mongocxx::pool>(uri);
    // pool is a shared variable to log the uri of the instance , and we could read it via the pool.acquire()
    auto client = pool->acquire();
    mongocxx::database db = (*client)[dbname];
    mongocxx::collection control = db["control"];
    mongocxx::collection opts_collection = db["options"];

    // fLog pointer 
    std::shared_ptr<MongoLog> fLog;
    fLog = std::make_shared<MongoLog>(log_retention, pool, dbname, log_dir, hostname); 
    if (fLog->Initialize()) {
    std::cout<<"Could not initialize logs!\n";
    exit(-1);
    }

    //Options
    std::cout << "Create object fOptions \n" ; 
    std::shared_ptr<Options> fOptions;


    // Initialize the controller 
    // For the time being , we do not use the DAQ Controller 
    std::unique_ptr<DAQController> controller;
    controller = std::make_unique<DAQController>(fLog, hostname);
    // First type of thread : Mongo Status update thread 
    //std::thread status_update(&UpdateStatus, pool, dbname, std::ref(controller));


    using namespace bsoncxx::builder::stream;
    auto opts = mongocxx::options::find_one_and_update{}; // {} could be used to initialization . 
    opts.sort(document{} << "_id" << 1 << finalize);
    std::string ack_host = "acknowledged." + hostname;
    // Sort oldest to newest 
    auto query = document{} << "host" << hostname << ack_host << 0 << finalize;
    auto update = document{} << "$currentDate" << open_document <<
    ack_host << true << close_document << finalize;
    std::cout << "ack_host:" << ack_host << std::endl ; 
    // The acknowledged host and the host only differs in the "acknowledged."

    using namespace std::chrono; 
    // Here we just try to fetch a single command file 
    auto ack_time = system_clock::now();


  while(b_run == true)
  {
    try
    {
      auto qdoc = control.find_one_and_update(query.view(), update.view(), opts);
      // The find_one_and_update includes the query and the updates from the opts (option collection)
      if (qdoc) 
      {
        std::cout << "Find a command file  " << std::endl ;  
	      //If we found a doc from the database and then get the command out of the doc
        auto doc = qdoc->view();
	      std::string command = "";
	      std::string user = "";
	      try
        {
	        command = (doc)["command"].get_utf8().value.to_string();
	        user = (doc)["user"].get_utf8().value.to_string();
	      }
	      catch (const std::exception &e)
        {
         std::cout<< "Received malformed command" <<bsoncxx::to_json(doc).c_str() << std::endl ;  
	       fLog->Entry(MongoLog::Warning, "Received malformed command %s",
			   bsoncxx::to_json(doc).c_str());
         // If something went wrong , we shall also log it . 
	      }
	      fLog->Entry(MongoLog::Debug, "Found a doc with command %s", command.c_str());
        std::cout<< "Found a doc with command" << command.c_str() << std::endl ; 
        auto ack_time = system_clock::now();
        // It seems that we have already got the file .
        // Process commands
        
	      if(command == "start")
        {
        // std::cout << "controller status" << controller->status() << std::endl ; 
	          if(controller->Start()!=0)
            {
	           continue;
	          }
            auto now = system_clock::now();
            fLog->Entry(MongoLog::Local, "Ack to start took %i us",
                duration_cast<microseconds>(now-ack_time).count());
	      }
        // Basic functions 
        
        else if(command == "stop")
        {
        // std::cout << "controller status" << controller->status() << std::endl ; 
	      // "stop" is also a general reset command and can be called any time
	       if(controller->Stop()!=0)
	       fLog->Entry(MongoLog::Error,
			   "DAQ failed to stop. Will continue clearing program memory.");
          auto now = system_clock::now();
          fLog->Entry(MongoLog::Local, "Ack to stop took %i us",
              duration_cast<microseconds>(now-ack_time).count());
          fLog->SetRunId(-1);
          fOptions.reset();
	      } 
        
        
        else if(command == "arm")
        {
         std::string override_json = "";
         // Can only arm if we're idle
           //To arm the instruments , we have to stop the controllers first . 
           std::cout <<"We are going to stop the controller. " << std::endl ; 
	         controller->Stop();
           std::cout << "We have finished stopping the controller. " << std::endl ; 
           // Get an override doc from the 'options_override' field if it exists
	         override_json = "";
	         
          // Mongocxx types confusing so passing json strings around
          std::string mode = doc["mode"].get_utf8().value.to_string();
          fLog->Entry(MongoLog::Local, "Getting options doc for mode %s", mode.c_str());
          
          fLog->Entry(MongoLog::Local, "Ready to set up the foptions pointers" );
          fOptions = std::make_shared<Options>(fLog, mode, hostname, &opts_collection,
			      pool, dbname, override_json);
            //This command does not work well ... 
            //But if we successfully debug it , everything will be ok . 
          fLog->Entry(MongoLog::Local, "Successfully set up the fOptions pointer");
          std::cout << "Successfully set up the fOptions pointer" << std::endl ; 
          
          int dt = duration_cast<milliseconds>(system_clock::now()-ack_time).count();
          fLog->SetRunId(fOptions->GetInt("number", -1));
          fLog->Entry(MongoLog::Local, "Took %i ms to load config", dt);
          std::cout << "Took  " << dt << "ms to load config" << std::endl ; 


	        if(controller->Arm(fOptions) != 0){
           std::cout  << "failed to arm the controller" << std::endl ; 
	         fLog->Entry(MongoLog::Error, "Failed to initialize electronics");
	         controller->Stop();
	         }
          else
          {
            std::cout << "Initialized electronics" << std::endl ;
	        fLog->Entry(MongoLog::Debug, "Initialized electronics");
	        }
          std::cout << "Main Arm command finished" << std::endl ; 
          sleep(5);
	    }
     else if (command == "quit") b_run = false;
      } // if doc
    }
    catch(const std::exception &e)
    {
      std::cout<<e.what()<<std::endl;
      std::cout<<"Can't connect to DB so will continue what I'm doing"<<std::endl;
    }
    std::this_thread::sleep_for(milliseconds(100));
  }
  
  std::cout << "The last part of main.cc" << std::endl ; 
  fOptions.reset();
  fLog.reset();
  return 0 ;
}














