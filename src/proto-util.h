#pragma once

#include "absl/time/time.h"
#include "google/protobuf/duration.pb.h"

namespace fasterswiper {

inline absl::Duration
FromProtoDuration(const google::protobuf::Duration &proto_dur) {
  return absl::Seconds(proto_dur.seconds()) +
         absl::Nanoseconds(proto_dur.nanos());
}

inline google::protobuf::Duration ToProtoDuration(const absl::Duration &dur) {
  const absl::Duration truncated = absl::Trunc(dur, absl::Seconds(1));
  google::protobuf::Duration proto_dur;
  proto_dur.set_seconds(absl::ToInt64Seconds(truncated));
  proto_dur.set_nanos(absl::ToInt64Nanoseconds(dur - truncated));
  return proto_dur;
}

} // namespace fasterswiper
