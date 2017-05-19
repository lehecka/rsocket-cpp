// Copyright 2004-present Facebook. All Rights Reserved.

#include "RSocketServer.h"
#include <folly/ExceptionWrapper.h>
#include "RSocketConnectionHandler.h"
#include "src/statemachine/RSocketStateMachine.h"
#include "src/internal/ScheduledRSocketResponder.h"

using namespace rsocket;

namespace rsocket {

class RSocketServerConnectionHandler : public virtual RSocketConnectionHandler {
 public:
  RSocketServerConnectionHandler(
      RSocketServer* server,
      OnAccept onAccept,
      folly::EventBase& eventBase)
      : server_{server}, onAccept_{std::move(onAccept)}, eventBase_{eventBase} {}

  std::shared_ptr<RSocketResponder> getHandler(
      std::shared_ptr<ConnectionSetupRequest> request) override {
    auto responder = onAccept_(std::move(request));
    if(responder) {
      return std::make_shared<ScheduledRSocketResponder>(std::move(responder), eventBase_);
    }
    return nullptr;
  }

  void manageSocket(
      std::shared_ptr<ConnectionSetupRequest> request,
      std::shared_ptr<rsocket::RSocketStateMachine> stateMachine) override {
    stateMachine->addClosedListener(
        [this, stateMachine](const folly::exception_wrapper&) {
          // Enqueue another event to remove and delete it.  We cannot delete
          // the RSocketStateMachine now as it still needs to finish processing
          // the onClosed handlers in the stack frame above us.
            // TODO => Ben commented out to get unit tests working
          //          executor_.add([this, stateMachine] {
          //            server_->removeConnection(stateMachine);
          //          });

        });

    server_->addConnection(std::move(stateMachine), eventBase_);
  }

 private:
  RSocketServer* server_;
  OnAccept onAccept_;
  folly::EventBase& eventBase_;
};

RSocketServer::RSocketServer(
    std::unique_ptr<ConnectionAcceptor> connectionAcceptor)
    : lazyAcceptor_(std::move(connectionAcceptor)),
      acceptor_(ProtocolVersion::Unknown) {}

RSocketServer::~RSocketServer() {
  // Stop accepting new connections.
  lazyAcceptor_->stop();

  // FIXME(alexanderm): This is where we /should/ close the FrameTransports
  // sitting around in the ServerConnectionAcceptor, but we can't yet...

  // Asynchronously close all existing ReactiveSockets.  If there are none, then
  // we can do an early exit.
  {
    auto locked = sockets_.lock();
    if (locked->empty()) {
      return;
    }

    shutdown_.emplace();

    for (auto& connectionPair : *locked) {
      // close() has to be called on the same executor as the socket
      auto& executor_ = connectionPair.second;
      executor_.add([s = connectionPair.first] {
        s->close(
            folly::exception_wrapper(), StreamCompletionSignal::SOCKET_CLOSED);
      });
    }
  }

    // TODO => Ben commented out to get unit tests working

  // Wait for all ReactiveSockets to close.
  //  shutdown_->wait();
  //  DCHECK(sockets_.lock()->empty());

  // All requests are fully finished, worker threads can be safely killed off.
}

void RSocketServer::start(OnAccept onAccept) {
  if (started) {
    throw std::runtime_error("RSocketServer::start() already called.");
  }
  started = true;

  LOG(INFO) << "Initializing connection acceptor on start";

  lazyAcceptor_
      ->start([ this, onAccept = std::move(onAccept) ](
          std::unique_ptr<DuplexConnection> conn, folly::EventBase& eventBase) {
        LOG(INFO) << "Going to accept duplex connection";

        auto connectionHandler_ =
            std::make_shared<RSocketServerConnectionHandler>(
                this, onAccept, eventBase);

        // FIXME(alexanderm): This isn't thread safe
        acceptor_.accept(std::move(conn), std::move(connectionHandler_));
      })
      .get(); // block until finished and return or throw
}

void RSocketServer::startAndPark(OnAccept onAccept) {
  start(std::move(onAccept));
  waiting_.wait();
}

void RSocketServer::unpark() {
  waiting_.post();
}

void RSocketServer::addConnection(
    std::shared_ptr<rsocket::RSocketStateMachine> socket,
    folly::Executor& executor) {
  sockets_.lock()->insert({std::move(socket), executor});
}

void RSocketServer::removeConnection(
    std::shared_ptr<rsocket::RSocketStateMachine> socket) {
  auto locked = sockets_.lock();
  locked->erase(socket);

  LOG(INFO) << "Removed ReactiveSocket";

  if (shutdown_ && locked->empty()) {
    shutdown_->post();
  }
}
} // namespace rsocket
