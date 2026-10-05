#include "perf_probe/memory_sink.h"

namespace perf_probe {

void MemorySink::OnJoin(const JoinReport& r) {
    joins[r.name] = r;
}

void MemorySink::OnArm(const ArmReport& r) {
    arms[ArmKey(r.join_name, r.arm_name)] = r;
}

const JoinReport* MemorySink::Join(const std::string& name) const {
    std::map<std::string, JoinReport>::const_iterator it = joins.find(name);
    return it == joins.end() ? NULL : &it->second;
}

const ArmReport* MemorySink::Arm(const std::string& join,
                                 const std::string& arm) const {
    std::map<std::string, ArmReport>::const_iterator it =
        arms.find(ArmKey(join, arm));
    return it == arms.end() ? NULL : &it->second;
}

std::string MemorySink::ArmKey(const std::string& join,
                               const std::string& arm) {
    return join + std::string("\0", 1) + arm;
}

}  // namespace perf_probe
