// PerfProbe（性能探针）
//
// 统一模型：只有 Join。Enable / Join / Arm / Seq / Commit 填账本；
// Flush 只读 Propagate。异步 Flush 合法，前提：
//   1) 最后一个 Commit happens-before Flush（或先 Detach/Snapshot）
//   2) Flush 期间禁止再 Join/Arm/Commit
//   3) Context 先销毁时必须 Detach 后再异步 Flush
//   4) Sink 自理线程与落盘；库不开线程
//   5) 同一账本同步/异步 Flush 数值一致
//
// 不做：Race、多线程并发 Arm、Flush 内开线程、跨请求共享可变实例。

#ifndef PERF_PROBE_PERF_PROBE_H_
#define PERF_PROBE_PERF_PROBE_H_

#include <map>
#include <string>

#include "perf_probe/join_builder.h"
#include "perf_probe/ledger.h"
#include "perf_probe/memory_sink.h"
#include "perf_probe/sink.h"
#include "perf_probe/types.h"

namespace perf_probe {

class PerfProbe {
 public:
    PerfProbe();

    bool enabled() const { return enabled_; }
    void Enable();

    JoinBuilder Join(const std::string& name);

    // 对本实例账本 Propagate + Sink；仍可再次 Flush。
    void Flush(Sink* sink) const;

    // 深拷贝；原实例可继续存在，拷贝可异步 Flush。
    Ledger Snapshot() const;

    // 移出账本并封印本实例；返回的 Ledger 可异步 Flush。
    Ledger Detach();

 private:
    friend class JoinBuilder;

    bool enabled_;
    bool sealed_;  // Detach 后禁止再写
    std::map<std::string, JoinRec> joins_;
};

}  // namespace perf_probe

#endif  // PERF_PROBE_PERF_PROBE_H_
