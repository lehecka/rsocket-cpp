// Copyright 2004-present Facebook. All Rights Reserved.

#include "RSocketConnectionHandler.h"
#include <atomic>

#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventBaseManager.h>

#include "RSocketErrors.h"
#include "RSocketStats.h"
#include "src/statemachine/RSocketStateMachine.h"
#include "src/temporary_home/NullRequestHandler.h"
#include "src/temporary_home/OldNewBridge.h"

namespace rsocket {

using namespace rsocket;
using namespace yarpl;

class RSocketHandlerBridge : public rsocket::DefaultRequestHandler {
 public:
  RSocketHandlerBridge(std::shared_ptr<RSocketResponder> handler)
      : handler_(std::move(handler)){};

  void handleRequestStream(
      Payload request,
      StreamId streamId,
      const yarpl::Reference<yarpl::flowable::Subscriber<Payload>>&
          response) noexcept override {
    auto flowable =
        handler_->handleRequestStream(std::move(request), std::move(streamId));
    // bridge from the existing eager RequestHandler and old Subscriber type
    // to the lazy Flowable and new Subscriber type
    flowable->subscribe(std::move(response));
  }

  yarpl::Reference<yarpl::flowable::Subscriber<Payload>> handleRequestChannel(
      Payload request,
      StreamId streamId,
      const yarpl::Reference<yarpl::flowable::Subscriber<Payload>>&
          response) noexcept override {
    auto eagerSubscriber = make_ref<EagerSubscriberBridge>();
    auto flowable = handler_->handleRequestChannel(
        std::move(request),
        yarpl::flowable::Flowables::fromPublisher<Payload>([eagerSubscriber](
            yarpl::Reference<yarpl::flowable::Subscriber<Payload>> subscriber) {
          eagerSubscriber->subscribe(subscriber);
        }),
        std::move(streamId));
    // bridge from the existing eager RequestHandler and old Subscriber type
    // to the lazy Flowable and new Subscriber type
    flowable->subscribe(std::move(response));

    return eagerSubscriber;
  }

  void handleRequestResponse(
      Payload request,
      StreamId streamId,
      const yarpl::Reference<yarpl::flowable::Subscriber<Payload>>&
          responseSubscriber) noexcept override {
    auto single = handler_->handleRequestResponse(std::move(request), streamId);
    // bridge from the existing eager RequestHandler and old Subscriber type
    // to the lazy Single and new SingleObserver type

    class BridgeSubscriptionToSingle : public yarpl::flowable::Subscription {
     public:
      BridgeSubscriptionToSingle(
          yarpl::Reference<yarpl::single::Single<Payload>> single,
          yarpl::Reference<yarpl::flowable::Subscriber<Payload>>
              responseSubscriber)
          : single_{std::move(single)},
            responseSubscriber_{std::move(responseSubscriber)} {}

      void request(int64_t n) noexcept override {
        // when we get a request we subscribe to Single
        bool expected = false;
        if (n > 0 && subscribed_.compare_exchange_strong(expected, true)) {
          single_->subscribe(yarpl::single::SingleObservers::create<Payload>(
              [this](Payload p) {
                // onNext
                responseSubscriber_->onNext(std::move(p));
              },
              [this](std::exception_ptr eptr) {
                responseSubscriber_->onError(std::move(eptr));
              }));
        }
      }

      void cancel() noexcept override{
          // TODO this code will be deleted shortly, so not bothering to make
          // work
      };

     private:
      yarpl::Reference<yarpl::single::Single<Payload>> single_;
      yarpl::Reference<yarpl::flowable::Subscriber<Payload>>
          responseSubscriber_;
      std::atomic_bool subscribed_{false};
    };

    responseSubscriber->onSubscribe(make_ref<BridgeSubscriptionToSingle>(
        std::move(single), responseSubscriber));
  }

  void handleFireAndForgetRequest(
      Payload request,
      StreamId streamId) noexcept override {
    handler_->handleFireAndForget(std::move(request), std::move(streamId));
  }

 private:
  std::shared_ptr<RSocketResponder> handler_;
};

void RSocketConnectionHandler::setupNewSocket(
    std::shared_ptr<FrameTransport> frameTransport,
    SetupParameters setupPayload) {
  LOG(INFO) << "RSocketServer => received new setup payload";

  // FIXME(alexanderm): Handler should be tied to specific executor
  auto executor = folly::EventBaseManager::get()->getExistingEventBase();

  auto socketParams =
      RSocketParameters(setupPayload.resumable, setupPayload.protocolVersion);
  std::shared_ptr<ConnectionSetupRequest> setupRequest =
      std::make_shared<ConnectionSetupRequest>(std::move(setupPayload));
  std::shared_ptr<RSocketResponder> requestHandler;
  try {
    requestHandler = getHandler(setupRequest);
  } catch (const RSocketError& e) {
    // TODO emit ERROR ... but how do I do that here?
    frameTransport->close(
        folly::exception_wrapper{std::current_exception(), e});
    return;
  }
  LOG(INFO) << "RSocketServer => received request handler";

  auto handlerBridge =
      std::make_shared<RSocketHandlerBridge>(std::move(requestHandler));

  auto rs = std::make_shared<RSocketStateMachine>(
      *executor,
      std::move(handlerBridge),
      RSocketStats::noop(),
      nullptr,
      ReactiveSocketMode::SERVER);

  manageSocket(setupRequest, rs);

  // TODO ---> this code needs to be moved inside RSocketStateMachine

  // Connect last, after all state has been set up.
  rs->setResumable(socketParams.resumable);

  if (socketParams.protocolVersion != ProtocolVersion::Unknown) {
    rs->setFrameSerializer(
        FrameSerializer::createFrameSerializer(socketParams.protocolVersion));
  }

  rs->connect(std::move(frameTransport), true, socketParams.protocolVersion);

  // TODO <---- up to here
  // TODO and then a simple API such as:
  // TODO rs->connectServer(frameTransport, params)
}

bool RSocketConnectionHandler::resumeSocket(
    std::shared_ptr<FrameTransport> frameTransport,
    ResumeParameters) {
  return false;
}

void RSocketConnectionHandler::connectionError(
    std::shared_ptr<FrameTransport>,
    folly::exception_wrapper ex) {
  LOG(WARNING) << "Connection failed before first frame: " << ex.what();
}

} // namespace rsocket
