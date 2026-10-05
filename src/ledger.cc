#include "perf_probe/ledger.h"

#include <set>

namespace perf_probe {

Ledger::Ledger(std::map<std::string, JoinRec> joins)
    : joins_(std::move(joins)) {}

void Ledger::ZeroSubtree(JoinRec* join,
                         std::map<std::string, JoinRec>* work) const {
    if (join == NULL) {
        return;
    }
    join->e2e_us = 0;
    for (size_t i = 0; i < join->arms.size(); ++i) {
        ArmRec& arm = join->arms[i];
        arm.e2e_us = 0;
        for (size_t j = 0; j < arm.children.size(); ++j) {
            std::map<std::string, JoinRec>::iterator it =
                work->find(arm.children[j]);
            if (it != work->end()) {
                ZeroSubtree(&it->second, work);
            }
        }
    }
}

void Ledger::Propagate(JoinRec* join, int64_t cap,
                       std::map<std::string, JoinRec>* work) const {
    if (join == NULL || !join->committed) {
        return;
    }
    join->e2e_us = MinUs(join->local_slack_us, cap);
    const int64_t child_cap = MinUs(join->local_slack_us, cap);

    for (size_t i = 0; i < join->arms.size(); ++i) {
        ArmRec& arm = join->arms[i];
        const bool winner =
            !join->winner_name.empty() && arm.name == join->winner_name;
        if (!winner) {
            arm.e2e_us = 0;
            for (size_t j = 0; j < arm.children.size(); ++j) {
                std::map<std::string, JoinRec>::iterator it =
                    work->find(arm.children[j]);
                if (it != work->end()) {
                    ZeroSubtree(&it->second, work);
                }
            }
            continue;
        }
        arm.e2e_us = child_cap;
        for (size_t j = 0; j < arm.children.size(); ++j) {
            std::map<std::string, JoinRec>::iterator it =
                work->find(arm.children[j]);
            if (it != work->end()) {
                Propagate(&it->second, child_cap, work);
            }
        }
    }
}

void Ledger::Emit(Sink* sink,
                  const std::map<std::string, JoinRec>& work) const {
    if (sink == NULL) {
        return;
    }
    for (std::map<std::string, JoinRec>::const_iterator it = work.begin();
         it != work.end(); ++it) {
        const JoinRec& j = it->second;
        if (!j.committed) {
            continue;
        }
        JoinReport jr;
        jr.name = j.name;
        jr.winner_name = j.winner_name;
        jr.wall_us = j.wall_us;
        jr.local_slack_us = j.local_slack_us;
        jr.e2e_us = j.e2e_us;
        sink->OnJoin(jr);

        for (size_t i = 0; i < j.arms.size(); ++i) {
            const ArmRec& a = j.arms[i];
            ArmReport ar;
            ar.join_name = j.name;
            ar.arm_name = a.name;
            ar.wall_us = a.wall_us;
            ar.e2e_us = a.e2e_us;
            ar.is_winner =
                !j.winner_name.empty() && a.name == j.winner_name;
            ar.is_leaf = a.children.empty();
            sink->OnArm(ar);
        }
    }
}

void Ledger::Flush(Sink* sink) const {
    std::map<std::string, JoinRec> work = joins_;

    // 根 = 未被任何 Seq 引用的已 Commit 汇点
    std::set<std::string> referenced;
    for (std::map<std::string, JoinRec>::const_iterator it = work.begin();
         it != work.end(); ++it) {
        const JoinRec& j = it->second;
        for (size_t i = 0; i < j.arms.size(); ++i) {
            for (size_t c = 0; c < j.arms[i].children.size(); ++c) {
                referenced.insert(j.arms[i].children[c]);
            }
        }
    }

    for (std::map<std::string, JoinRec>::iterator it = work.begin();
         it != work.end(); ++it) {
        if (!it->second.committed) {
            continue;
        }
        if (referenced.count(it->first) == 0) {
            Propagate(&it->second, kInfCap, &work);
        }
    }

    Emit(sink, work);
}

}  // namespace perf_probe
