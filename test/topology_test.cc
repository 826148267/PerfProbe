#include "perf_probe/perf_probe.h"

#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <utility>
#include <vector>

using perf_probe::ArmReport;
using perf_probe::JoinReport;
using perf_probe::Ledger;
using perf_probe::MemorySink;
using perf_probe::PerfProbe;

namespace {

// ---------- A: two waves + sidecar ----------

TEST(TopologyA, HttpWinsVsJcreator) {
    // wave1: a=100,b=80 → local=20; wave2: c=50,d=40 → local=10
    // http=150, side=100 → slack_hc=50; e2e_1st=20, e2e_2nd=10, e2e_jc=0
    PerfProbe p;
    p.Enable();
    p.Join("wave1").Arm("a", 100).Arm("b", 80).Commit();
    p.Join("wave2").Arm("c", 50).Arm("d", 40).Commit();
    {
        auto j = p.Join("vs_sidecar");
        j.Arm("http").Seq("wave1", "wave2");
        j.Arm("sidecar", 100);
        j.Commit();
    }
    MemorySink sink;
    p.Flush(&sink);

    ASSERT_NE(sink.Join("wave1"), nullptr);
    EXPECT_EQ(sink.Join("wave1")->winner_name, "a");
    EXPECT_EQ(sink.Join("wave1")->local_slack_us, 20);
    EXPECT_EQ(sink.Join("wave1")->e2e_us, 20);

    ASSERT_NE(sink.Join("wave2"), nullptr);
    EXPECT_EQ(sink.Join("wave2")->winner_name, "c");
    EXPECT_EQ(sink.Join("wave2")->e2e_us, 10);

    ASSERT_NE(sink.Join("vs_sidecar"), nullptr);
    EXPECT_EQ(sink.Join("vs_sidecar")->winner_name, "http");
    EXPECT_EQ(sink.Join("vs_sidecar")->local_slack_us, 50);
    EXPECT_EQ(sink.Join("vs_sidecar")->e2e_us, 50);

    ASSERT_NE(sink.Arm("vs_sidecar", "sidecar"), nullptr);
    EXPECT_EQ(sink.Arm("vs_sidecar", "sidecar")->e2e_us, 0);
    ASSERT_NE(sink.Arm("vs_sidecar", "http"), nullptr);
    EXPECT_EQ(sink.Arm("vs_sidecar", "http")->e2e_us, 50);
}

TEST(TopologyA, JcreatorWins) {
    PerfProbe p;
    p.Enable();
    p.Join("wave1").Arm("a", 100).Arm("b", 80).Commit();
    p.Join("wave2").Arm("c", 50).Arm("d", 40).Commit();
    {
        auto j = p.Join("vs_sidecar");
        j.Arm("http").Seq("wave1", "wave2");
        j.Arm("sidecar", 200);
        j.Commit();
    }
    MemorySink sink;
    p.Flush(&sink);

    EXPECT_EQ(sink.Join("wave1")->e2e_us, 0);
    EXPECT_EQ(sink.Join("wave2")->e2e_us, 0);
    EXPECT_EQ(sink.Join("vs_sidecar")->winner_name, "sidecar");
    EXPECT_EQ(sink.Join("vs_sidecar")->local_slack_us, 50);
    EXPECT_EQ(sink.Arm("vs_sidecar", "sidecar")->e2e_us, 50);
    EXPECT_EQ(sink.Arm("vs_sidecar", "http")->e2e_us, 0);
}

TEST(TopologyA, NoJcreator) {
    PerfProbe p;
    p.Enable();
    p.Join("wave1").Arm("a", 100).Arm("b", 80).Commit();
    p.Join("wave2").Arm("c", 50).Arm("d", 40).Commit();
    MemorySink sink;
    p.Flush(&sink);

    EXPECT_EQ(sink.Join("wave1")->e2e_us, 20);
    EXPECT_EQ(sink.Join("wave2")->e2e_us, 10);
    EXPECT_EQ(sink.Join("vs_sidecar"), nullptr);
}

// ---------- B: 单汇点稠密 ----------

TEST(TopologyB, DenseFiveArms) {
    PerfProbe p;
    p.Enable();
    p.Join("dense")
        .Arm("a", 10)
        .Arm("b", 50)
        .Arm("c", 40)
        .Arm("d", 30)
        .Arm("e", 20)
        .Commit();
    MemorySink sink;
    p.Flush(&sink);
    ASSERT_NE(sink.Join("dense"), nullptr);
    EXPECT_EQ(sink.Join("dense")->winner_name, "b");
    EXPECT_EQ(sink.Join("dense")->wall_us, 50);
    EXPECT_EQ(sink.Join("dense")->local_slack_us, 10);  // 50-40
    EXPECT_EQ(sink.Join("dense")->e2e_us, 10);
}

TEST(TopologyB, SingleArmLocalZero) {
    PerfProbe p;
    p.Enable();
    p.Join("one").Arm("only", 99).Commit();
    MemorySink sink;
    p.Flush(&sink);
    EXPECT_EQ(sink.Join("one")->local_slack_us, 0);
    EXPECT_EQ(sink.Join("one")->e2e_us, 0);
    EXPECT_EQ(sink.Arm("one", "only")->e2e_us, 0);
}

// ---------- C: 深 Seq 链 ----------

TEST(TopologyC, DeepSeqCapMin) {
    // L0: leaf winner 100 vs 90 → local=10
    // L1: Seq(L0) wall=100 vs leaf 70 → local=30, cap to L0 = min(30,∞)=30
    //     L0 e2e = min(10,30)=10
    // L2: Seq(L1) wall=100 vs leaf 40 → local=60
    //     L1 e2e = min(30,60)=30; L0 e2e = min(10,30)=10
    PerfProbe p;
    p.Enable();
    p.Join("L0").Arm("w", 100).Arm("l", 90).Commit();
    p.Join("L1").Arm("up").Seq("L0").Arm("side", 70).Commit();
    p.Join("L2").Arm("up").Seq("L1").Arm("side", 40).Commit();
    MemorySink sink;
    p.Flush(&sink);

    EXPECT_EQ(sink.Join("L2")->local_slack_us, 60);
    EXPECT_EQ(sink.Join("L2")->e2e_us, 60);
    EXPECT_EQ(sink.Join("L1")->e2e_us, 30);
    EXPECT_EQ(sink.Join("L0")->e2e_us, 10);
}

TEST(TopologyC, DeepLoserZero) {
    PerfProbe p;
    p.Enable();
    p.Join("L0").Arm("w", 100).Arm("l", 90).Commit();
    p.Join("L1").Arm("up").Seq("L0").Arm("side", 70).Commit();
    // L2: side 赢
    p.Join("L2").Arm("up").Seq("L1").Arm("side", 200).Commit();
    MemorySink sink;
    p.Flush(&sink);

    EXPECT_EQ(sink.Join("L2")->winner_name, "side");
    EXPECT_EQ(sink.Join("L1")->e2e_us, 0);
    EXPECT_EQ(sink.Join("L0")->e2e_us, 0);
    EXPECT_EQ(sink.Arm("L2", "side")->e2e_us,
              sink.Join("L2")->local_slack_us);
}

// ---------- D: 宽 Seq ----------

TEST(TopologyD, WideSeqSharedCap) {
    PerfProbe p;
    p.Enable();
    p.Join("c0").Arm("a", 10).Arm("b", 5).Commit();   // local=5
    p.Join("c1").Arm("a", 20).Arm("b", 8).Commit();   // local=12
    p.Join("c2").Arm("a", 30).Arm("b", 25).Commit();  // local=5
    // seq wall = 10+20+30=60 vs leaf 40 → local=20; 子共享 cap=20
    p.Join("wide").Arm("bundle").Seq("c0", "c1", "c2").Arm("leaf", 40).Commit();
    MemorySink sink;
    p.Flush(&sink);

    EXPECT_EQ(sink.Join("wide")->wall_us, 60);
    EXPECT_EQ(sink.Join("wide")->local_slack_us, 20);
    EXPECT_EQ(sink.Join("c0")->e2e_us, 5);   // min(5,20)
    EXPECT_EQ(sink.Join("c1")->e2e_us, 12);  // min(12,20)
    EXPECT_EQ(sink.Join("c2")->e2e_us, 5);   // min(5,20)
}

// ---------- E: 输家子树 ----------

TEST(TopologyE, LoserDeepSubtreeZero) {
    PerfProbe p;
    p.Enable();
    p.Join("win_leaf").Arm("x", 50).Arm("y", 40).Commit();
    p.Join("lose_deep").Arm("p", 30).Arm("q", 20).Commit();
    p.Join("lose_mid").Arm("up").Seq("lose_deep").Arm("s", 10).Commit();
    p.Join("root")
        .Arm("W").Seq("win_leaf")
        .Arm("L").Seq("lose_mid")
        .Commit();
    MemorySink sink;
    p.Flush(&sink);

    EXPECT_EQ(sink.Join("root")->winner_name, "W");
    EXPECT_EQ(sink.Join("win_leaf")->e2e_us, 10);  // min(10, root local=20? wall W=50 L=30 →20)
    EXPECT_EQ(sink.Join("root")->local_slack_us, 20);
    EXPECT_EQ(sink.Join("lose_mid")->e2e_us, 0);
    EXPECT_EQ(sink.Join("lose_deep")->e2e_us, 0);
}

// ---------- F: 混合叶子 + Seq ----------

TEST(TopologyF, MixedLeafAndSeq) {
    PerfProbe p;
    p.Enable();
    p.Join("sub").Arm("a", 40).Arm("b", 30).Commit();
    p.Join("mix").Arm("seq").Seq("sub").Arm("leaf", 55).Commit();
    MemorySink sink;
    p.Flush(&sink);

    EXPECT_EQ(sink.Join("mix")->winner_name, "leaf");
    EXPECT_EQ(sink.Join("mix")->local_slack_us, 15);  // 55-40
    EXPECT_EQ(sink.Join("sub")->e2e_us, 0);
    EXPECT_EQ(sink.Arm("mix", "leaf")->e2e_us, 15);
}

// ---------- G: 边界 ----------

TEST(TopologyG, NotEnabledNoEmit) {
    PerfProbe p;
    p.Join("x").Arm("a", 10).Arm("b", 5).Commit();
    MemorySink sink;
    p.Flush(&sink);
    EXPECT_TRUE(sink.joins.empty());
}

TEST(TopologyG, UncommittedChildWallZero) {
    PerfProbe p;
    p.Enable();
    // 不 Commit child
    p.Join("parent").Arm("s").Seq("missing").Arm("leaf", 10).Commit();
    MemorySink sink;
    p.Flush(&sink);
    EXPECT_EQ(sink.Join("parent")->winner_name, "leaf");
    EXPECT_EQ(sink.Join("parent")->wall_us, 10);
}

TEST(TopologyG, EmptyArms) {
    PerfProbe p;
    p.Enable();
    p.Join("empty").Commit();
    MemorySink sink;
    p.Flush(&sink);
    ASSERT_NE(sink.Join("empty"), nullptr);
    EXPECT_EQ(sink.Join("empty")->wall_us, 0);
    EXPECT_EQ(sink.Join("empty")->local_slack_us, 0);
    EXPECT_TRUE(sink.Join("empty")->winner_name.empty());
}

TEST(TopologyG, DuplicateJoinNameOverwrite) {
    PerfProbe p;
    p.Enable();
    p.Join("j").Arm("a", 10).Arm("b", 5).Commit();
    p.Join("j").Arm("c", 100).Arm("d", 1).Commit();
    MemorySink sink;
    p.Flush(&sink);
    EXPECT_EQ(sink.Join("j")->winner_name, "c");
    EXPECT_EQ(sink.Join("j")->local_slack_us, 99);
}

TEST(TopologyG, TieKeepsFirstWinner) {
    // wall 相等：strict >，先登记者保持 winner
    PerfProbe p;
    p.Enable();
    p.Join("tie").Arm("first", 50).Arm("second", 50).Commit();
    MemorySink sink;
    p.Flush(&sink);
    EXPECT_EQ(sink.Join("tie")->winner_name, "first");
    EXPECT_EQ(sink.Join("tie")->local_slack_us, 0);
    EXPECT_EQ(sink.Join("tie")->e2e_us, 0);
    EXPECT_TRUE(sink.Arm("tie", "first")->is_winner);
    EXPECT_FALSE(sink.Arm("tie", "second")->is_winner);
}

TEST(TopologyG, SkipEmptyNameAndNonPositiveUs) {
    PerfProbe p;
    p.Enable();
    p.Join("filt")
        .Arm("", 100)
        .Arm("ok", 80)
        .Arm("zero", 0)
        .Arm("neg", -5)
        .Arm("top", 90)
        .Commit();
    MemorySink sink;
    p.Flush(&sink);
    EXPECT_EQ(sink.Join("filt")->winner_name, "top");
    EXPECT_EQ(sink.Join("filt")->local_slack_us, 10);  // 90-80
    EXPECT_EQ(sink.Arm("filt", ""), nullptr);
    EXPECT_EQ(sink.Arm("filt", "zero"), nullptr);
    EXPECT_EQ(sink.Arm("filt", "neg"), nullptr);
    ASSERT_NE(sink.Arm("filt", "ok"), nullptr);
    ASSERT_NE(sink.Arm("filt", "top"), nullptr);
}

TEST(TopologyG, SkipEmptySeqArmName) {
    PerfProbe p;
    p.Enable();
    p.Join("child").Arm("a", 10).Arm("b", 5).Commit();
    p.Join("parent").Arm("").Seq("child").Arm("leaf", 20).Commit();
    MemorySink sink;
    p.Flush(&sink);
    EXPECT_EQ(sink.Join("parent")->winner_name, "leaf");
    EXPECT_EQ(sink.Join("parent")->wall_us, 20);
    EXPECT_EQ(sink.Arm("parent", ""), nullptr);
}

TEST(TopologyG, MultiIndependentRoots) {
    // 两个互不引用的汇点都是根，各自 cap=∞
    PerfProbe p;
    p.Enable();
    p.Join("r1").Arm("a", 30).Arm("b", 10).Commit();  // local=20
    p.Join("r2").Arm("c", 40).Arm("d", 25).Commit();  // local=15
    MemorySink sink;
    p.Flush(&sink);
    EXPECT_EQ(sink.Join("r1")->e2e_us, 20);
    EXPECT_EQ(sink.Join("r2")->e2e_us, 15);
}

// ---------- H: 异步 Detach / 契约 ----------

TEST(TopologyH, DetachFlushMatchesSync) {
    PerfProbe p;
    p.Enable();
    p.Join("wave1").Arm("a", 100).Arm("b", 80).Commit();
    p.Join("wave2").Arm("c", 50).Arm("d", 40).Commit();
    {
        auto j = p.Join("vs_sidecar");
        j.Arm("http").Seq("wave1", "wave2");
        j.Arm("sidecar", 100);
        j.Commit();
    }

    MemorySink sync_sink;
    p.Snapshot().Flush(&sync_sink);

    Ledger ledger = p.Detach();
    MemorySink async_sink;
    ledger.Flush(&async_sink);

    ASSERT_EQ(sync_sink.joins.size(), async_sink.joins.size());
    for (auto it = sync_sink.joins.begin(); it != sync_sink.joins.end(); ++it) {
        const JoinReport* a = async_sink.Join(it->first);
        ASSERT_NE(a, nullptr) << it->first;
        EXPECT_EQ(a->winner_name, it->second.winner_name);
        EXPECT_EQ(a->e2e_us, it->second.e2e_us);
        EXPECT_EQ(a->local_slack_us, it->second.local_slack_us);
        EXPECT_EQ(a->wall_us, it->second.wall_us);
    }
    ASSERT_EQ(sync_sink.arms.size(), async_sink.arms.size());
    for (auto it = sync_sink.arms.begin(); it != sync_sink.arms.end(); ++it) {
        auto ait = async_sink.arms.find(it->first);
        ASSERT_TRUE(ait != async_sink.arms.end());
        EXPECT_EQ(ait->second.e2e_us, it->second.e2e_us);
        EXPECT_EQ(ait->second.is_winner, it->second.is_winner);
    }
}

TEST(TopologyH, DetachSealsFurtherWrites) {
    PerfProbe p;
    p.Enable();
    p.Join("old").Arm("a", 10).Arm("b", 5).Commit();
    Ledger ledger = p.Detach();

    // Detach 后封印：即使再 Enable 也不能写入
    p.Enable();
    p.Join("new").Arm("x", 100).Arm("y", 1).Commit();
    MemorySink sealed_sink;
    p.Flush(&sealed_sink);
    EXPECT_TRUE(sealed_sink.joins.empty());

    MemorySink ledger_sink;
    ledger.Flush(&ledger_sink);
    ASSERT_NE(ledger_sink.Join("old"), nullptr);
    EXPECT_EQ(ledger_sink.Join("old")->e2e_us, 5);
    EXPECT_EQ(ledger_sink.Join("new"), nullptr);
}

TEST(TopologyH, FlushIdempotent) {
    PerfProbe p;
    p.Enable();
    p.Join("wave1").Arm("a", 100).Arm("b", 80).Commit();
    p.Join("wave2").Arm("c", 50).Arm("d", 40).Commit();
    {
        auto j = p.Join("vs");
        j.Arm("http").Seq("wave1", "wave2");
        j.Arm("jc", 100);
        j.Commit();
    }
    MemorySink first;
    MemorySink second;
    p.Flush(&first);
    p.Flush(&second);

    ASSERT_EQ(first.joins.size(), second.joins.size());
    for (auto it = first.joins.begin(); it != first.joins.end(); ++it) {
        const JoinReport* b = second.Join(it->first);
        ASSERT_NE(b, nullptr);
        EXPECT_EQ(b->e2e_us, it->second.e2e_us);
        EXPECT_EQ(b->winner_name, it->second.winner_name);
        EXPECT_EQ(b->local_slack_us, it->second.local_slack_us);
        EXPECT_EQ(b->wall_us, it->second.wall_us);
    }
    ASSERT_EQ(first.arms.size(), second.arms.size());
    for (auto it = first.arms.begin(); it != first.arms.end(); ++it) {
        auto bit = second.arms.find(it->first);
        ASSERT_TRUE(bit != second.arms.end());
        EXPECT_EQ(bit->second.e2e_us, it->second.e2e_us);
    }
}

TEST(TopologyH, SnapshotIndependence) {
    PerfProbe p;
    p.Enable();
    p.Join("j").Arm("a", 20).Arm("b", 10).Commit();
    Ledger snap = p.Snapshot();

    // 原实例继续改写；快照不受影响
    p.Join("j").Arm("c", 100).Arm("d", 1).Commit();
    MemorySink live;
    p.Flush(&live);
    MemorySink frozen;
    snap.Flush(&frozen);

    EXPECT_EQ(live.Join("j")->winner_name, "c");
    EXPECT_EQ(frozen.Join("j")->winner_name, "a");
    EXPECT_EQ(frozen.Join("j")->e2e_us, 10);
}

// ---------- I: 槽只采叶子耗时；拓扑在记账时声明 ----------
// 扁平 slots 不编码树结构。并行只写叶子 wall；汇齐后（可异步）
// 用 Join / Seq 建造拓扑，再用槽填叶子 Arm。

TEST(TopologyI, GatherThenSerialArm) {
    // 叶子槽下标（拓扑不在这里）
    enum { W1A = 0, W1B, W2C, W2D, SIDE, kN };
    const int64_t leaf_us[kN] = {100, 80, 50, 40, 100};
    // wave1 local=20, wave2 local=10; http=150 vs side=100 → sidecar e2e=0

    std::vector<int64_t> slots(kN, 0);
    std::vector<std::thread> workers;
    workers.reserve(kN);
    for (int i = 0; i < kN; ++i) {
        workers.push_back(std::thread([&, i]() {
            slots[i] = leaf_us[i];  // 只写自己的槽
        }));
    }
    for (int i = 0; i < kN; ++i) {
        workers[i].join();
    }

    // 单线程：声明树 + 用槽填叶子
    PerfProbe p;
    p.Enable();
    p.Join("wave1").Arm("a", slots[W1A]).Arm("b", slots[W1B]).Commit();
    p.Join("wave2").Arm("c", slots[W2C]).Arm("d", slots[W2D]).Commit();
    {
        auto j = p.Join("vs_sidecar");
        j.Arm("http").Seq("wave1", "wave2");  // 结构在此，不在 slots
        j.Arm("sidecar", slots[SIDE]);
        j.Commit();
    }

    MemorySink sink;
    p.Flush(&sink);
    ASSERT_NE(sink.Join("wave1"), nullptr);
    EXPECT_EQ(sink.Join("wave1")->winner_name, "a");
    EXPECT_EQ(sink.Join("wave1")->e2e_us, 20);
    EXPECT_EQ(sink.Join("wave2")->e2e_us, 10);
    EXPECT_EQ(sink.Join("vs_sidecar")->winner_name, "http");
    EXPECT_EQ(sink.Join("vs_sidecar")->local_slack_us, 50);
    EXPECT_EQ(sink.Arm("vs_sidecar", "sidecar")->e2e_us, 0);
}

TEST(TopologyI, SlotsOnlyAsyncBuild) {
    // 热路径只留下叶子槽；另一线程声明拓扑并 Flush
    enum { W1A = 0, W1B, W2C, W2D, SIDE, kN };
    std::vector<int64_t> slots(kN);
    slots[W1A] = 100;
    slots[W1B] = 80;
    slots[W2C] = 50;
    slots[W2D] = 40;
    slots[SIDE] = 100;

    MemorySink sink;
    std::thread async_worker([&]() {
        PerfProbe p;
        p.Enable();
        p.Join("wave1").Arm("a", slots[W1A]).Arm("b", slots[W1B]).Commit();
        p.Join("wave2").Arm("c", slots[W2C]).Arm("d", slots[W2D]).Commit();
        {
            auto j = p.Join("vs_sidecar");
            j.Arm("http").Seq("wave1", "wave2");
            j.Arm("sidecar", slots[SIDE]);
            j.Commit();
        }
        p.Flush(&sink);
    });
    async_worker.join();

    EXPECT_EQ(sink.Join("wave1")->e2e_us, 20);
    EXPECT_EQ(sink.Join("wave2")->e2e_us, 10);
    EXPECT_EQ(sink.Join("vs_sidecar")->winner_name, "http");
    EXPECT_EQ(sink.Arm("vs_sidecar", "sidecar")->e2e_us, 0);
}

// ---------- C 加深 / D 加宽 ----------

TEST(TopologyC, DeeperFourLevelsCapClamp) {
    // L0 local=10; L1 local=30 → L0 e2e=10
    // L2 local=5  → L1 e2e=min(30,5)=5, L0 e2e=min(10,5)=5
    // L3 local=100 → L2 e2e=5, … L0 仍 5
    PerfProbe p;
    p.Enable();
    p.Join("L0").Arm("w", 100).Arm("l", 90).Commit();
    p.Join("L1").Arm("up").Seq("L0").Arm("side", 70).Commit();
    p.Join("L2").Arm("up").Seq("L1").Arm("side", 95).Commit();  // wall up=100, local=5
    p.Join("L3").Arm("up").Seq("L2").Arm("side", 1).Commit();   // local=99
    MemorySink sink;
    p.Flush(&sink);

    EXPECT_EQ(sink.Join("L3")->e2e_us, 99);
    EXPECT_EQ(sink.Join("L2")->e2e_us, 5);
    EXPECT_EQ(sink.Join("L1")->e2e_us, 5);
    EXPECT_EQ(sink.Join("L0")->e2e_us, 5);
}

TEST(TopologyD, SeqFourChildrenSharedCap) {
    PerfProbe p;
    p.Enable();
    p.Join("c0").Arm("a", 10).Arm("b", 4).Commit();   // local=6
    p.Join("c1").Arm("a", 20).Arm("b", 5).Commit();   // local=15
    p.Join("c2").Arm("a", 30).Arm("b", 28).Commit();  // local=2
    p.Join("c3").Arm("a", 40).Arm("b", 10).Commit();  // local=30
    // Σ=100 vs leaf 70 → local=30; 子共享 cap=30
    p.Join("wide4")
        .Arm("bundle").Seq("c0", "c1", "c2", "c3")
        .Arm("leaf", 70)
        .Commit();
    MemorySink sink;
    p.Flush(&sink);

    EXPECT_EQ(sink.Join("wide4")->wall_us, 100);
    EXPECT_EQ(sink.Join("wide4")->local_slack_us, 30);
    EXPECT_EQ(sink.Join("c0")->e2e_us, 6);
    EXPECT_EQ(sink.Join("c1")->e2e_us, 15);
    EXPECT_EQ(sink.Join("c2")->e2e_us, 2);
    EXPECT_EQ(sink.Join("c3")->e2e_us, 30);  // min(30,30)
}

// ---------- 综合：深≥3 + 扇出≥4 + 输家子树 ----------

TEST(TopologyCombo, DeepWideLoser) {
    PerfProbe p;
    p.Enable();
    // 底层宽扇出 4
    p.Join("fan")
        .Arm("f0", 40)
        .Arm("f1", 35)
        .Arm("f2", 30)
        .Arm("f3", 25)
        .Commit();  // winner f0 local=5
    p.Join("mid").Arm("up").Seq("fan").Arm("side", 10).Commit();  // wall=40 local=30
    // 输家深链
    p.Join("lx").Arm("a", 8).Arm("b", 7).Commit();
    p.Join("ly").Arm("up").Seq("lx").Arm("s", 1).Commit();
    // 根：赢家 mid(40) vs 输家 ly(8)
    p.Join("root").Arm("W").Seq("mid").Arm("L").Seq("ly").Commit();
    MemorySink sink;
    p.Flush(&sink);

    EXPECT_EQ(sink.Join("root")->winner_name, "W");
    EXPECT_EQ(sink.Join("root")->local_slack_us, 32);  // 40-8
    EXPECT_EQ(sink.Join("mid")->e2e_us, 30);           // min(30,32)
    EXPECT_EQ(sink.Join("fan")->e2e_us, 5);            // min(5,30)
    EXPECT_EQ(sink.Join("ly")->e2e_us, 0);
    EXPECT_EQ(sink.Join("lx")->e2e_us, 0);
}

}  // namespace
