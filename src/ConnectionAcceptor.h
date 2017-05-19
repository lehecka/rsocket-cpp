// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <folly/futures/Future.h>
#include <folly/io/async/EventBase.h>

#include "src/DuplexConnection.h"

namespace rsocket {

using OnDuplexConnectionAccept = std::function<
    void(std::unique_ptr<rsocket::DuplexConnection>, folly::EventBase&)>;

/**
 * Common interface for a server that accepts connections and turns them into
 * DuplexConnection.
 *
 * This is primarily used with RSocket::createServer(ConnectionAcceptor)
 *
 * Built-in implementations can be found in rsocket/transports/, such as
 * rsocket/transports/TcpConnectionAcceptor.h
 *
 * TODO: Add way of specifying number of worker threads.
 */
class ConnectionAcceptor {
 public:
  ConnectionAcceptor() = default;
  virtual ~ConnectionAcceptor() = default;

  ConnectionAcceptor(const ConnectionAcceptor&) = delete;
  ConnectionAcceptor(ConnectionAcceptor&&) = delete;

  ConnectionAcceptor& operator=(const ConnectionAcceptor&) = delete;
  ConnectionAcceptor& operator=(ConnectionAcceptor&&) = delete;

  /**
   * Allocate/start required resources (threads, sockets, etc) and begin
   * listening for new connections.
   *
   * Will return an empty future on success, otherwise the future will contain
   * the error.
   *
   * This can only be called once.
   */
  virtual folly::Future<folly::Unit> start(
      OnDuplexConnectionAccept onAccept) = 0;

  /**
   * Stop listening for new connections.
   *
   * This can only be called once.  Must be called in or before
   * the implementation's destructor.  Must be synchronous.
   */
  virtual void stop() = 0;
};
}
