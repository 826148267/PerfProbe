#include "perf_probe/perf_probe.h"

namespace perf_probe {

PerfProbe::PerfProbe() : enabled_(false), sealed_(false) {}

void PerfProbe::Enable() {
    if (!sealed_) {
        enabled_ = true;
    }
}

JoinBuilder PerfProbe::Join(const std::string& name) {
    return JoinBuilder(this, name);
}

void PerfProbe::Flush(Sink* sink) const {
    if (!enabled_) {
        return;
    }
    Ledger(joins_).Flush(sink);
}

Ledger PerfProbe::Snapshot() const {
    return Ledger(joins_);
}

Ledger PerfProbe::Detach() {
    Ledger out(std::move(joins_));
    joins_.clear();
    sealed_ = true;
    enabled_ = false;
    return out;
}

}  // namespace perf_probe
