import pymongo
from pymongo import MongoClient
import os
import time


# This python script keep reading the status of the detector 
client = MongoClient("mongodb://192.168.1.88:27017/admin")
db = client['daq']
collection = db['status']
STATUS = ["Idle", "Arming", "Armed", "Running", "Error"]


while 1:
    docs= collection.find({}).sort("_id", -1).limit(1)
    for doc in docs:
        print("%s: Client %s reports status: %s rate: %.2f: %i"%(doc['_id'].generation_time, doc['host'], STATUS[doc['status']], doc['rate'],doc['buffer_size']))
    
    print("Time:{}".format(time.time()))
    time.sleep(1)