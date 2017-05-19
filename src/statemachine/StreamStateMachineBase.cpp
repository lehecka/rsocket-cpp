// Copyright 2004-present Facebook. All Rights Reserved.

#include "src/statemachine/StreamStateMachineBase.h"
#include <folly/io/IOBuf.h>
#include "RSocketStateMachine.h"
#include "StreamsHandler.h"

namespace rsocket {

void StreamStateMachineBase::handlePayload(
    Payload&& payload,
    bool complete,
    bool flagsNext) {
  VLOG(4) << "Unexpected handlePayload";
}

void StreamStateMachineBase::handleRequestN(uint32_t n) {
  VLOG(4) << "Unexpected handleRequestN";
}

void StreamStateMachineBase::handleError(
    folly::exception_wrapper errorPayload) {
  VLOG(4) << "Unexpected handleError";
}

void StreamStateMachineBase::handleCancel() {
  VLOG(4) << "Unexpected handleCancel";
}

void StreamStateMachineBase::endStream(StreamCompletionSignal) {
  isTerminated_ = true;
}

void StreamStateMachineBase::newStream(
    StreamType streamType,
    uint32_t initialRequestN,
    Payload payload,
    bool completed) {
  writer_->writeNewStream(
      streamId_, streamType, initialRequestN, std::move(payload), completed);
}

void StreamStateMachineBase::writePayload(Payload&& payload, bool complete) {
  writer_->writePayload(streamId_, std::move(payload), complete);
}

void StreamStateMachineBase::writeRequestN(uint32_t n) {
  writer_->writeRequestN(streamId_, n);
}

void StreamStateMachineBase::applicationError(std::string errorPayload) {
  // TODO: a bad frame for a stream should not bring down the whole socket
  // https://github.com/ReactiveSocket/reactivesocket-cpp/issues/311
  writer_->writeCloseStream(
      streamId_,
      StreamCompletionSignal::APPLICATION_ERROR,
      Payload(std::move(errorPayload)));
  closeStream(StreamCompletionSignal::APPLICATION_ERROR);
}

void StreamStateMachineBase::errorStream(std::string errorPayload) {
  writer_->writeCloseStream(
      streamId_,
      StreamCompletionSignal::ERROR,
      Payload(std::move(errorPayload)));
  closeStream(StreamCompletionSignal::ERROR);
}

void StreamStateMachineBase::cancelStream() {
  writer_->writeCloseStream(
      streamId_, StreamCompletionSignal::CANCEL, Payload());
  closeStream(StreamCompletionSignal::CANCEL);
}

void StreamStateMachineBase::completeStream() {
  writer_->writeCloseStream(
      streamId_, StreamCompletionSignal::COMPLETE, Payload());
  closeStream(StreamCompletionSignal::COMPLETE);
}

void StreamStateMachineBase::closeStream(StreamCompletionSignal signal) {
  writer_->onStreamClosed(streamId_, signal);
  // TODO: set writer_ to nullptr
}
} // reactivesocket
