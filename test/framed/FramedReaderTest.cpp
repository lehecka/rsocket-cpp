// Copyright 2004-present Facebook. All Rights Reserved.

#include <array>

#include <folly/ExceptionWrapper.h>
#include <folly/Format.h>
#include <folly/io/Cursor.h>
#include <gmock/gmock.h>
#include "src/FrameSerializer.h"
#include "src/ReactiveSocket.h"
#include "src/framed/FramedDuplexConnection.h"
#include "src/framed/FramedReader.h"
#include "test/InlineConnection.h"
#include "test/MockRequestHandler.h"
#include "test/streams/Mocks.h"

using namespace ::testing;
using namespace ::reactivesocket;

TEST(FramedReaderTest, Read1Frame) {
  auto frameSubscriber =
      std::make_shared<MockSubscriber<std::unique_ptr<folly::IOBuf>>>();
  auto wireSubscription = std::make_shared<MockSubscription>();

  std::string msg1("value1value1");

  auto payload1 = folly::IOBuf::create(0);
  folly::io::Appender a1(payload1.get(), 10);
  a1.writeBE<int32_t>(msg1.size() + sizeof(int32_t));
  folly::format("{}", msg1.c_str())(a1);

  auto framedReader = std::make_shared<FramedReader>(
      frameSubscriber,
      inlineExecutor(),
      std::make_shared<ProtocolVersion>(
          FrameSerializer::getCurrentProtocolVersion()));

  EXPECT_CALL(*frameSubscriber, onSubscribe_(_)).Times(1);

  framedReader->onSubscribe(wireSubscription);

  EXPECT_CALL(*frameSubscriber, onNext_(_)).Times(0);
  EXPECT_CALL(*frameSubscriber, onError_(_)).Times(0);
  EXPECT_CALL(*frameSubscriber, onComplete_()).Times(0);

  framedReader->onNext(std::move(payload1));

  EXPECT_CALL(*frameSubscriber, onNext_(_))
      .WillOnce(Invoke([&](std::unique_ptr<folly::IOBuf>& p) {
        ASSERT_EQ(msg1, p->moveToFbString().toStdString());
      }));

  EXPECT_CALL(*wireSubscription, request_(_)).Times(1);

  frameSubscriber->subscription()->request(3);

  // to delete objects
  EXPECT_CALL(*frameSubscriber, onComplete_()).Times(1);
  EXPECT_CALL(*wireSubscription, cancel_()).Times(1);

  frameSubscriber->subscription()->cancel();
  framedReader->onComplete();
}

TEST(FramedReaderTest, Read3Frames) {
  auto frameSubscriber =
      std::make_shared<MockSubscriber<std::unique_ptr<folly::IOBuf>>>();
  auto wireSubscription = std::make_shared<MockSubscription>();

  std::string msg1("value1value1");
  std::string msg2("value2value2");
  std::string msg3("value3value3");

  auto payload1 = folly::IOBuf::create(0);
  folly::io::Appender a1(payload1.get(), 10);
  a1.writeBE<int32_t>(msg1.size() + sizeof(int32_t));
  folly::format("{}", msg1.c_str())(a1);
  a1.writeBE<int32_t>(msg2.size() + sizeof(int32_t));
  folly::format("{}", msg2.c_str())(a1);

  auto payload2 = folly::IOBuf::create(0);
  folly::io::Appender a2(payload2.get(), 10);
  a2.writeBE<int32_t>(msg3.size() + sizeof(int32_t));
  folly::format("{}", msg3.c_str())(a2);

  folly::IOBufQueue bufQueue;
  bufQueue.append(std::move(payload1));
  bufQueue.append(std::move(payload2));

  auto framedReader = std::make_shared<FramedReader>(
      frameSubscriber,
      inlineExecutor(),
      std::make_shared<ProtocolVersion>(
          FrameSerializer::getCurrentProtocolVersion()));

  EXPECT_CALL(*frameSubscriber, onSubscribe_(_)).Times(1);

  framedReader->onSubscribe(wireSubscription);

  EXPECT_CALL(*frameSubscriber, onNext_(_)).Times(0);
  EXPECT_CALL(*frameSubscriber, onError_(_)).Times(0);
  EXPECT_CALL(*frameSubscriber, onComplete_()).Times(0);

  framedReader->onNext(bufQueue.move());

  EXPECT_CALL(*frameSubscriber, onNext_(_))
      .WillOnce(Invoke([&](std::unique_ptr<folly::IOBuf>& p) {
        ASSERT_EQ(msg1, p->moveToFbString().toStdString());
      }))
      .WillOnce(Invoke([&](std::unique_ptr<folly::IOBuf>& p) {
        ASSERT_EQ(msg2, p->moveToFbString().toStdString());
      }))
      .WillOnce(Invoke([&](std::unique_ptr<folly::IOBuf>& p) {
        ASSERT_EQ(msg3, p->moveToFbString().toStdString());
      }));

  frameSubscriber->subscription()->request(3);

  // to delete objects
  EXPECT_CALL(*frameSubscriber, onComplete_()).Times(1);
  EXPECT_CALL(*wireSubscription, cancel_()).Times(1);

  frameSubscriber->subscription()->cancel();
  framedReader->onComplete();
}

TEST(FramedReaderTest, Read1FrameIncomplete) {
  auto frameSubscriber =
      std::make_shared<MockSubscriber<std::unique_ptr<folly::IOBuf>>>();
  auto wireSubscription = std::make_shared<MockSubscription>();

  std::string part1("val");
  std::string part2("ueXXX");
  std::string msg1 = part1 + part2;

  auto framedReader = std::make_shared<FramedReader>(
      frameSubscriber,
      inlineExecutor(),
      std::make_shared<ProtocolVersion>(
          FrameSerializer::getCurrentProtocolVersion()));
  framedReader->onSubscribe(wireSubscription);

  EXPECT_CALL(*frameSubscriber, onNext_(_)).Times(0);
  EXPECT_CALL(*frameSubscriber, onError_(_)).Times(0);
  EXPECT_CALL(*frameSubscriber, onComplete_()).Times(0);

  frameSubscriber->subscription()->request(3);

  auto payload = folly::IOBuf::create(0);
  {
    folly::io::Appender appender(payload.get(), 10);
    appender.writeBE<int32_t>(msg1.size() + sizeof(int32_t));
  }

  framedReader->onNext(std::move(payload));

  payload = folly::IOBuf::create(0);
  {
    folly::io::Appender appender(payload.get(), 10);
    folly::format("{}", part1.c_str())(appender);
  }

  framedReader->onNext(std::move(payload));

  payload = folly::IOBuf::create(0);
  {
    folly::io::Appender appender(payload.get(), 10);
    folly::format("{}", part2.c_str())(appender);
  }

  EXPECT_CALL(*frameSubscriber, onNext_(_))
      .WillOnce(Invoke([&](std::unique_ptr<folly::IOBuf>& p) {
        ASSERT_EQ(msg1, p->moveToFbString().toStdString());
      }));

  framedReader->onNext(std::move(payload));
  // to delete objects
  EXPECT_CALL(*frameSubscriber, onComplete_()).Times(1);
  EXPECT_CALL(*wireSubscription, cancel_()).Times(1);

  frameSubscriber->subscription()->cancel();
  framedReader->onComplete();
}

TEST(FramedReaderTest, InvalidDataStream) {
  auto rsConnection = std::make_unique<InlineConnection>();
  auto testConnection = std::make_unique<InlineConnection>();

  rsConnection->connectTo(*testConnection);

  auto framedRsAutomatonConnection = std::make_unique<FramedDuplexConnection>(
      std::move(rsConnection), inlineExecutor());

  // Dump 1 invalid frame and expect an error
  auto inputSubscription = std::make_shared<MockSubscription>();
  auto sub = testConnection->getOutput();
  EXPECT_CALL(*inputSubscription, request_(_)).WillOnce(Invoke([&](auto) {
    auto invalidFrameSizePayload =
        folly::IOBuf::createCombined(sizeof(int32_t));
    folly::io::Appender appender(
        invalidFrameSizePayload.get(), /* do not grow */ 0);
    appender.writeBE<int32_t>(1);
    sub->onNext(std::move(invalidFrameSizePayload));
  }));
  EXPECT_CALL(*inputSubscription, cancel_()).WillOnce(Invoke([&sub]() {
    sub->onComplete();
  }));

  auto testOutputSubscriber =
      std::make_shared<MockSubscriber<std::unique_ptr<folly::IOBuf>>>();
  EXPECT_CALL(*testOutputSubscriber, onSubscribe_(_))
      .WillOnce(Invoke([&](std::shared_ptr<Subscription> subscription) {
        // allow receiving frames from the automaton
        subscription->request(std::numeric_limits<size_t>::max());
      }));
  EXPECT_CALL(*testOutputSubscriber, onNext_(_))
      .WillOnce(Invoke([&](std::unique_ptr<folly::IOBuf>& p) {
        // SETUP frame with leading frame size
      }));

  EXPECT_CALL(*testOutputSubscriber, onComplete_()).Times(0);
  EXPECT_CALL(*testOutputSubscriber, onError_(_)).Times(1);

  testConnection->setInput(testOutputSubscriber);
  sub->onSubscribe(inputSubscription);

  auto requestHandler = std::make_unique<StrictMock<MockRequestHandler>>();
  EXPECT_CALL(*requestHandler, socketOnConnected()).Times(1);
  EXPECT_CALL(*requestHandler, socketOnClosed(_)).Times(1);

  auto reactiveSocket = ReactiveSocket::fromClientConnection(
      defaultExecutor(),
      std::move(framedRsAutomatonConnection),
      // No interactions on this mock, the client will not accept any
      // requests.
      std::move(requestHandler),
      ConnectionSetupPayload("", "", Payload("test client payload")));
}

// TODO(lehecka): verify FramedReader protocol autodetection mechanism
// with this test
// make sure it will never crash
//
// TEST(FramedReaderTest, ReadEmptyPayload) {
//   auto frameSubscriber = std::make_shared<
//       NiceMock<MockSubscriber<std::unique_ptr<folly::IOBuf>>>>();
//
//   auto payload = folly::IOBuf::create(0);
//   auto frameSize = sizeof(int32_t);
//   folly::io::Appender a(payload.get(), frameSize);
//   a.writeBE<int32_t>(frameSize);
//
//   auto framedReader = std::make_shared<FramedReader>(
//       frameSubscriber,
//       inlineExecutor(),
//       std::make_shared<ProtocolVersion>(
//           FrameSerializer::getCurrentProtocolVersion()));
//
//   framedReader->onSubscribe(std::make_shared<NiceMock<MockSubscription>>());
//   framedReader->onNext(std::move(payload));
//
//   EXPECT_CALL(*frameSubscriber, onNext_(_))
//       .WillOnce(Invoke([&](std::unique_ptr<folly::IOBuf>& p) {
//         ASSERT_EQ("", p->moveToFbString().toStdString());
//       }));
//   EXPECT_CALL(*frameSubscriber, onError_(_)).Times(0);
//
//   frameSubscriber->subscription()->request(1);
//   framedReader->onComplete();
// }
