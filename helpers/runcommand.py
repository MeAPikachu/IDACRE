import os
from pymongo import MongoClient
import argparse
import datetime
import glob


# information we need :
#  --command : 
#  --number : to log the number of the run 
#  --mode : to log the set_run_mode 
#  --host: Which host set the command 
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--command', choices='check write arm stop start'.split(), required=True, help='The command')
    parser.add_argument('--number', type=int, default=0, help='Run number')
    parser.add_argument('--mode', help='Run mode', required=True)
    parser.add_argument('--host', nargs='+', default="RelicsDAQ_reader_0", help="Hosts to issue to")

    args = parser.parse_args()
    return args

def insert_control(coll, args):
    doc = {
        "command": args.command,
        "number": args.number,
        "options_override": {"number": args.number},
        "mode": args.mode,
        "host": args.host,
        "user": os.getlogin(),
        "run_identifier": '%06i' % args.number,
        "acknowledged": {h:0 for h in args.host},
        "createdAt": datetime.datetime.now()
    }
    coll.insert_one(doc)
    return


def check_repeated(path, index):
    existing_files = glob.glob(path + '/*')
    current_file_name = path + '/' + str(0) * (6 - len(str(index))) + str(index)
    while current_file_name in existing_files:
        index += 1
        current_file_name = path + '/' + str(0) * (6 - len(str(index))) + str(index)
    return index

if __name__ == '__main__':

    with MongoClient("mongodb://192.168.1.88:27017/admin") as client:
        args=main()
        path = client['daq']['options'].find_one({'name':args.mode})['strax_output_path']
        index = args.number
        new_index = check_repeated(path, index)
        args.number = new_index

        try:
            client['daq']['control'].delete_many({})
            insert_control(client['daq']['control'],args)
        except Exception as e:
            print('%s: %s' % (type(e), e))
