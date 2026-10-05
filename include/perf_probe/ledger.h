#ifndef PERF_PROBE_LEDGER_H_
#define PERF_PROBE_LEDGER_H_

#include <map>
#include <string>

#include "perf_probe/sink.h"
#include "perf_probe/types.h"

namespace perf_probe {

// 只读账本（Detach / Snapshot）。可异步 Flush；数值与同步路径一致。
class Ledger {
 public:
    Ledger() {}
    explicit Ledger(std::map<std::string, JoinRec> joins);

    void Flush(Sink* sink) const;

    const std::map<std::string, JoinRec>& joins() const { return joins_; }

 private:
    void Propagate(JoinRec* join, int64_t cap,
                   std::map<std::string, JoinRec>* work) const;
    void ZeroSubtree(JoinRec* join,
                     std::map<std::string, JoinRec>* work) const;
    void Emit(Sink* sink, const std::map<std::string, JoinRec>& work) const;

    std::map<std::string, JoinRec> joins_;
};

}  // namespace perf_probe

#endif  // PERF_PROBE_LEDGER_H_
