#include "net/spdy/core/spdy_protocol.h"

namespace net {
extern SpdyPriority ClampSpdy3Priority(SpdyPriority priority) {
  if (priority < kV3HighestPriority) {
    SPDY_BUG << "Invalid priority: " << static_cast<int>(priority);
    return kV3HighestPriority;
  }
  if (priority > kV3LowestPriority) {
    SPDY_BUG << "Invalid priority: " << static_cast<int>(priority);
    return kV3LowestPriority;
  }
  return priority;
}

extern SpdyPriority Http2WeightToSpdy3Priority(int weight) {
  weight = ClampHttp2Weight(weight);
  const float kSteps = 255.9f / 7.f;
  return static_cast<SpdyPriority>(7.f - (weight - 1) / kSteps);
}	

extern int ClampHttp2Weight(int weight) {
  if (weight < kHttp2MinStreamWeight) {
    SPDY_BUG << "Invalid weight: " << weight;
    return kHttp2MinStreamWeight;
  }
  if (weight > kHttp2MaxStreamWeight) {
    SPDY_BUG << "Invalid weight: " << weight;
    return kHttp2MaxStreamWeight;
  }
  return weight;
}

} //net