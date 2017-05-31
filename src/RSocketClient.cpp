// Copyright 2004-present Facebook. All Rights Reserved.

#include "RSocketClient.h"
#include "RSocketRequester.h"
#include "RSocketResponder.h"
#include "RSocketStats.h"
#include "src/internal/FollyKeepaliveTimer.h"
#include "src/framing/FrameTransport.h"

using namespace folly;

namespace rsocket {

RSocketClient::RSocketClient(
    std::unique_ptr<ConnectionFactory> connectionFactory)
    : connectionFactory_(std::move(connectionFactory)) {
  VLOG(1) << "Constructing RSocketClient";
}

folly::Future<std::shared_ptr<RSocketRequester>> RSocketClient::connect() {
  VLOG(2) << "Starting connection";

  auto promise =
      std::make_shared<folly::Promise<std::shared_ptr<RSocketRequester>>>();
  auto future = promise->getFuture();

  connectionFactory_->connect([ this, promise = std::move(promise) ](
      std::unique_ptr<DuplexConnection> connection,
      folly::EventBase & eventBase) mutable {
      VLOG(3) << "onConnect received DuplexConnection";
      auto rsocket =
          fromConnection(std::make_pair(std::move(connection), &eventBase));
      // store it so it lives as long as the RSocketClient
      rsockets_.push_back(rsocket);
      promise->setValue(rsocket);
  });

  return future;
}

std::shared_ptr<RSocketRequester> RSocketClient::fromConnection(
    std::pair<std::unique_ptr<DuplexConnection>, folly::EventBase*> connection) {
  auto* eventBase = std::get<1>(connection);
  auto duplexConnection = std::move(std::get<0>(connection));
  auto rs = std::make_shared<RSocketStateMachine>(
      *eventBase,
      // TODO: need to allow Responder being passed in optionally
      std::make_shared<RSocketResponder>(),
      // TODO: need to allow stats being passed in
      RSocketStats::noop(),
      // TODO need to optionally allow defining the keepalive timer
      std::make_unique<FollyKeepaliveTimer>(
          *eventBase, std::chrono::milliseconds(5000)),
      ReactiveSocketMode::CLIENT);

  // TODO: need to allow this being passed in
  auto setupParameters =
      SetupParameters("text/plain", "text/plain", Payload("meta", "data"));
  rs->connectClientSendSetup(
      std::move(duplexConnection), std::move(setupParameters));

  return RSocketRequester::create(std::move(rs), *eventBase);
}

RSocketClient::~RSocketClient() {
  VLOG(1) << "Destroying RSocketClient";
}
}
