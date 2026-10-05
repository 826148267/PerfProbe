#ifndef PERF_PROBE_SINK_H_
#define PERF_PROBE_SINK_H_

#include "perf_probe/types.h"

namespace perf_probe {

class Sink {
 public:
    virtual ~Sink() {}
    virtual void OnJoin(const JoinReport& r) = 0;
    virtual void OnArm(const ArmReport& r) = 0;
};

}  // namespace perf_probe

#endif  // PERF_PROBE_SINK_H_
