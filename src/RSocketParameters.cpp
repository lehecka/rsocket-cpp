// Copyright 2004-present Facebook. All Rights Reserved.

#include "RSocketParameters.h"
#include <folly/String.h>

namespace rsocket {
std::ostream& operator<<(
    std::ostream& os,
    const SetupParameters& setupPayload) {
  return os << "metadataMimeType: " << setupPayload.metadataMimeType
            << " dataMimeType: " << setupPayload.dataMimeType
            << " payload: " << setupPayload.payload
            << " token: " << setupPayload.token
            << " resumable: " << setupPayload.resumable;
}
}
