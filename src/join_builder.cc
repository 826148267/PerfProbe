#include "perf_probe/join_builder.h"

#include "perf_probe/perf_probe.h"

namespace perf_probe {

JoinBuilder::JoinBuilder(PerfProbe* probe, std::string name)
    : probe_(probe), name_(std::move(name)) {}

JoinBuilder& JoinBuilder::Arm(const std::string& name, int64_t us) {
    // 对齐 NoteLastArriver：空名 / 非正耗时直接丢弃
    if (name.empty() || us <= 0) {
        return *this;
    }
    ArmRec arm;
    arm.name = name;
    arm.wall_us = us;
    arms_.push_back(arm);
    return *this;
}

JoinBuilder::SeqSpec JoinBuilder::Arm(const std::string& name) {
    return SeqSpec(this, name);
}

JoinBuilder::SeqSpec::SeqSpec(JoinBuilder* parent, std::string arm_name)
    : parent_(parent), arm_name_(std::move(arm_name)) {}

JoinBuilder& JoinBuilder::SeqSpec::Seq(
    const std::vector<std::string>& children) {
    if (arm_name_.empty() || children.empty()) {
        return *parent_;
    }
    ArmRec arm;
    arm.name = arm_name_;
    arm.children = children;
    parent_->arms_.push_back(arm);
    return *parent_;
}

JoinBuilder& JoinBuilder::SeqSpec::Seq(const std::string& a) {
    return Seq(std::vector<std::string>(1, a));
}

JoinBuilder& JoinBuilder::SeqSpec::Seq(const std::string& a,
                                      const std::string& b) {
    std::vector<std::string> v;
    v.push_back(a);
    v.push_back(b);
    return Seq(v);
}

JoinBuilder& JoinBuilder::SeqSpec::Seq(const std::string& a,
                                      const std::string& b,
                                      const std::string& c) {
    std::vector<std::string> v;
    v.push_back(a);
    v.push_back(b);
    v.push_back(c);
    return Seq(v);
}

JoinBuilder& JoinBuilder::SeqSpec::Seq(const std::string& a,
                                      const std::string& b,
                                      const std::string& c,
                                      const std::string& d) {
    std::vector<std::string> v;
    v.push_back(a);
    v.push_back(b);
    v.push_back(c);
    v.push_back(d);
    return Seq(v);
}

void JoinBuilder::Commit() {
    if (probe_ == NULL || !probe_->enabled_ || probe_->sealed_) {
        return;
    }

    JoinRec rec;
    rec.name = name_;
    rec.arms = arms_;

    for (size_t i = 0; i < rec.arms.size(); ++i) {
        ArmRec& arm = rec.arms[i];
        if (arm.children.empty()) {
            continue;
        }
        int64_t sum = 0;
        for (size_t j = 0; j < arm.children.size(); ++j) {
            std::map<std::string, JoinRec>::const_iterator it =
                probe_->joins_.find(arm.children[j]);
            if (it == probe_->joins_.end() || !it->second.committed) {
                continue;  // 未 Commit 子：墙钟按 0
            }
            sum += it->second.wall_us;
        }
        arm.wall_us = sum;
    }

    int64_t first = 0;
    int64_t second = 0;
    std::string winner;
    for (size_t i = 0; i < rec.arms.size(); ++i) {
        const ArmRec& arm = rec.arms[i];
        if (arm.wall_us > first) {
            second = first;
            first = arm.wall_us;
            winner = arm.name;
        } else if (arm.wall_us > second) {
            second = arm.wall_us;
        }
    }
    rec.wall_us = first;
    rec.winner_name = winner;
    rec.local_slack_us = (rec.arms.size() >= 2) ? (first - second) : 0;
    rec.committed = true;

    probe_->joins_[name_] = rec;
}

}  // namespace perf_probe
