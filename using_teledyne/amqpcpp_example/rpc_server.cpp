#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include "json.hpp"
#include "asiohandler.h"
using json = nlohmann::json;

std::string json_parser(std::string jdata)
{
  auto j2 = json::parse(jdata);
  std::cout << "Parsing this data: " << std::endl;
  std::cout << "Aperture: " << j2["aperture"] << std::endl;
  std::cout << "Capture: " << j2["capture"] << std::endl;
  std::cout << "Focus: " << j2["focus"] << std::endl;
  std::cout << "Interval: " << j2["interval"] << std::endl;
  std::cout << "Location: " << j2["location"] << std::endl;
  std::cout << j2 << std::endl;
  return j2.dump();
}

int main(void)
{
    boost::asio::io_service ioService;
    AsioHandler handler(ioService);
    handler.connect("localhost", 5672);

    AMQP::Connection connection(&handler, AMQP::Login("guest", "guest"), "/");

    AMQP::Channel channel(&connection);
    channel.setQos(1);

    channel.declareQueue("rpc_queue");
    channel.consume("").onReceived([&channel](const AMQP::Message &message,
            uint64_t deliveryTag,
            bool redelivered)
    {
        const auto body = message.message();
        std::cout<<" ... CorId"<<message.correlationID()<<std::endl;

        AMQP::Envelope env(json_parser(std::string(body)));
        env.setCorrelationID(message.correlationID());

        channel.publish("", message.replyTo(), env);
        channel.ack(deliveryTag);
    });

    std::cout << " [x] Awaiting RPC requests" << std::endl;

    ioService.run();
    return 0;
}
