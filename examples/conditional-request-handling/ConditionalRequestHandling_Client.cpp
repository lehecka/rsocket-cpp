// Copyright 2004-present Facebook. All Rights Reserved.

#include <folly/init/Init.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/portability/GFlags.h>
#include <iostream>

#include "examples/util/ExampleSubscriber.h"
#include "src/RSocket.h"
#include "src/transports/tcp/TcpConnectionFactory.h"
#include "yarpl/Flowable.h"

using namespace ::folly;
using namespace ::rsocket_example;
using namespace ::rsocket;
using namespace yarpl::flowable;

DEFINE_string(host, "localhost", "host to connect to");
DEFINE_int32(port, 9898, "host:port to connect to");

int main(int argc, char* argv[]) {
  FLAGS_logtostderr = true;
  FLAGS_minloglevel = 0;
  folly::init(&argc, &argv);

  folly::SocketAddress address;
  address.setFromHostPort(FLAGS_host, FLAGS_port);

  auto rsf = RSocket::createClient(
      std::make_unique<TcpConnectionFactory>(std::move(address)));
  auto rs = rsf->connect().get();

  rs->requestStream(Payload("Bob"))->take(5)->subscribe([](Payload p) {
    std::cout << "Received: " << p.moveDataToString() << std::endl;
  });

  // Wait for a newline on the console to terminate the server.
  std::getchar();
  return 0;
}
