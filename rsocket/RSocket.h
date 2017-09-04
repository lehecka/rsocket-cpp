// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "rsocket/RSocketClient.h"
#include "rsocket/RSocketServer.h"

namespace rsocket {

/**
 * Main entry to creating RSocket clients and servers.
 */
class RSocket {
 public:
  // Creates a RSocketClient which is connected to the remoteside.
  static folly::Future<std::unique_ptr<RSocketClient>> createConnectedClient(
      std::shared_ptr<ConnectionFactory>,
      SetupParameters setupParameters = SetupParameters(),
      std::shared_ptr<RSocketResponder> responder =
          std::make_shared<RSocketResponder>(),
      std::unique_ptr<KeepaliveTimer> keepaliveTimer =
          std::unique_ptr<KeepaliveTimer>(),
      std::shared_ptr<RSocketStats> stats = RSocketStats::noop(),
      std::shared_ptr<RSocketConnectionEvents> connectionEvents =
          std::shared_ptr<RSocketConnectionEvents>(),
      std::shared_ptr<ResumeManager> resumeManager = nullptr,
      std::shared_ptr<ColdResumeHandler> coldResumeHandler =
          std::shared_ptr<ColdResumeHandler>());

  // Creates a RSocketClient which cold-resumes from the provided state
  static folly::Future<std::unique_ptr<RSocketClient>> createResumedClient(
      std::shared_ptr<ConnectionFactory>,
      ResumeIdentificationToken token,
      std::shared_ptr<ResumeManager> resumeManager,
      std::shared_ptr<ColdResumeHandler> coldResumeHandler,
      std::shared_ptr<RSocketResponder> responder =
          std::make_shared<RSocketResponder>(),
      std::unique_ptr<KeepaliveTimer> keepaliveTimer =
          std::unique_ptr<KeepaliveTimer>(),
      std::shared_ptr<RSocketStats> stats = RSocketStats::noop(),
      std::shared_ptr<RSocketConnectionEvents> connectionEvents =
          std::shared_ptr<RSocketConnectionEvents>(),
      ProtocolVersion protocolVersion = ProtocolVersion::Current());

  // Creates a RSocketClient from an existing DuplexConnection
  static std::unique_ptr<RSocketClient> createClientFromConnection(
      std::unique_ptr<DuplexConnection> connection,
      folly::EventBase& eventBase,
      SetupParameters setupParameters = SetupParameters(),
      std::shared_ptr<ConnectionFactory> connectionFactory = nullptr,
      std::shared_ptr<RSocketResponder> responder =
          std::make_shared<RSocketResponder>(),
      std::unique_ptr<KeepaliveTimer> keepaliveTimer =
          std::unique_ptr<KeepaliveTimer>(),
      std::shared_ptr<RSocketStats> stats = RSocketStats::noop(),
      std::shared_ptr<RSocketConnectionEvents> connectionEvents =
          std::shared_ptr<RSocketConnectionEvents>(),
      std::shared_ptr<ResumeManager> resumeManager = nullptr,
      std::shared_ptr<ColdResumeHandler> coldResumeHandler =
          std::shared_ptr<ColdResumeHandler>());

  // A convenience function to create RSocketServer
  static std::unique_ptr<RSocketServer> createServer(
      std::unique_ptr<ConnectionAcceptor>);

  RSocket() = delete;

  RSocket(const RSocket&) = delete;

  RSocket(RSocket&&) = delete;

  RSocket& operator=(const RSocket&) = delete;

  RSocket& operator=(RSocket&&) = delete;
};
}
