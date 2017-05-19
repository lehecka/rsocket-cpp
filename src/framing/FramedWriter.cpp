// Copyright 2004-present Facebook. All Rights Reserved.

#include "src/framing/FramedWriter.h"
#include <folly/io/Cursor.h>
#include "FrameSerializer_v1_0.h"

namespace rsocket {

constexpr static const auto kMaxFrameLength = 0xFFFFFF; // 24bit max value

template <typename TWriter>
static void writeFrameLength(
    TWriter& cur,
    size_t frameLength,
    size_t frameSizeFieldLength) {
  DCHECK(frameSizeFieldLength > 0);

  // starting from the highest byte
  // frameSizeFieldLength == 3 => shift = [16,8,0]
  // frameSizeFieldLength == 4 => shift = [24,16,8,0]
  auto shift = (frameSizeFieldLength - 1) * 8;

  while (frameSizeFieldLength--) {
    auto byte = (frameLength >> shift) & 0xFF;
    cur.write(static_cast<uint8_t>(byte));
    shift -= 8;
  }
}

size_t FramedWriter::getFrameSizeFieldLength() const {
  CHECK(*protocolVersion_ != ProtocolVersion::Unknown);
  if (*protocolVersion_ < FrameSerializerV1_0::Version) {
    return sizeof(int32_t);
  } else {
    return 3; // bytes
  }
}

size_t FramedWriter::getPayloadLength(size_t payloadLength) const {
  DCHECK(*protocolVersion_ != ProtocolVersion::Unknown);
  if (*protocolVersion_ < FrameSerializerV1_0::Version) {
    return payloadLength + getFrameSizeFieldLength();
  } else {
    return payloadLength;
  }
}

void FramedWriter::onSubscribeImpl(
    std::shared_ptr<Subscription> subscription) noexcept {
  CHECK(!writerSubscription_);
  writerSubscription_ = std::move(subscription);
  stream_->onSubscribe(shared_from_this());
}

std::unique_ptr<folly::IOBuf> FramedWriter::appendSize(
    std::unique_ptr<folly::IOBuf> payload) {
  CHECK(payload);

  const auto frameSizeFieldLength = getFrameSizeFieldLength();
  // the frame size includes the payload size and the size value
  auto payloadLength = getPayloadLength(payload->computeChainDataLength());
  if (payloadLength > kMaxFrameLength) {
    return nullptr;
  }

  if (payload->headroom() >= frameSizeFieldLength) {
    // move the data pointer back and write value to the payload
    payload->prepend(frameSizeFieldLength);
    folly::io::RWPrivateCursor cur(payload.get());
    writeFrameLength(cur, payloadLength, frameSizeFieldLength);
    VLOG(4) << "writing frame length=" << payload->length() << std::endl
            << hexDump(payload->clone()->moveToFbString());
    return payload;
  } else {
    auto newPayload = folly::IOBuf::createCombined(frameSizeFieldLength);
    folly::io::Appender appender(newPayload.get(), /* do not grow */ 0);
    writeFrameLength(appender, payloadLength, frameSizeFieldLength);
    newPayload->appendChain(std::move(payload));
    VLOG(4) << "writing frame length=" << newPayload->computeChainDataLength()
            << std::endl
            << hexDump(newPayload->clone()->moveToFbString());
    return newPayload;
  }
}

void FramedWriter::onNextImpl(std::unique_ptr<folly::IOBuf> payload) noexcept {
  auto sizedPayload = appendSize(std::move(payload));
  if (!sizedPayload) {
    VLOG(1) << "payload too big";
    cancel();
    return;
  }
  stream_->onNext(std::move(sizedPayload));
}

void FramedWriter::onNextMultiple(
    std::vector<std::unique_ptr<folly::IOBuf>> payloads) {
  folly::IOBufQueue payloadQueue;

  for (auto& payload : payloads) {
    auto sizedPayload = appendSize(std::move(payload));
    if (!sizedPayload) {
      VLOG(1) << "payload too big";
      cancel();
      return;
    }
    payloadQueue.append(std::move(sizedPayload));
  }
  stream_->onNext(payloadQueue.move());
}

void FramedWriter::onCompleteImpl() noexcept {
  if (auto subscriber = std::move(stream_)) {
    subscriber->onComplete();
  }
  if (auto subscription = std::move(writerSubscription_)) {
    subscription->cancel();
  }
}

void FramedWriter::onErrorImpl(folly::exception_wrapper ex) noexcept {
  if (auto subscriber = std::move(stream_)) {
    subscriber->onError(std::move(ex));
  }
  if (auto subscription = std::move(writerSubscription_)) {
    subscription->cancel();
  }
}

void FramedWriter::requestImpl(size_t n) noexcept {
  // it is possible to receive requestImpl after on{Complete,Error}
  // because it is a different interface and can be hooked up somewhere else
  if (writerSubscription_) {
    writerSubscription_->request(n);
  }
}

void FramedWriter::cancelImpl() noexcept {
  if (auto subscription = std::move(writerSubscription_)) {
    subscription->cancel();
  }
  if (auto subscriber = std::move(stream_)) {
    subscriber->onComplete();
  }
}

} // reactivesocket
