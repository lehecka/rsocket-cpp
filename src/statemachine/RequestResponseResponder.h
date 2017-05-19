// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "src/statemachine/PublisherBase.h"
#include "src/statemachine/StreamStateMachineBase.h"
#include "yarpl/flowable/Subscriber.h"

namespace rsocket {

/// Implementation of stream stateMachine that represents a RequestResponse
/// responder
class RequestResponseResponder : public StreamStateMachineBase,
                                 public PublisherBase,
                                 public yarpl::flowable::Subscriber<Payload> {
 public:
  explicit RequestResponseResponder(const Parameters& params)
      : StreamStateMachineBase(params), PublisherBase(1) {}

 private:
  void onSubscribe(yarpl::Reference<yarpl::flowable::Subscription>
                       subscription) noexcept override;
  void onNext(Payload) noexcept override;
  void onComplete() noexcept override;
  void onError(const std::exception_ptr) noexcept override;

  void handleCancel() override;
  void handleRequestN(uint32_t n) override;

  void pauseStream(RequestHandler&) override;
  void resumeStream(RequestHandler&) override;
  void endStream(StreamCompletionSignal) override;

  /// State of the Subscription responder.
  enum class State : uint8_t {
    RESPONDING,
    CLOSED,
  } state_{State::RESPONDING};
};

} // reactivesocket
