#ifndef PERF_PROBE_JOIN_BUILDER_H_
#define PERF_PROBE_JOIN_BUILDER_H_

#include <string>
#include <vector>

#include "perf_probe/types.h"

namespace perf_probe {

class PerfProbe;

// 建造一个汇点：叶子 Arm(name, us) 或合成 Arm(name).Seq(...)，最后 Commit。
class JoinBuilder {
 public:
    JoinBuilder& Arm(const std::string& name, int64_t us);

    class SeqSpec {
     public:
        JoinBuilder& Seq(const std::vector<std::string>& children);
        JoinBuilder& Seq(const std::string& a);
        JoinBuilder& Seq(const std::string& a, const std::string& b);
        JoinBuilder& Seq(const std::string& a, const std::string& b,
                         const std::string& c);
        JoinBuilder& Seq(const std::string& a, const std::string& b,
                         const std::string& c, const std::string& d);

     private:
        friend class JoinBuilder;
        SeqSpec(JoinBuilder* parent, std::string arm_name);

        JoinBuilder* parent_;
        std::string arm_name_;
    };

    // 合成臂：随后 .Seq(...)
    SeqSpec Arm(const std::string& name);

    void Commit();

 private:
    friend class PerfProbe;
    friend class SeqSpec;

    JoinBuilder(PerfProbe* probe, std::string name);

    PerfProbe* probe_;
    std::string name_;
    std::vector<ArmRec> arms_;
};

}  // namespace perf_probe

#endif  // PERF_PROBE_JOIN_BUILDER_H_
