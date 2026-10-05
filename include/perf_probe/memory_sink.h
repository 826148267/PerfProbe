#ifndef PERF_PROBE_MEMORY_SINK_H_
#define PERF_PROBE_MEMORY_SINK_H_

#include <map>
#include <string>

#include "perf_probe/sink.h"

namespace perf_probe {

// UT / 异步缓冲：按汇点名存报告。
class MemorySink : public Sink {
 public:
    void OnJoin(const JoinReport& r) override;
    void OnArm(const ArmReport& r) override;

    const JoinReport* Join(const std::string& name) const;
    const ArmReport* Arm(const std::string& join,
                         const std::string& arm) const;

    static std::string ArmKey(const std::string& join,
                              const std::string& arm);

    std::map<std::string, JoinReport> joins;
    std::map<std::string, ArmReport> arms;  // key = ArmKey(join, arm)
};

}  // namespace perf_probe

#endif  // PERF_PROBE_MEMORY_SINK_H_
