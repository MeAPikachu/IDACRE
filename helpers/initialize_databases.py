from pymongo import MongoClient
import os




host='192.168.1.88'
port=27017
database='daq'


client=MongoClient(f'mongodb://{host}:{port}')
db = client[database]

# Collection list 
# 1-aggregate status 
# 2-board_map
# 3-cable_map
# 4-control
# 5-detector_control
# 6-log
# 7-options
# 8-status
# 9-system_monitor

#Basically , the control and options collections are the most important part of the collections . 

# aggregate status
db.create_collection('aggregate_status')
db.aggregate_status.create_index([('detector', 1), ('_id', -1)])
db.aggregate_status.create_index('number')




# board and cable maps
db.create_collection('board_map', validator={'$jsonSchema': {
    'bsonType': 'object',
    'required': ['board', 'host', 'model', 'detector'],
    'properties': {
        'opt_bd': {
            'bsonType': 'int',
            'description': 'Board position in optical link'
        },
        'slot': {
            'bsonType': 'int',
            'description': 'Board position in VME crate'
        },
        'host': {
            'bsonType': 'string',
            'description': 'Which reader the board is connected to'
        },
        'crate': {
            'bsonType': 'int',
            'description': 'Which VME crate the board lives in'
        },
        'detector': {
            'enum': ['tpc', 'muon_veto', 'neutron_veto'],
            'description': 'Which detector the board is part of'
        },
        'board': {
            'bsonType': 'int',
            'description': 'Board serial number'
        },
        'model': {
            'bsonType': 'string',
            'description': 'Board model'
        },
        'vme_address': {
            'bsonType': 'string',
            'description': 'Board VME address'
        },
        'link': {
            'bsonType': 'int',
            'description': 'Optical link number'
        }
    }}})
db.board_map.create_index('board')
db.board_map.create_index([('crate', 1), ('slot', 1)], unique=True)
db.board_map.create_index([('host', 1), ('link', 1), ('opt_bd', 1)], unique=True)




db.create_collection('cable_map', validator={'$jsonSchema': {
    'bsonType': 'object',
    'required': ['pmt', 'detector', 'adc', 'adc_channel', 'threshold'],
    'properties': {
        'pmt': {
            'bsonType': 'int',
            'description': 'Global PMT/channel number',
        },
        'adc': {
            'bsonType': 'int',
            'description': 'Serial number of the ADC this PMT is connected to'
        },
        'adc_channel': {
            'bsonType': 'int',
            'description': 'Which ADC channel the PMT is connected to'
        },
        'detector': {
            'enum': ['tpc', 'muon_veto', 'neutron_veto'],
            'description': 'Which detector this PMT is part of'
        },
        'threshold': {
            'bsonType': 'int',
            'description': 'This PMT\'s threshold'
        }
    }}})
db.cable_map.create_index('pmt', unique=True)
db.cable_map.create_index([('adc', 1), ('adc_channel', 1)], unique=True)





# Collection "Control" that is used to send the command of the users . 
db.create_collection('control', validator={'$jsonSchema': {
    'bsonType': 'object',
    'required': ['command', 'user', 'host', 'createdAt', 'acknowledged'],
    'properties': {
        'command': {
            'enum': ['start', 'stop', 'arm', 'quit'],
        },
        'user': {'bsonType': 'string'},
        'host': {'bsonType': ['string', 'array']},
        'acknowledged': {'bsonType': 'object'}
    }}})
db.control.create_index('createdAt', expireAfterSeconds=7*24*3600)
db.control.create_index([('host', 1), ('_id', 1)])




# detector control
db.create_collection('detector_control')
db.detector_control.create_index('key')




# log
db.create_collection('log')
db.log.create_index('runid')
db.log.create_index([('priority', 1), ('_id', -1)])





# options
db.create_collection('options', validator={'$jsonSchema': {
    'bsonType': 'object',
    'required': ['name', 'user', 'description', 'detector'],
    'properties': {
        'name': {'bsonType': 'string'},
        'user': {'bsonType': 'string'},
        'detector': {
            'anyOf': [
                {
                    'bsonType': 'string',
                    'enum': ['tpc', 'muon_veto', 'neutron_veto', 'include']
                },
                {
                    'bsonType': 'array',
                    'items': {
                        'enum': ['tpc', 'muon_veto', 'neutron_veto', 'include']
                    }
                }
        ]},
        'description': {'bsonType': 'string'}
    }}})
db.options.create_index('name')



# status
db.create_collection('status')
db.status.create_index('time', expireAfterSeconds=3*24*3600)
db.status.create_index([('host', 1), ('_id', -1)])



# system monitor
db.create_collection('system_monitor')
db.system_monitor.create_index('time', expireAfterSeconds=3*24*3600)
db.system_monitor.create_index([('host', 1), ('_id', -1)])


with open("/home/data/debug/daq_database_collections.txt","w") as f : 
    print("daq database Collections{}".format(db.list_collection_names()),file=f)

