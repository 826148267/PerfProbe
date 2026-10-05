// PerfProbe 公共类型与账本记录。
#ifndef PERF_PROBE_TYPES_H_
#define PERF_PROBE_TYPES_H_

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace perf_probe {

// Propagate 根节点 cap：足够大，避免与真实 us 混淆。
const int64_t kInfCap = std::numeric_limits<int64_t>::max() / 4;

// ---- Sink 报告（Flush 产出）----

struct JoinReport {
    std::string name;
    std::string winner_name;
    int64_t wall_us;
    int64_t local_slack_us;
    int64_t e2e_us;

    JoinReport()
        : wall_us(0), local_slack_us(0), e2e_us(0) {}
};

struct ArmReport {
    std::string join_name;
    std::string arm_name;
    int64_t wall_us;
    int64_t e2e_us;
    bool is_winner;
    bool is_leaf;

    ArmReport()
        : wall_us(0), e2e_us(0), is_winner(false), is_leaf(true) {}
};

// ---- 账本内部记录 ----

struct ArmRec {
    std::string name;
    int64_t wall_us;                 // 叶子给定；Seq 在 Commit 时 = Σ 子墙钟
    std::vector<std::string> children;  // 空 = 叶子
    int64_t e2e_us;                  // Propagate 填

    ArmRec() : wall_us(0), e2e_us(0) {}
};

struct JoinRec {
    std::string name;
    bool committed;
    int64_t wall_us;
    std::string winner_name;
    int64_t local_slack_us;
    int64_t e2e_us;
    std::vector<ArmRec> arms;

    JoinRec()
        : committed(false),
          wall_us(0),
          local_slack_us(0),
          e2e_us(0) {}
};

inline int64_t MinUs(int64_t a, int64_t b) {
    return a < b ? a : b;
}

}  // namespace perf_probe

#endif  // PERF_PROBE_TYPES_H_
