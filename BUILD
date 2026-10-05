package(default_visibility = ["//visibility:public"])

cc_library(
    name = "perf_probe",
    hdrs = glob(["include/perf_probe/*.h"]),
    srcs = glob(["src/*.cc"]),
    includes = ["include"],
    copts = ["-std=c++11"],
)

cc_test(
    name = "topology_test",
    srcs = ["test/topology_test.cc"],
    deps = [
        ":perf_probe",
        "@com_google_googletest//:gtest_main",
    ],
    copts = ["-std=c++11"],
    linkopts = ["-pthread"],
    size = "small",
)
