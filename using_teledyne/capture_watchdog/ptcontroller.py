import os
import sys
import json
import pika
import time
import logger
import argparse

logger = logger.logger(tofile=True)
parser = argparse.ArgumentParser(description='WatchDog -- sidekick to the cuic capture'+ \
                                 '(currently only supports teledyne cameras)')
parser.add_argument('--loc', metavar='L', type=str, nargs='+',
                    dest='location', required=True,
                    help='Location where the camera is running. Queue name will be generated from this')

args = parser.parse_args()

RPC_VHOST = "/vis"
known_queues = {
    "1mtcNorth": {"cam": "1mtcNorth", "watchdog": "1mtcNorth_watchdog", "pt": "1mtcNorth_pt", "lens": "1mtcNorth_lens"},
    "1mtcSouth": {"cam": "1mtcSouth", "watchdog": "1mtcSouth_watchdog", "pt": "1mtcSouth_pt", "lens": "1mtcSouth_lens"},
    "370Roof"  : {"cam": "370Roof", "watchdog": "370Roof_watchdog"},
    "test"     : {"cam": "test", "watchdog": "test_watchdog"},
}
RPC_QUEUE_NAME = known_queues[args.location[0]]["pt"]
RPC_SERVER = os.getenv("rpc_server")
RPC_PORT = os.getenv("rpc_port")
RPC_USER = os.getenv("rpc_user")
RPC_PASS = os.getenv("rpc_pass")
pt = None

try:
    # relative import for pyflirpt
    current_path = os.path.split(os.path.abspath(os.path.realpath(sys.argv[0])))[0]
    sys.path.append(os.path.abspath(os.path.join(current_path, os.path.pardir)))
    from pyflirpt.keyboard import keyboard
    pt = keyboard.KeyboardController(pt_ip=os.getenv("north_pt_ip").encode(), pt_port=os.getenv("north_pt_port").encode())
except Exception as ex:
    logger.critical("Cannot import certain modules "+str(ex))

def status():
    stat = pt.current_pos()
    return {"pan": stat[0],
            "tilt": stat[1]}

def pan_tilt(pval, tval):
    logger.info("Got: Pan: "+str(pval)+" Tilt: "+str(tval))
    try:
        if pval != -999999:
            pt.pan(pval)
        if tval != -999999:
            pt.tilt(tval)
    except Exception as ex:
        logger.warning("Error in panning or tilting: "+str(ex))
    finally:
        while not pt.ready():
            time.sleep(1)
        new_pos = status()
        logger.info("New location: "+str(new_pos))
        return new_pos

def on_rpc_request(ch, method, props, body):
    """
    Blocking Function for handling the incoming data
    Refer "http://pika.readthedocs.io/en/0.11.2/modules/adapters/blocking.html"
    """
    try:
        response = {}
        command_dict = json.loads(body.decode("utf-8"))
        if command_dict.get('status', None):
            response.update(status())
        else:
            logger.info("Command received is for panning and tilting")
            response.update(pan_tilt(command_dict.get("pan", -999999), command_dict.get("tilt", -999999)))
        ch.basic_publish(exchange='',
                         routing_key=props.reply_to,
                         properties=pika.BasicProperties(correlation_id=props.correlation_id),
                         body=json.dumps(response))
        ch.basic_ack(delivery_tag=method.delivery_tag)
    except Exception as ex:
        logger.error("Error in on_rpc_request: "+str(ex))

if __name__ == "__main__":
    credentials = pika.PlainCredentials(os.getenv("rpc_user"), os.getenv("rpc_pass"))
    connection = pika.BlockingConnection(
        pika.ConnectionParameters(host=os.getenv("rpc_server"), port=os.getenv("rpc_port"),
                                  virtual_host=RPC_VHOST, credentials=credentials, heartbeat=0))
    channel = connection.channel()
    channel.queue_declare(queue=RPC_QUEUE_NAME)

    try:
        channel.basic_qos(prefetch_count=1)
        channel.basic_consume(on_rpc_request, queue=RPC_QUEUE_NAME)
        logger.info("Listening for RPC messages")
        channel.start_consuming()
    except KeyboardInterrupt as ki:
        logger.info("Exiting now")
        #pt.cleanup()
        sys.exit(1)
    except Exception as ex:
        logger.critical("Critical Exception in main: "+str(ex))

                    
