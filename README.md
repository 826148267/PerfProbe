# PerfProbe

A small C++ library for **causal-style performance attribution** on request topologies.

You register a join tree (parallel arms + serial stages) and measured wall times; the library computes who is on the critical path and how much end-to-end latency would improve if that path were accelerated.

No business SDKs. No glog / brpc / logging backends inside the library.

```bash
bazel test //:topology_test
```

---

## Why not another latency histogram?

When parallel RPCs and serial stages are stacked, a slow leaf does not always matter: if another arm dominates wall time, speeding up the loser does not shrink end-to-end latency.

[SOSP'16 / Causal Profiling (COZ)](https://www.usenix.org/conference/atc16/technical-sessions/presentation/curtsinger) framed this problem with *virtual speedup*. The original tooling instruments `pthread`; many production stacks use M:N user-level threads (e.g. bthread), so that instrumentation does not attribute correctly on the real call path.

PerfProbe uses **hypothetical accounting** instead: declare topology + measured arm walls, then derive “if we accelerate the winner, how much e2e can improve?”. The library owns the formulas; callers only build the tree.

| Concept | Meaning |
|---------|---------|
| **Join** | Wait for all arms; wall = max (winner) |
| **Seq** | Serial child joins; wall = sum, linked in the tree |
| **local_slack** | `winner − second` at this join |
| **e2e slack** | Slack after propagating an acceleration cap down the winner path |

Loser subtrees get e2e = 0 — accelerating them does not help end-to-end.

**Intentionally out of scope:** `Race`, dual-write wall helpers, injecting real delays, spawning threads inside the library.

---

## Model

### Commit (ledger fill)

For each join:

1. Leaf arm: `wall = given us`
2. Seq arm: `wall = Σ` committed child join walls (missing children count as 0)
3. `winner = argmax(wall)`, `second =` next largest
4. `local_slack = (arms ≥ 2) ? winner − second : 0`

### Flush / Propagate (read-only e2e)

Start at roots with `cap = +∞`:

```text
join.e2e = min(local_slack, cap)
child_cap = min(local_slack, cap)

for each arm:
  winner: arm.e2e = child_cap
          Seq children: Propagate(child, child_cap)   # shared cap
  loser:  arm.e2e = 0; zero entire subtree
```

Roots = committed joins not referenced by any `Seq` (each gets `cap=∞`).

```text
          vs_root (cap=∞)
         /              \
    Arm pipeline.Seq   Arm sidecar (leaf)
     /        \
  wave1      wave2
```

---

## API

| Call | Role |
|------|------|
| `Enable()` | Turn recording on; without it Join/Flush are no-ops |
| `Join(name)` | Open a join |
| `.Arm(name, us)` | Leaf arm (empty name or `us≤0` dropped) |
| `.Arm(name).Seq(children…)` | Synthetic arm: sum child walls + attach tree |
| `Commit()` | Finalize this join into the ledger |
| `Flush(Sink*)` | Propagate from roots through `Sink` |
| `Snapshot()` | Deep-copy ledger (original may keep mutating) |
| `Detach()` | Move ledger out and seal this instance for async flush |

```cpp
#include "perf_probe/perf_probe.h"

using namespace perf_probe;

PerfProbe probe;
probe.Enable();

probe.Join("wave1")
    .Arm("api_a", 100000)
    .Arm("api_b", 80000)
    .Commit();  // winner a, local=20000

probe.Join("wave2")
    .Arm("api_c", 50000)
    .Arm("api_d", 40000)
    .Commit();  // winner c, local=10000

{
    auto root = probe.Join("vs_sidecar");
    root.Arm("pipeline").Seq("wave1", "wave2");  // wall=150000
    root.Arm("sidecar", 100000);
    root.Commit();  // pipeline wins, local=50000
}

MemorySink sink;
probe.Flush(&sink);
```

### Custom Sink

Implement `Sink::OnJoin` / `OnArm` (pure virtual) and map `JoinReport` / `ArmReport` to your metrics or logs.  
`MemorySink` is for tests and in-memory buffering; production adapters live in your application.

---

## Async flush

Hot path: Enable / Join / Arm / Commit. Flush is a pure traversal of a committed ledger.

1. Last `Commit` happens-before `Flush` (or `Snapshot` / `Detach` first)
2. No Join/Arm/Commit during Flush on that instance
3. If the request context will die first, `Detach` before async flush
4. The library does not start threads; sinks own I/O
5. Sync and async flush on the same ledger must agree numerically

```cpp
auto ledger = probe.Detach();
// ... another thread / tail queue ...
MemorySink sink;
ledger.Flush(&sink);
```

---

## Conventions

- Child joins must `Commit` before an outer `Seq` references them; no cycles
- Prefer single-threaded writers per `PerfProbe`; do not share a mutable instance across requests
- Re-`Join` same name overwrites
- Empty / single arm → `local_slack = 0`
- Without `Enable`, `Flush` emits nothing

### Parallel arms: gather, then serial Arm (recommended)

The library has **no locks**. Parallelism belongs in measurement only; writing the probe must be serial.

**Slots ≠ topology.** Flat `slots` hold leaf walls; `Join` / `Seq` are declared when building the ledger (in code or a separate topology descriptor). You may keep only slots on the hot path and build + Flush later.

```cpp
enum { W1A, W1B, W2C, W2D, SIDE, kN };
std::vector<int64_t> slots(kN);
std::vector<std::thread> workers;
for (int i = 0; i < kN; ++i) {
    workers.emplace_back([&, i] { slots[i] = measure_leaf(i); });
}
for (auto& t : workers) t.join();

PerfProbe probe;
probe.Enable();
probe.Join("wave1").Arm("a", slots[W1A]).Arm("b", slots[W1B]).Commit();
probe.Join("wave2").Arm("c", slots[W2C]).Arm("d", slots[W2D]).Commit();
{
    auto j = probe.Join("vs_sidecar");
    j.Arm("pipeline").Seq("wave1", "wave2");
    j.Arm("sidecar", slots[SIDE]);
    j.Commit();
}
MemorySink sink;
probe.Flush(&sink);
```

See UT `TopologyI.GatherThenSerialArm` / `TopologyI.SlotsOnlyAsyncBuild`. If you must Arm as each worker finishes, serialize with your own mutex; never concurrent `push_back` on the same `JoinBuilder`.

---

## Layout

```text
include/perf_probe/
  types.h          reports + ledger records
  sink.h           Sink interface
  memory_sink.h    MemorySink
  ledger.h         read-only ledger + Propagate
  join_builder.h   JoinBuilder
  perf_probe.h     umbrella
src/
test/topology_test.cc
```

Bazel: `//:perf_probe`, `//:topology_test`.

```python
git_repository(
    name = "perf_probe",
    remote = "https://github.com/826148267/PerfProbe.git",
    commit = "<pin>",
)
# deps = ["@perf_probe//:perf_probe"]
```

---

## Tests

| Suite | Coverage |
|-------|----------|
| A | Two HTTP waves + sidecar: pipeline wins / sidecar wins / no sidecar |
| B | Dense single join / single arm local=0 |
| C | Deep Seq chain + cap clamping |
| D | Wide Seq; shared child_cap |
| E | Loser deep subtree e2e all 0 |
| F | Mixed leaf + Seq |
| G | Filters, overwrite, ties, multi-root |
| H | Detach / Snapshot / Flush idempotence |
| I | Leaf slots + multi-level tree build (optional async) |
| Combo | Depth≥3 + fan-out≥4 + loser subtree |

Requires C++11, Bazel, and pthread for the gather UT.
