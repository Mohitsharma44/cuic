import pika
from pika.exceptions import ConnectionClosed
import time
import uuid
import sys
import os

class UOControllerRpcClient(object):
    def __init__(self, vhost, queue_name):
        self.queue_name = queue_name
        credentials = pika.PlainCredentials(os.getenv("rpc_user"), os.getenv("rpc_pass"))
        self.connection = pika.BlockingConnection(pika.ConnectionParameters(os.getenv("rpc_server"),
                                                                            os.getenv("rpc_port"),
                                                                            vhost, credentials))
        # wait 10 seconds before timing out
        self.connection.add_timeout(10, self.connection_timeout)
        self.channel = self.connection.channel()
        # Only one server can get the message
        result = self.channel.queue_declare(exclusive=True)
        self.callback_queue = result.method.queue
        self.channel.basic_consume(self.on_response, no_ack=True,
                                   queue=self.callback_queue)

    def on_response(self, ch, method, props, body):
        if self.corr_id == props.correlation_id:
            self.response = body

    def _cleanup(self):
        """
        Called when connection is closed 
        """
        print("Purging all messages from the queue")
        self.channel.queue_purge(queue=self.queue_name)
            
    def connection_timeout(self):
        """
        If the connection timesout, close the connection
        Returns
        -------
        True: If connection close was successful else raises Exception
        """
        try:
            print("Could not establish connection")
            #self.channel.queue_delete(queue='rpc_queue')
            self._cleanup()
            self.connection.close()
            return True
        except ConnectionClosed as ex:
            pass
    
    def call(self, command):
        self.response = None
        self.corr_id = str(uuid.uuid4())
        print('Correlation Id: ', self.corr_id)
        try:
            if self.connection.is_open:
                self.channel.basic_publish(exchange='',
                                           routing_key=self.queue_name,
                                           properties=pika.BasicProperties(
                                               reply_to=self.callback_queue,
                                               correlation_id=self.corr_id,
                                               content_type='application/json'),
                                           body=str(command))
                while self.response is None:
                    self.connection.process_data_events()
                #return int(self.response)
                return str(self.response)
            else:
                return None
        except ConnectionClosed as cc:
            print("Connection is not Open!")
            return False


