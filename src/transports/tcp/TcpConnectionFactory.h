// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <folly/SocketAddress.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include "src/ConnectionFactory.h"
#include "src/DuplexConnection.h"

namespace rsocket {

/**
 * TCP implementation of ConnectionFactory for use with RSocket::createClient().
 *
 * Creation of this does nothing.  The `start` method kicks off work.
 */
class TcpConnectionFactory : public ConnectionFactory {
 public:
  explicit TcpConnectionFactory(folly::SocketAddress);
  virtual ~TcpConnectionFactory();

  static std::pair<std::unique_ptr<DuplexConnection>, folly::EventBase*>
  createDuplexConnectionFromConnectedSocket(
      folly::AsyncSocket::UniquePtr socket,
      folly::EventBase& eventBase);

  /**
   * Connect to server defined in constructor.
   *
   * Each call to connect() creates a new AsyncSocket.
   */
  void connect(OnConnect) override;

 private:
  folly::SocketAddress address_;
  folly::ScopedEventBaseThread worker_;
};
}
