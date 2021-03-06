#include <iostream>

#include "tools.h"
#include "asiohandler.h"

int main(int argc, char *argv[])
{
  boost::asio::io_service ioService;
  AsioHandler handler(ioService);
  handler.connect("localhost", 5672);

  AMQP::Connection connection(&handler, AMQP::Login("guest", "guest"), "/");

  AMQP::Channel channel(&connection);

  const std::string correlation(uuid());
  AMQP::QueueCallback callback = [&](const std::string &name,
                                     int msgcount,
                                     int consumercount)
    {
      std::string command;
      if (argc > 1)
        {
          std::string arg1(argv[1]);
          command = arg1;
        }
      else
        {
          command = "t";
        }
      //int i = 30;
      //AMQP::Envelope env(std::to_string(i));
      AMQP::Envelope env(command);
      env.setCorrelationID(correlation);
      env.setReplyTo(name);
      channel.publish("","rpc_queue",env);
      //std::cout<<" [x] Requesting fib("<< i <<")";
      std::cout<<" [x] Running command: "<< command;
      std::cout<<" ... Correlation Id:"<< correlation << std::endl;
    };
  channel.declareQueue(AMQP::exclusive).onSuccess(callback);

  boost::asio::deadline_timer t(ioService, boost::posix_time::millisec(100));
  auto receiveCallback = [&](const AMQP::Message &message,
                             uint64_t deliveryTag,
                             bool redelivered)
    {
      if(message.correlationID() != correlation)
        return;

      std::cout<<" [.] Got "<<message.message()<<std::endl;
      t.async_wait([&](const boost::system::error_code&){ioService.stop();});
    };

  channel.consume("", AMQP::noack).onReceived(receiveCallback);

  ioService.run();
  return 0;
}
