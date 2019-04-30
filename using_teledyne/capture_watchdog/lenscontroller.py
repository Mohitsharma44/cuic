import os
import sys
import json
import pika
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
RPC_QUEUE_NAME = known_queues[args.location[0]]["lens"]
RPC_SERVER = os.getenv("rpc_server")
RPC_PORT = os.getenv("rpc_port")
RPC_USER = os.getenv("rpc_user")
RPC_PASS = os.getenv("rpc_pass")
birger = None

try:
    # relative import for pybirger
    current_path = os.path.split(os.path.abspath(os.path.realpath(sys.argv[0])))[0]
    sys.path.append(os.path.abspath(os.path.join(current_path, os.path.pardir)))
    from pybirger.pybirger import api
    birger = api.Birger(os.getenv("birger_uds_host"), os.getenv("birger_uds_port"))
except Exception as ex:
    logger.critical("Cannot import certain modules "+str(ex))

def set_focus(val):
    logger.info("Setting focus to: {}".format(val))
    birger.set_focus(val)
    return {"focus": birger.get_focus().decode('ascii')}

def set_aperture(val):
    logger.info("Setting aperture to: {}".format(val))
    birger.set_aperture(val)
    return {"aperture": birger.get_aperture().decode('ascii')}

def status():
    focus = birger.get_focus().decode('ascii')
    aperture = birger.get_aperture().decode('ascii')
    serial = birger.sn().decode('ascii')
    return {"sn": serial,
            "focus": focus,
            "aperture": aperture}

def on_rpc_request(ch, method, props, body):
    """
    Blocking Function for handling the incoming data
    Refer "http://pika.readthedocs.io/en/0.11.2/modules/adapters/blocking.html"
    """
    try:
        response = {}
        command_dict = json.loads(body.decode("utf-8"))
        logger.info("Got {}".format(command_dict))
        if command_dict.get('status', None):
            response.update(status())
        else:
            if command_dict.get('focus', -99) != -99:
                response.update(set_focus(command_dict['focus']))
            if command_dict.get('aperture', -99) != -99:
                response.update(set_aperture(command_dict['aperture']))
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
        pika.ConnectionParameters(os.getenv("rpc_server"), os.getenv("rpc_port"),
                                  RPC_VHOST, credentials))
    channel = connection.channel()
    channel.queue_declare(queue=RPC_QUEUE_NAME)

    try:
        channel.basic_qos(prefetch_count=1)
        channel.basic_consume(on_rpc_request, queue=RPC_QUEUE_NAME)
        logger.info("Listening for RPC messages")
        channel.start_consuming()
    except KeyboardInterrupt as ki:
        logger.info("Exiting now")
        #birger.cleanup()
        sys.exit(1)
    except Exception as ex:
        logger.critical("Critical Exception in main: "+str(ex))
