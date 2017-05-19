// Copyright 2004-present Facebook. All Rights Reserved.

#include "src/statemachine/StreamResponder.h"
#include <folly/ExceptionString.h>

namespace rsocket {

using namespace yarpl;
using namespace yarpl::flowable;

void StreamResponder::onSubscribe(
    Reference<yarpl::flowable::Subscription> subscription) noexcept {
  if (StreamStateMachineBase::isTerminated()) {
    subscription->cancel();
    return;
  }
  publisherSubscribe(std::move(subscription));
}

void StreamResponder::onNext(Payload response) noexcept {
  debugCheckOnNextOnError();
  switch (state_) {
    case State::RESPONDING: {
      writePayload(std::move(response), false);
      break;
    }
    case State::CLOSED:
      break;
  }
}

void StreamResponder::onComplete() noexcept {
  switch (state_) {
    case State::RESPONDING: {
      state_ = State::CLOSED;
      completeStream();
    } break;
    case State::CLOSED:
      break;
  }
}

void StreamResponder::onError(const std::exception_ptr ex) noexcept {
  debugCheckOnNextOnError();
  switch (state_) {
    case State::RESPONDING: {
      state_ = State::CLOSED;
      applicationError(folly::exceptionStr(ex).toStdString());
    } break;
    case State::CLOSED:
      break;
  }
}

void StreamResponder::pauseStream(RequestHandler& requestHandler) {
  pausePublisherStream(requestHandler);
}

void StreamResponder::resumeStream(RequestHandler& requestHandler) {
  resumePublisherStream(requestHandler);
}

void StreamResponder::endStream(StreamCompletionSignal signal) {
  switch (state_) {
    case State::RESPONDING:
      // Spontaneous ::endStream signal means an error.
      DCHECK(StreamCompletionSignal::COMPLETE != signal);
      DCHECK(StreamCompletionSignal::CANCEL != signal);
      state_ = State::CLOSED;
      break;
    case State::CLOSED:
      break;
  }
  terminatePublisher(signal);
  StreamStateMachineBase::endStream(signal);
}

void StreamResponder::handleCancel() {
  switch (state_) {
    case State::RESPONDING:
      state_ = State::CLOSED;
      closeStream(StreamCompletionSignal::CANCEL);
      break;
    case State::CLOSED:
      break;
  }
}

void StreamResponder::handleRequestN(uint32_t n) {
  PublisherBase::processRequestN(n);
}
}
