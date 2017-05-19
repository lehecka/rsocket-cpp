// Copyright 2004-present Facebook. All Rights Reserved.

#include "src/statemachine/ChannelRequester.h"
#include <folly/ExceptionString.h>

namespace rsocket {

using namespace yarpl;
using namespace yarpl::flowable;

void ChannelRequester::onSubscribe(
    Reference<Subscription> subscription) noexcept {
  CHECK(State::NEW == state_);
  if (ConsumerBase::isTerminated()) {
    subscription->cancel();
    return;
  }
  publisherSubscribe(subscription);
  // Request the first payload immediately.
  subscription->request(1);
}

void ChannelRequester::onNext(Payload request) noexcept {
  switch (state_) {
    case State::NEW: {
      state_ = State::REQUESTED;
      // FIXME: find a root cause of this asymmetry; the problem here is that
      // the ConsumerBase::request might be delivered after the whole thing is
      // shut down, if one uses InlineConnection.
      size_t initialN = initialResponseAllowance_.drainWithLimit(
          Frame_REQUEST_N::kMaxRequestN);
      size_t remainingN = initialResponseAllowance_.drain();
      // Send as much as possible with the initial request.
      CHECK_GE(Frame_REQUEST_N::kMaxRequestN, initialN);
      newStream(
          StreamType::CHANNEL,
          static_cast<uint32_t>(initialN),
          std::move(request),
          false);
      // We must inform ConsumerBase about an implicit allowance we have
      // requested from the remote end.
      ConsumerBase::addImplicitAllowance(initialN);
      // Pump the remaining allowance into the ConsumerBase _after_ sending the
      // initial request.
      if (remainingN) {
        ConsumerBase::generateRequest(remainingN);
      }
    } break;
    case State::REQUESTED: {
      debugCheckOnNextOnError();
      writePayload(std::move(request), 0);
      break;
    }
    case State::CLOSED:
      break;
  }
}

// TODO: consolidate code in onCompleteImpl, onErrorImpl, cancelImpl
void ChannelRequester::onComplete() noexcept {
  switch (state_) {
    case State::NEW:
      state_ = State::CLOSED;
      closeStream(StreamCompletionSignal::COMPLETE);
      break;
    case State::REQUESTED: {
      state_ = State::CLOSED;
      completeStream();
    } break;
    case State::CLOSED:
      break;
  }
}

void ChannelRequester::onError(const std::exception_ptr ex) noexcept {
  switch (state_) {
    case State::NEW:
      state_ = State::CLOSED;
      closeStream(StreamCompletionSignal::APPLICATION_ERROR);
      break;
    case State::REQUESTED: {
      applicationError(folly::exceptionStr(ex).toStdString());
    } break;
    case State::CLOSED:
      break;
  }
}

void ChannelRequester::request(int64_t n) noexcept {
  switch (state_) {
    case State::NEW:
      // The initial request has not been sent out yet, hence we must accumulate
      // the unsynchronised allowance, portion of which will be sent out with
      // the initial request frame, and the rest will be dispatched via
      // ConsumerBase:request (ultimately by sending REQUEST_N frames).
      initialResponseAllowance_.release(n);
      break;
    case State::REQUESTED:
      ConsumerBase::generateRequest(n);
      break;
    case State::CLOSED:
      break;
  }
}

void ChannelRequester::cancel() noexcept {
  switch (state_) {
    case State::NEW:
      state_ = State::CLOSED;
      closeStream(StreamCompletionSignal::CANCEL);
      break;
    case State::REQUESTED: {
      state_ = State::CLOSED;
      cancelStream();
    } break;
    case State::CLOSED:
      break;
  }
}

void ChannelRequester::endStream(StreamCompletionSignal signal) {
  switch (state_) {
    case State::NEW:
    case State::REQUESTED:
      // Spontaneous ::endStream signal messagesns an error.
      DCHECK(StreamCompletionSignal::COMPLETE != signal);
      DCHECK(StreamCompletionSignal::CANCEL != signal);
      state_ = State::CLOSED;
      break;
    case State::CLOSED:
      break;
  }
  terminatePublisher(signal);
  ConsumerBase::endStream(signal);
}

void ChannelRequester::handlePayload(
    Payload&& payload,
    bool complete,
    bool flagsNext) {
  bool end = false;
  switch (state_) {
    case State::NEW:
      // Cannot receive a frame before sending the initial request.
      CHECK(false);
      break;
    case State::REQUESTED:
      if (complete) {
        state_ = State::CLOSED;
        end = true;
      }
      break;
    case State::CLOSED:
      break;
  }

  processPayload(std::move(payload), flagsNext);

  if (end) {
    closeStream(StreamCompletionSignal::COMPLETE);
  }
}

void ChannelRequester::handleError(folly::exception_wrapper errorPayload) {
  switch (state_) {
    case State::NEW:
      // Cannot receive a frame before sending the initial request.
      CHECK(false);
      break;
    case State::REQUESTED:
      state_ = State::CLOSED;
      ConsumerBase::onError(errorPayload);
      closeStream(StreamCompletionSignal::ERROR);
      break;
    case State::CLOSED:
      break;
  }
}

void ChannelRequester::handleRequestN(uint32_t n) {
  PublisherBase::processRequestN(n);
}

} // reactivesocket
