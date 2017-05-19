// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <folly/io/async/EventBase.h>

#include "yarpl/Flowable.h"
#include "yarpl/Single.h"

#include "Payload.h"
#include "src/internal/ReactiveStreamsCompat.h"
#include "src/statemachine/RSocketStateMachine.h"

namespace rsocket {

/**
 * Request APIs to submit requests on an RSocket connection.
 *
 * This is most commonly used by an RSocketClient, but due to the symmetric
 * nature of RSocket, this can be used from server->client as well.
 *
 * For context within the overall RSocket protocol:
 *
 * - Client: The side initiating a connection.
 * - Server: The side accepting connections from clients.
 * - Connection: The instance of a transport session between client and server.
 * - Requester: The side sending a request.
 *       A connection has at most 2 Requesters. One in each direction.
 * - Responder: The side receiving a request.
 *       A connection has at most 2 Responders. One in each direction.
 *
 * See https://github.com/rsocket/rsocket/blob/master/Protocol.md#terminology
 * for more information on how this fits into the RSocket protocol terminology.
 */
class RSocketRequester {
 public:
  static std::shared_ptr<RSocketRequester> create(
      std::shared_ptr<rsocket::RSocketStateMachine> srs,
      folly::EventBase& executor);
  // TODO figure out how to use folly::Executor instead of EventBase

  ~RSocketRequester(); // implementing for logging right now
  RSocketRequester(const RSocketRequester&) = delete; // copy
  RSocketRequester(RSocketRequester&&) = delete; // move
  RSocketRequester& operator=(const RSocketRequester&) = delete; // copy
  RSocketRequester& operator=(RSocketRequester&&) = delete; // move

  /**
   * Send a single request and get a response stream.
   *
   * Interaction model details can be found at
   * https://github.com/ReactiveSocket/reactivesocket/blob/master/Protocol.md#request-stream
   *
   * @param payload
   */
  yarpl::Reference<yarpl::flowable::Flowable<rsocket::Payload>> requestStream(
      rsocket::Payload request);

  /**
    * Start a channel (streams in both directions).
    *
    * Interaction model details can be found at
    * https://github.com/ReactiveSocket/reactivesocket/blob/master/Protocol.md#request-channel
    *
    * @param request
    */
  yarpl::Reference<yarpl::flowable::Flowable<rsocket::Payload>> requestChannel(
      yarpl::Reference<yarpl::flowable::Flowable<rsocket::Payload>> requests);

  /**
   * Send a single request and get a single response.
   *
   * Interaction model details can be found at
   * https://github.com/ReactiveSocket/reactivesocket/blob/master/Protocol.md#stream-sequences-request-response
   *
   * @param payload
   */
  yarpl::Reference<yarpl::single::Single<rsocket::Payload>> requestResponse(
      rsocket::Payload request);

  /**
   * Send a single Payload with no response.
   *
   * The returned Single<void> invokes onSuccess or onError
   * based on client-side success or failure. Once the payload is
   * sent to the network it is "forgotten" and the Single<void> will
   * be finished with no further response indicating success
   * or failure on the server.
   *
   * Interaction model details can be found at
   * https://github.com/ReactiveSocket/reactivesocket/blob/master/Protocol.md#request-fire-n-forget
   *
   * @param payload
   */
  yarpl::Reference<yarpl::single::Single<void>> fireAndForget(
      rsocket::Payload request);

  /**
   * Send metadata without response.
   *
   * @param metadata
   */
  void metadataPush(std::unique_ptr<folly::IOBuf> metadata);

 private:
  RSocketRequester(
      std::shared_ptr<rsocket::RSocketStateMachine> srs,
      folly::EventBase& eventBase);
  std::shared_ptr<rsocket::RSocketStateMachine> stateMachine_;
  folly::EventBase& eventBase_;
};
}
