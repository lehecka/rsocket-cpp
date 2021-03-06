// Copyright 2004-present Facebook. All Rights Reserved.

#include "src/ConnectionAutomaton.h"

#include <folly/ExceptionWrapper.h>
#include <folly/MoveWrapper.h>
#include <folly/Optional.h>
#include <folly/String.h>
#include <folly/io/async/EventBase.h>
#include "src/ClientResumeStatusCallback.h"
#include "src/ConnectionSetupPayload.h"
#include "src/DuplexConnection.h"
#include "src/FrameTransport.h"
#include "src/RequestHandler.h"
#include "src/ResumeCache.h"
#include "src/Stats.h"
#include "src/StreamState.h"
#include "src/automata/ChannelResponder.h"
#include "src/automata/StreamAutomatonBase.h"

namespace reactivesocket {

ConnectionAutomaton::ConnectionAutomaton(
    folly::Executor& executor,
    ReactiveSocket* reactiveSocket,
    std::shared_ptr<RequestHandler> requestHandler,
    std::shared_ptr<Stats> stats,
    std::unique_ptr<KeepaliveTimer> keepaliveTimer,
    ReactiveSocketMode mode)
    : ExecutorBase(executor),
      reactiveSocket_(reactiveSocket),
      stats_(stats),
      mode_(mode),
      resumeCache_(std::make_shared<ResumeCache>(stats)),
      streamState_(std::make_shared<StreamState>(*stats)),
      requestHandler_(std::move(requestHandler)),
      keepaliveTimer_(std::move(keepaliveTimer)),
      streamsFactory_(*this, mode) {
  // We deliberately do not "open" input or output to avoid having c'tor on the
  // stack when processing any signals from the connection. See ::connect and
  // ::onSubscribe.
  CHECK(streamState_);
}

ConnectionAutomaton::~ConnectionAutomaton() {
  // this destructor can be called from a different thread because the stream
  // automatons destroyed on different threads can be the last ones referencing
  // this.

  VLOG(6) << "~ConnectionAutomaton";
  // We rely on SubscriptionPtr and SubscriberPtr to dispatch appropriate
  // terminal signals.
  DCHECK(!resumeCallback_);
  DCHECK(isDisconnectedOrClosed()); // the instance should be closed by via
  // close method
}

void ConnectionAutomaton::setResumable(bool resumable) {
  debugCheckCorrectExecutor();
  DCHECK(isDisconnectedOrClosed()); // we allow to set this flag before we are
  // connected
  remoteResumeable_ = isResumable_ = resumable;
}

bool ConnectionAutomaton::connect(
    std::shared_ptr<FrameTransport> frameTransport,
    bool sendingPendingFrames,
    ProtocolVersion protocolVersion) {
  debugCheckCorrectExecutor();
  CHECK(isDisconnectedOrClosed());
  CHECK(frameTransport);
  CHECK(!frameTransport->isClosed());
  if (protocolVersion != ProtocolVersion::Unknown) {
    if (frameSerializer_) {
      if (frameSerializer_->protocolVersion() != protocolVersion) {
        DCHECK(false);
        frameTransport->close(std::runtime_error("protocol version mismatch"));
        return false;
      }
    } else {
      frameSerializer_ =
          FrameSerializer::createFrameSerializer(protocolVersion);
      if (!frameSerializer_) {
        DCHECK(false);
        frameTransport->close(std::runtime_error("invaid protocol version"));
        return false;
      }
    }
  }

  frameTransport_ = std::move(frameTransport);

  for (auto& callback : onConnectListeners_) {
    callback();
  }

  requestHandler_->socketOnConnected();

  // We need to create a hard reference to frameTransport_ to make sure the
  // instance survives until the setFrameProcessor returns.  There can be
  // terminating signals processed in that call which will nullify
  // frameTransport_.
  auto frameTransportCopy = frameTransport_;

  // Keep a reference to this, as processing frames might close the
  // ReactiveSocket instance.
  auto copyThis = shared_from_this();
  frameTransport_->setFrameProcessor(copyThis);

  if (sendingPendingFrames) {
    DCHECK(!resumeCallback_);
    // we are free to try to send frames again
    // not all frames might be sent if the connection breaks, the rest of them
    // will queue up again
    auto outputFrames = streamState_->moveOutputPendingFrames();
    for (auto& frame : outputFrames) {
      outputFrameOrEnqueue(std::move(frame));
    }

    // TODO: turn on only after setup frame was received
    if (keepaliveTimer_) {
      keepaliveTimer_->start(shared_from_this());
    }
  }

  return true;
}

std::shared_ptr<FrameTransport> ConnectionAutomaton::detachFrameTransport() {
  debugCheckCorrectExecutor();
  if (isDisconnectedOrClosed()) {
    return nullptr;
  }

  frameTransport_->setFrameProcessor(nullptr);
  return std::move(frameTransport_);
}

void ConnectionAutomaton::disconnect(folly::exception_wrapper ex) {
  debugCheckCorrectExecutor();
  VLOG(6) << "disconnect";
  if (isDisconnectedOrClosed()) {
    return;
  }

  for (auto& callback : onDisconnectListeners_) {
    callback(ex);
  }

  requestHandler_->socketOnDisconnected(ex);

  closeFrameTransport(std::move(ex), StreamCompletionSignal::CONNECTION_END);
  pauseStreams();
  stats_->socketDisconnected();
}

void ConnectionAutomaton::close(
    folly::exception_wrapper ex,
    StreamCompletionSignal signal) {
  debugCheckCorrectExecutor();

  if (isClosed_) {
    return;
  }
  isClosed_ = true;
  reactiveSocket_ = nullptr;
  stats_->socketClosed(signal);

  VLOG(6) << "close";

  if (resumeCallback_) {
    resumeCallback_->onResumeError(
        std::runtime_error(ex ? ex.what().c_str() : "RS closing"));
    resumeCallback_.reset();
  }

  onConnectListeners_.clear();
  onDisconnectListeners_.clear();
  auto onCloseListeners = std::move(onCloseListeners_);
  for (auto& callback : onCloseListeners) {
    callback(ex);
  }

  requestHandler_->socketOnClosed(ex);

  closeStreams(signal);
  closeFrameTransport(std::move(ex), signal);
}

void ConnectionAutomaton::closeFrameTransport(
    folly::exception_wrapper ex,
    StreamCompletionSignal signal) {
  if (isDisconnectedOrClosed()) {
    DCHECK(!resumeCallback_);
    return;
  }

  // Stop scheduling keepalives since the socket is now disconnected
  if (keepaliveTimer_) {
    keepaliveTimer_->stop();
  }

  if (resumeCallback_) {
    resumeCallback_->onConnectionError(
        std::runtime_error(ex ? ex.what().c_str() : "connection closing"));
    resumeCallback_.reset();
  }

  // echo the exception to the frameTransport only if the frameTransport started
  // closing with error
  // otherwise we sent some error frame over the wire and we are closing
  // transport cleanly
  frameTransport_->close(
      signal == StreamCompletionSignal::CONNECTION_ERROR
          ? std::move(ex)
          : folly::exception_wrapper());
  frameTransport_ = nullptr;
}

void ConnectionAutomaton::disconnectOrCloseWithError(Frame_ERROR&& errorFrame) {
  debugCheckCorrectExecutor();
  if (isResumable_) {
    disconnect(std::runtime_error(errorFrame.payload_.data->cloneAsValue()
                                      .moveToFbString()
                                      .toStdString()));
  } else {
    closeWithError(std::move(errorFrame));
  }
}

void ConnectionAutomaton::closeWithError(Frame_ERROR&& error) {
  debugCheckCorrectExecutor();
  VLOG(3) << "closeWithError "
          << error.payload_.data->cloneAsValue().moveToFbString();

  StreamCompletionSignal signal;
  switch (error.errorCode_) {
    case ErrorCode::INVALID_SETUP:
      signal = StreamCompletionSignal::INVALID_SETUP;
      break;
    case ErrorCode::UNSUPPORTED_SETUP:
      signal = StreamCompletionSignal::UNSUPPORTED_SETUP;
      break;
    case ErrorCode::REJECTED_SETUP:
      signal = StreamCompletionSignal::REJECTED_SETUP;
      break;

    case ErrorCode::CONNECTION_ERROR:
    // StreamCompletionSignal::CONNECTION_ERROR is reserved for
    // frameTransport errors
    // ErrorCode::CONNECTION_ERROR is a normal Frame_ERROR error code which has
    // nothing to do with frameTransport
    case ErrorCode::APPLICATION_ERROR:
    case ErrorCode::REJECTED:
    case ErrorCode::RESERVED:
    case ErrorCode::CANCELED:
    case ErrorCode::INVALID:
    default:
      signal = StreamCompletionSignal::ERROR;
  }

  auto exception = std::runtime_error(
      error.payload_.data->cloneAsValue().moveToFbString().toStdString());

  if (frameSerializer_) {
    outputFrameOrEnqueue(frameSerializer_->serializeOut(std::move(error)));
  }
  close(std::move(exception), signal);
}

void ConnectionAutomaton::reconnect(
    std::shared_ptr<FrameTransport> newFrameTransport,
    std::unique_ptr<ClientResumeStatusCallback> resumeCallback) {
  debugCheckCorrectExecutor();
  CHECK(newFrameTransport);
  CHECK(resumeCallback);
  CHECK(!resumeCallback_);
  CHECK(isResumable_);
  CHECK(mode_ == ReactiveSocketMode::CLIENT);

  // TODO: output frame buffer should not be written to the new connection until
  // we receive resume ok
  resumeCallback_ = std::move(resumeCallback);
  connect(std::move(newFrameTransport), false, ProtocolVersion::Unknown);
}

void ConnectionAutomaton::addStream(
    StreamId streamId,
    yarpl::Reference<StreamAutomatonBase> automaton) {
  debugCheckCorrectExecutor();
  auto result = streamState_->streams_.emplace(streamId, std::move(automaton));
  (void)result;
  assert(result.second);
}

void ConnectionAutomaton::endStream(
    StreamId streamId,
    StreamCompletionSignal signal) {
  debugCheckCorrectExecutor();
  VLOG(6) << "endStream";
  // The signal must be idempotent.
  if (!endStreamInternal(streamId, signal)) {
    return;
  }
  DCHECK(
      signal == StreamCompletionSignal::CANCEL ||
      signal == StreamCompletionSignal::COMPLETE ||
      signal == StreamCompletionSignal::APPLICATION_ERROR ||
      signal == StreamCompletionSignal::ERROR);
}

bool ConnectionAutomaton::endStreamInternal(
    StreamId streamId,
    StreamCompletionSignal signal) {
  VLOG(6) << "endStreamInternal";
  auto it = streamState_->streams_.find(streamId);
  if (it == streamState_->streams_.end()) {
    // Unsubscribe handshake initiated by the connection, we're done.
    return false;
  }
  // Remove from the map before notifying the automaton.
  auto automaton = std::move(it->second);
  streamState_->streams_.erase(it);
  automaton->endStream(signal);
  return true;
}

void ConnectionAutomaton::closeStreams(StreamCompletionSignal signal) {
  // Close all streams.
  while (!streamState_->streams_.empty()) {
    auto oldSize = streamState_->streams_.size();
    auto result =
        endStreamInternal(streamState_->streams_.begin()->first, signal);
    (void)oldSize;
    (void)result;
    // TODO(stupaq): what kind of a user action could violate these
    // assertions?
    assert(result);
    assert(streamState_->streams_.size() == oldSize - 1);
  }
}

void ConnectionAutomaton::pauseStreams() {
  for (auto& streamKV : streamState_->streams_) {
    streamKV.second->pauseStream(*requestHandler_);
  }
}

void ConnectionAutomaton::resumeStreams() {
  for (auto& streamKV : streamState_->streams_) {
    streamKV.second->resumeStream(*requestHandler_);
  }
}

void ConnectionAutomaton::processFrame(std::unique_ptr<folly::IOBuf> frame) {
  auto thisPtr = this->shared_from_this();
  runInExecutor([ thisPtr, frame = std::move(frame) ]() mutable {
    thisPtr->processFrameImpl(std::move(frame));
  });
}

void ConnectionAutomaton::processFrameImpl(
    std::unique_ptr<folly::IOBuf> frame) {
  if (isClosed()) {
    return;
  }

  if (!ensureOrAutodetectFrameSerializer(*frame)) {
    DLOG(FATAL) << "frame serializer is not set";
    // Failed to autodetect protocol version
    closeWithError(Frame_ERROR::invalidFrame());
    return;
  }

  auto frameType = frameSerializer_->peekFrameType(*frame);
  stats_->frameRead(frameType);

  // TODO(tmont): If a frame is invalid, it will still be tracked. However, we
  // actually want that. We want to keep
  // each side in sync, even if a frame is invalid.
  resumeCache_->trackReceivedFrame(*frame, frameType);

  auto streamIdPtr = frameSerializer_->peekStreamId(*frame);
  if (!streamIdPtr) {
    // Failed to deserialize the frame.
    closeWithError(Frame_ERROR::invalidFrame());
    return;
  }
  auto streamId = *streamIdPtr;
  if (streamId == 0) {
    handleConnectionFrame(frameType, std::move(frame));
    return;
  }

  // during the time when we are resuming we are can't receive any other
  // than connection level frames which drives the resumption
  // TODO(lehecka): this assertion should be handled more elegantly using
  // different state machine
  if (resumeCallback_) {
    LOG(ERROR) << "received stream frames during resumption";
    closeWithError(Frame_ERROR::invalidFrame());
    return;
  }

  handleStreamFrame(streamId, frameType, std::move(frame));
}

void ConnectionAutomaton::onTerminal(folly::exception_wrapper ex) {
  auto thisPtr = this->shared_from_this();
  auto movedEx = folly::makeMoveWrapper(ex);
  runInExecutor([thisPtr, movedEx]() mutable {
    thisPtr->onTerminalImpl(movedEx.move());
  });
}

void ConnectionAutomaton::onTerminalImpl(folly::exception_wrapper ex) {
  if (isResumable_) {
    disconnect(std::move(ex));
  } else {
    auto termSignal = ex ? StreamCompletionSignal::CONNECTION_ERROR
                         : StreamCompletionSignal::CONNECTION_END;
    close(std::move(ex), termSignal);
  }
}

void ConnectionAutomaton::handleConnectionFrame(
    FrameType frameType,
    std::unique_ptr<folly::IOBuf> payload) {
  switch (frameType) {
    case FrameType::KEEPALIVE: {
      Frame_KEEPALIVE frame;
      if (!deserializeFrameOrError(
              remoteResumeable_, frame, std::move(payload))) {
        return;
      }

      resumeCache_->resetUpToPosition(frame.position_);
      if (mode_ == ReactiveSocketMode::SERVER) {
        if (!!(frame.header_.flags_ & FrameFlags::KEEPALIVE_RESPOND)) {
          sendKeepalive(FrameFlags::EMPTY, std::move(frame.data_));
        } else {
          closeWithError(
              Frame_ERROR::connectionError("keepalive without flag"));
        }
      } else {
        if (!!(frame.header_.flags_ & FrameFlags::KEEPALIVE_RESPOND)) {
          closeWithError(Frame_ERROR::connectionError(
              "client received keepalive with respond flag"));
        } else if (keepaliveTimer_) {
          keepaliveTimer_->keepaliveReceived();
        }
      }
      return;
    }
    case FrameType::SETUP: {
      Frame_SETUP frame;
      if (!deserializeFrameOrError(frame, std::move(payload))) {
        return;
      }
      if (!!(frame.header_.flags_ & FrameFlags::RESUME_ENABLE)) {
        remoteResumeable_ = true;
      } else {
        remoteResumeable_ = false;
      }
      if (!!(frame.header_.flags_ & FrameFlags::LEASE)) {
        // TODO(yschimke) We don't have the correct lease and wait logic above
        // yet
        LOG(ERROR) << "ignoring setup frame with lease";
      }

      ConnectionSetupPayload setupPayload;
      frame.moveToSetupPayload(setupPayload);

      // this should be already set to the correct version
      if (frameSerializer_->protocolVersion() != setupPayload.protocolVersion) {
        closeWithError(Frame_ERROR::badSetupFrame("invalid protocol version"));
        return;
      }

      requestHandler_->handleSetupPayload(
          *reactiveSocket_, std::move(setupPayload));
      return;
    }
    case FrameType::METADATA_PUSH: {
      Frame_METADATA_PUSH frame;
      if (deserializeFrameOrError(frame, std::move(payload))) {
        requestHandler_->handleMetadataPush(std::move(frame.metadata_));
      }
      return;
    }
    case FrameType::RESUME: {
      if (mode_ == ReactiveSocketMode::SERVER && isResumable_) {
        Frame_RESUME frame;
        if (!deserializeFrameOrError(frame, std::move(payload))) {
          return;
        }
        auto resumed = requestHandler_->handleResume(
            *reactiveSocket_,
            ResumeParameters(
                frame.token_,
                frame.lastReceivedServerPosition_,
                frame.clientPosition_,
                ProtocolVersion(frame.versionMajor_, frame.versionMinor_)));
        if (!resumed) {
          closeWithError(Frame_ERROR::connectionError("can not resume"));
        }
      } else {
        closeWithError(
            Frame_ERROR::connectionError("RS not resumable. Can not resume"));
      }
      return;
    }
    case FrameType::RESUME_OK: {
      Frame_RESUME_OK frame;
      if (!deserializeFrameOrError(frame, std::move(payload))) {
        return;
      }
      if (resumeCallback_) {
        if (resumeCache_->isPositionAvailable(frame.position_)) {
          resumeCallback_->onResumeOk();
          resumeCallback_.reset();
          resumeFromPosition(frame.position_);
        } else {
          closeWithError(Frame_ERROR::connectionError(folly::to<std::string>(
              "Client cannot resume, server position ",
              frame.position_,
              " is not available.")));
        }
      } else {
        closeWithError(Frame_ERROR::invalidFrame());
      }
      return;
    }
    case FrameType::ERROR: {
      Frame_ERROR frame;
      if (!deserializeFrameOrError(frame, std::move(payload))) {
        return;
      }

      // TODO: handle INVALID_SETUP, UNSUPPORTED_SETUP, REJECTED_SETUP

      if (frame.errorCode_ == ErrorCode::CONNECTION_ERROR && resumeCallback_) {
        resumeCallback_->onResumeError(
            std::runtime_error(frame.payload_.moveDataToString()));
        resumeCallback_.reset();
        // fall through
      }

      close(
          std::runtime_error(frame.payload_.moveDataToString()),
          StreamCompletionSignal::ERROR);
      return;
    }
    case FrameType::RESERVED:
    case FrameType::LEASE:
    case FrameType::REQUEST_RESPONSE:
    case FrameType::REQUEST_FNF:
    case FrameType::REQUEST_STREAM:
    case FrameType::REQUEST_CHANNEL:
    case FrameType::REQUEST_N:
    case FrameType::CANCEL:
    case FrameType::PAYLOAD:
    case FrameType::EXT:
    default:
      closeWithError(Frame_ERROR::unexpectedFrame());
      return;
  }
}

void ConnectionAutomaton::handleStreamFrame(
    StreamId streamId,
    FrameType frameType,
    std::unique_ptr<folly::IOBuf> serializedFrame) {
  auto it = streamState_->streams_.find(streamId);
  if (it == streamState_->streams_.end()) {
    handleUnknownStream(streamId, frameType, std::move(serializedFrame));
    return;
  }
  auto &automaton = it->second;

  switch (frameType) {
    case FrameType::REQUEST_N: {
      Frame_REQUEST_N frameRequestN;
      if (!deserializeFrameOrError(frameRequestN,
                                   std::move(serializedFrame))) {
        return;
      }
      automaton->handleRequestN(frameRequestN.requestN_);
      break;
    }
    case FrameType::CANCEL: {
      automaton->handleCancel();
      break;
    }
    case FrameType::PAYLOAD: {
      Frame_PAYLOAD framePayload;
      if (!deserializeFrameOrError(framePayload,
                                   std::move(serializedFrame))) {
        return;
      }
      automaton->handlePayload(std::move(framePayload.payload_),
                               framePayload.header_.flagsComplete(),
                               framePayload.header_.flagsNext());
      break;
    }
    case FrameType::ERROR: {
      Frame_ERROR frameError;
      if (!deserializeFrameOrError(frameError,
                                   std::move(serializedFrame))) {
        return;
      }
      automaton->handleError(
          std::runtime_error(frameError.payload_.moveDataToString()));
      break;
    }
    case FrameType::REQUEST_CHANNEL:
    case FrameType::REQUEST_RESPONSE:
    case FrameType::RESERVED:
    case FrameType::SETUP:
    case FrameType::LEASE:
    case FrameType::KEEPALIVE:
    case FrameType::REQUEST_FNF:
    case FrameType::REQUEST_STREAM:
    case FrameType::METADATA_PUSH:
    case FrameType::RESUME:
    case FrameType::RESUME_OK:
    case FrameType::EXT:
      closeWithError(Frame_ERROR::unexpectedFrame());
      break;
    default:
      // because of compatibility with future frame types we will just ignore
      // unknown frames
      break;
  }
}

void ConnectionAutomaton::handleUnknownStream(
    StreamId streamId,
    FrameType frameType,
    std::unique_ptr<folly::IOBuf> serializedFrame) {
  DCHECK(streamId != 0);
  // TODO: comparing string versions is odd because from version
  // 10.0 the lexicographic comparison doesn't work
  // we should change the version to struct
  if (frameSerializer_->protocolVersion() > ProtocolVersion{0, 0} &&
      !streamsFactory_.registerNewPeerStreamId(streamId)) {
    return;
  }

  switch (frameType) {
    case FrameType::REQUEST_CHANNEL: {
      Frame_REQUEST_CHANNEL frame;
      if (!deserializeFrameOrError(frame, std::move(serializedFrame))) {
        return;
      }
      auto automaton = streamsFactory_.createChannelResponder(
          frame.requestN_, streamId);
      auto requestSink = requestHandler_->handleRequestChannel(
          std::move(frame.payload_), streamId, automaton);
      automaton->subscribe(requestSink);
      break;
    }
    case FrameType::REQUEST_STREAM: {
      Frame_REQUEST_STREAM frame;
      if (!deserializeFrameOrError(frame, std::move(serializedFrame))) {
        return;
      }
      auto automaton = streamsFactory_.createStreamResponder(
          frame.requestN_, streamId);
      requestHandler_->handleRequestStream(
          std::move(frame.payload_), streamId, automaton);
      break;
    }
    case FrameType::REQUEST_RESPONSE: {
      Frame_REQUEST_RESPONSE frame;
      if (!deserializeFrameOrError(frame, std::move(serializedFrame))) {
        return;
      }
      auto automaton =
          streamsFactory_.createRequestResponseResponder(streamId);
      requestHandler_->handleRequestResponse(
          std::move(frame.payload_), streamId, automaton);
      break;
    }
    case FrameType::REQUEST_FNF: {
      Frame_REQUEST_FNF frame;
      if (!deserializeFrameOrError(frame, std::move(serializedFrame))) {
        return;
      }
      // no stream tracking is necessary
      requestHandler_->handleFireAndForgetRequest(
          std::move(frame.payload_), streamId);
      break;
    }

    case FrameType::RESUME:
    case FrameType::SETUP:
    case FrameType::METADATA_PUSH:
    case FrameType::LEASE:
    case FrameType::KEEPALIVE:
    case FrameType::RESERVED:
    case FrameType::REQUEST_N:
    case FrameType::CANCEL:
    case FrameType::PAYLOAD:
    case FrameType::ERROR:
    case FrameType::RESUME_OK:
    case FrameType::EXT:
    default:
      DLOG(ERROR) << "unknown stream frame (streamId=" << streamId
                  << " frameType=" << frameType << ")";
      closeWithError(Frame_ERROR::unexpectedFrame());
  }
}
/// @}

void ConnectionAutomaton::sendKeepalive(std::unique_ptr<folly::IOBuf> data) {
  sendKeepalive(FrameFlags::KEEPALIVE_RESPOND, std::move(data));
}

void ConnectionAutomaton::sendKeepalive(
    FrameFlags flags,
    std::unique_ptr<folly::IOBuf> data) {
  debugCheckCorrectExecutor();
  Frame_KEEPALIVE pingFrame(
      flags, resumeCache_->impliedPosition(), std::move(data));
  outputFrameOrEnqueue(
      frameSerializer_->serializeOut(std::move(pingFrame), remoteResumeable_));
}

void ConnectionAutomaton::tryClientResume(
    const ResumeIdentificationToken& token,
    std::shared_ptr<FrameTransport> frameTransport,
    std::unique_ptr<ClientResumeStatusCallback> resumeCallback) {
  frameTransport->outputFrameOrEnqueue(frameSerializer_->serializeOut(
      createResumeFrame(token)));

  // if the client was still connected we will disconnected the old connection
  // with a clear error message
  disconnect(
      std::runtime_error("resuming client on a different connection"));
  setResumable(true);
  reconnect(std::move(frameTransport), std::move(resumeCallback));
}

Frame_RESUME ConnectionAutomaton::createResumeFrame(
    const ResumeIdentificationToken& token) const {
  return Frame_RESUME(
      token,
      resumeCache_->impliedPosition(),
      resumeCache_->lastResetPosition(),
      frameSerializer_->protocolVersion());
}

bool ConnectionAutomaton::isPositionAvailable(ResumePosition position) {
  debugCheckCorrectExecutor();
  return resumeCache_->isPositionAvailable(position);
}

bool ConnectionAutomaton::resumeFromPositionOrClose(
    ResumePosition serverPosition,
    ResumePosition clientPosition) {
  debugCheckCorrectExecutor();
  DCHECK(!resumeCallback_);
  DCHECK(!isDisconnectedOrClosed());
  DCHECK(mode_ == ReactiveSocketMode::SERVER);

  bool clientPositionExist = (clientPosition == kUnspecifiedResumePosition) ||
      resumeCache_->canResumeFrom(clientPosition);

  if (clientPositionExist &&
      resumeCache_->isPositionAvailable(serverPosition)) {
    frameTransport_->outputFrameOrEnqueue(frameSerializer_->serializeOut(
        Frame_RESUME_OK(resumeCache_->impliedPosition())));
    resumeFromPosition(serverPosition);
    return true;
  } else {
    closeWithError(Frame_ERROR::connectionError(folly::to<std::string>(
        "Cannot resume server, client lastServerPosition=",
        serverPosition,
        " firstClientPosition=",
        clientPosition,
        " is not available. Last reset position is ",
        resumeCache_->lastResetPosition())));
    return false;
  }
}

void ConnectionAutomaton::resumeFromPosition(ResumePosition position) {
  DCHECK(!resumeCallback_);
  DCHECK(!isDisconnectedOrClosed());
  DCHECK(resumeCache_->isPositionAvailable(position));

  resumeStreams();
  resumeCache_->sendFramesFromPosition(position, *frameTransport_);

  for (auto& frame : streamState_->moveOutputPendingFrames()) {
    outputFrameOrEnqueue(std::move(frame));
  }

  if (!isDisconnectedOrClosed() && keepaliveTimer_) {
    keepaliveTimer_->start(shared_from_this());
  }
}

void ConnectionAutomaton::outputFrameOrEnqueue(
    std::unique_ptr<folly::IOBuf> frame) {
  debugCheckCorrectExecutor();
  // if we are resuming we cant send any frames until we receive RESUME_OK
  if (!isDisconnectedOrClosed() && !resumeCallback_) {
    outputFrame(std::move(frame));
  } else {
    streamState_->enqueueOutputPendingFrame(std::move(frame));
  }
}

void ConnectionAutomaton::requestFireAndForget(Payload request) {
  Frame_REQUEST_FNF frame(
      streamsFactory().getNextStreamId(),
      FrameFlags::EMPTY,
      std::move(std::move(request)));
  outputFrameOrEnqueue(frameSerializer_->serializeOut(std::move(frame)));
}

void ConnectionAutomaton::metadataPush(std::unique_ptr<folly::IOBuf> metadata) {
  outputFrameOrEnqueue(frameSerializer_->serializeOut(
      Frame_METADATA_PUSH(std::move(metadata))));
}

void ConnectionAutomaton::outputFrame(std::unique_ptr<folly::IOBuf> frame) {
  DCHECK(!isDisconnectedOrClosed());

  auto frameType = frameSerializer_->peekFrameType(*frame);
  stats_->frameWritten(frameType);

  if (isResumable_) {
    auto streamIdPtr = frameSerializer_->peekStreamId(*frame);
    resumeCache_->trackSentFrame(*frame, frameType, streamIdPtr);
  }
  frameTransport_->outputFrameOrEnqueue(std::move(frame));
}

uint32_t ConnectionAutomaton::getKeepaliveTime() const {
  debugCheckCorrectExecutor();
  return keepaliveTimer_
      ? static_cast<uint32_t>(keepaliveTimer_->keepaliveTime().count())
      : Frame_SETUP::kMaxKeepaliveTime;
}

bool ConnectionAutomaton::isDisconnectedOrClosed() const {
  return !frameTransport_;
}

bool ConnectionAutomaton::isClosed() const {
  return isClosed_;
}

DuplexConnection* ConnectionAutomaton::duplexConnection() const {
  debugCheckCorrectExecutor();
  return frameTransport_ ? frameTransport_->duplexConnection() : nullptr;
}

void ConnectionAutomaton::debugCheckCorrectExecutor() const {
  DCHECK(
      !dynamic_cast<folly::EventBase*>(&executor()) ||
      dynamic_cast<folly::EventBase*>(&executor())->isInEventBaseThread());
}

void ConnectionAutomaton::addConnectedListener(std::function<void()> listener) {
  CHECK(listener);
  onConnectListeners_.push_back(std::move(listener));
}

void ConnectionAutomaton::addDisconnectedListener(ErrorCallback listener) {
  CHECK(listener);
  onDisconnectListeners_.push_back(std::move(listener));
}

void ConnectionAutomaton::addClosedListener(ErrorCallback listener) {
  CHECK(listener);
  onCloseListeners_.push_back(std::move(listener));
}

void ConnectionAutomaton::setFrameSerializer(
    std::unique_ptr<FrameSerializer> frameSerializer) {
  CHECK(frameSerializer);
  // serializer is not interchangeable, it would screw up resumability
  // CHECK(!frameSerializer_);
  frameSerializer_ = std::move(frameSerializer);
}

void ConnectionAutomaton::setUpFrame(
    std::shared_ptr<FrameTransport> frameTransport,
    ConnectionSetupPayload setupPayload) {
  auto protocolVersion = getSerializerProtocolVersion();

  Frame_SETUP frame(
      setupPayload.resumable ? FrameFlags::RESUME_ENABLE : FrameFlags::EMPTY,
      protocolVersion.major,
      protocolVersion.minor,
      getKeepaliveTime(),
      Frame_SETUP::kMaxLifetime,
      setupPayload.token,
      std::move(setupPayload.metadataMimeType),
      std::move(setupPayload.dataMimeType),
      std::move(setupPayload.payload));

  // TODO: when the server returns back that it doesn't support resumability, we
  // should retry without resumability

  // making sure we send setup frame first
  frameTransport->outputFrameOrEnqueue(
      frameSerializer_->serializeOut(std::move(frame)));
  // then the rest of the cached frames will be sent
  connect(
      std::move(frameTransport), true, ProtocolVersion::Unknown);
}

ProtocolVersion ConnectionAutomaton::getSerializerProtocolVersion() {
  return frameSerializer_->protocolVersion();
}

void ConnectionAutomaton::writeNewStream(
    StreamId streamId,
    StreamType streamType,
    uint32_t initialRequestN,
    Payload payload,
    bool completed) {
  switch (streamType) {
    case StreamType::CHANNEL:
      outputFrameOrEnqueue(frameSerializer_->serializeOut(Frame_REQUEST_CHANNEL(
          streamId,
          completed ? FrameFlags::COMPLETE : FrameFlags::EMPTY,
          initialRequestN,
          std::move(payload))));
      break;

    case StreamType::STREAM:
      outputFrameOrEnqueue(frameSerializer_->serializeOut(Frame_REQUEST_STREAM(
          streamId, FrameFlags::EMPTY, initialRequestN, std::move(payload))));
      break;

    case StreamType::REQUEST_RESPONSE:
      outputFrameOrEnqueue(
          frameSerializer_->serializeOut(Frame_REQUEST_RESPONSE(
              streamId, FrameFlags::EMPTY, std::move(payload))));
      break;

    case StreamType::FNF:
      outputFrameOrEnqueue(frameSerializer_->serializeOut(
          Frame_REQUEST_FNF(streamId, FrameFlags::EMPTY, std::move(payload))));
      break;

    default:
      CHECK(false); // unknown type
  }
}

void ConnectionAutomaton::writeRequestN(StreamId streamId, uint32_t n) {
  outputFrameOrEnqueue(
      frameSerializer_->serializeOut(Frame_REQUEST_N(streamId, n)));
}

void ConnectionAutomaton::writePayload(
    StreamId streamId,
    Payload payload,
    bool complete) {
  Frame_PAYLOAD frame(
      streamId,
      FrameFlags::NEXT | (complete ? FrameFlags::COMPLETE : FrameFlags::EMPTY),
      std::move(payload));
  outputFrameOrEnqueue(frameSerializer_->serializeOut(std::move(frame)));
}

void ConnectionAutomaton::writeCloseStream(
    StreamId streamId,
    StreamCompletionSignal signal,
    Payload payload) {
  switch (signal) {
    case StreamCompletionSignal::COMPLETE:
      outputFrameOrEnqueue(
          frameSerializer_->serializeOut(Frame_PAYLOAD::complete(streamId)));
      break;

    case StreamCompletionSignal::CANCEL:
      outputFrameOrEnqueue(
          frameSerializer_->serializeOut(Frame_CANCEL(streamId)));
      break;

    case StreamCompletionSignal::ERROR:
      outputFrameOrEnqueue(frameSerializer_->serializeOut(
          Frame_ERROR::error(streamId, std::move(payload))));
      break;

    case StreamCompletionSignal::APPLICATION_ERROR:
      outputFrameOrEnqueue(frameSerializer_->serializeOut(
          Frame_ERROR::applicationError(streamId, std::move(payload))));
      break;

    case StreamCompletionSignal::INVALID_SETUP:
    case StreamCompletionSignal::UNSUPPORTED_SETUP:
    case StreamCompletionSignal::REJECTED_SETUP:

    case StreamCompletionSignal::CONNECTION_ERROR:
    case StreamCompletionSignal::CONNECTION_END:
    case StreamCompletionSignal::SOCKET_CLOSED:
    default:
      CHECK(false); // unexpected value
  }
}

void ConnectionAutomaton::onStreamClosed(
    StreamId streamId,
    StreamCompletionSignal signal) {
  endStream(streamId, signal);
}

bool ConnectionAutomaton::ensureOrAutodetectFrameSerializer(
    const folly::IOBuf& firstFrame) {
  if (frameSerializer_) {
    return true;
  }

  if (mode_ != ReactiveSocketMode::SERVER) {
    // this should never happen as clients are initized with FrameSerializer
    // instance
    DCHECK(false);
    return false;
  }

  auto serializer = FrameSerializer::createAutodetectedSerializer(firstFrame);
  if (!serializer) {
    LOG(ERROR) << "unable to detect protocol version";
    return false;
  }

  VLOG(2) << "detected protocol version" << serializer->protocolVersion();
  frameSerializer_ = std::move(serializer);
  return true;
}
} // reactivesocket
