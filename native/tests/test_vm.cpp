#include "mf_test.h"

#include <string>
#include <vector>

#include "compile_util.h"

namespace {

bool setupVM(const std::string& source, mfvm::VM& vm, testutil::RecordingHost& host,
             mfc::DiagnosticSink& sink) {
  const testutil::Compilation compilation = testutil::compileScript(source, sink);
  if (!compilation.ok) {
    std::cout << sink.formatText() << "\n";
    return false;
  }
  std::string error;
  if (!vm.loadModule(compilation.module, &error)) {
    std::cout << "load failed: " << error << "\n";
    return false;
  }
  host.vm = &vm;
  vm.setHost(&host);
  return true;
}

}  // namespace

MF_TEST(vm_runs_arithmetic_branches_and_loops) {
  mfc::DiagnosticSink sink;
  mfvm::VM vm;
  testutil::RecordingHost host;
  MF_CHECK(setupVM(R"(
    quest q {
      on start {
        let x = 1 + 2 * 3;
        if (x == 7) { emit Ok(x); } else { emit Bad(x); }
        let i = 0;
        while (i < 3) { i = i + 1; }
        emit LoopDone(i);
      }
    }
  )",
                   vm, host, sink));
  vm.startQuest();
  vm.runUntilAllIdle();
  MF_CHECK(vm.lastError().empty());
  MF_CHECK_EQ(host.emits.size(), static_cast<size_t>(2));
  if (host.emits.size() == 2) {
    MF_CHECK_STR_EQ(host.emits[0], "Ok");
    MF_CHECK_STR_EQ(host.emits[1], "LoopDone");
  }
}

MF_TEST(vm_filters_events_with_predicates) {
  mfc::DiagnosticSink sink;
  mfvm::VM vm;
  testutil::RecordingHost host;
  MF_CHECK(setupVM(R"(
    quest q {
      on start {
        wait event(Kill, target == "Wolf");
        emit WolfKilled();
        wait event(Talk, npc == "Elder");
        emit Done();
      }
    }
  )",
                   vm, host, sink));
  testutil::runQuest(vm, host,
                     {{"Kill", {{"target", "Boar"}}},
                      {"Kill", {{"target", "Wolf"}}},
                      {"Talk", {{"npc", "Innkeeper"}}},
                      {"Talk", {{"npc", "Elder"}}}});
  MF_CHECK(vm.lastError().empty());
  MF_CHECK_EQ(host.emits.size(), static_cast<size_t>(2));
  if (host.emits.size() == 2) {
    MF_CHECK_STR_EQ(host.emits[0], "WolfKilled");
    MF_CHECK_STR_EQ(host.emits[1], "Done");
  }
}

MF_TEST(vm_numeric_predicate_fields) {
  mfc::DiagnosticSink sink;
  mfvm::VM vm;
  testutil::RecordingHost host;
  MF_CHECK(setupVM(R"(
    quest q {
      on start {
        wait event(Kill, count >= 2);
        emit BigKill();
      }
    }
  )",
                   vm, host, sink));
  host.vm = &vm;
  vm.setHost(&host);
  vm.startQuest();
  vm.runUntilAllIdle();
  vm.emitEvent("Kill", {{"count", mfvm::Value::integer(1)}});
  vm.runUntilAllIdle();
  MF_CHECK(host.emits.empty());
  vm.emitEvent("Kill", {{"count", mfvm::Value::integer(2)}});
  vm.runUntilAllIdle();
  MF_CHECK_EQ(host.emits.size(), static_cast<size_t>(1));
  MF_CHECK_STR_EQ(host.emits[0], "BigKill");
}

MF_TEST(vm_waits_for_timers) {
  mfc::DiagnosticSink sink;
  mfvm::VM vm;
  testutil::RecordingHost host;
  MF_CHECK(setupVM("quest q { on start { wait time(1.5); emit Alive(); } }", vm, host, sink));
  vm.startQuest();
  vm.runUntilAllIdle();
  MF_CHECK_EQ(host.emits.size(), static_cast<size_t>(1));
  MF_CHECK_EQ(vm.nowMicros(), static_cast<int64_t>(1500000));
  MF_CHECK_STR_EQ(host.emits[0], "Alive");
}

MF_TEST(vm_short_circuits_logical_operators) {
  mfc::DiagnosticSink sink;
  mfvm::VM vm;
  testutil::RecordingHost host;
  MF_CHECK(setupVM(R"(
    quest q {
      on start {
        let ok = true || (1 / 0 > 0);
        let safe = false && (1 / 0 > 0);
        emit Result(ok, safe);
      }
    }
  )",
                   vm, host, sink));
  vm.startQuest();
  vm.runUntilAllIdle();
  MF_CHECK_STR_EQ(vm.lastError(), "");
  MF_CHECK_EQ(host.emits.size(), static_cast<size_t>(1));
}

MF_TEST(vm_runs_listener_handlers) {
  mfc::DiagnosticSink sink;
  mfvm::VM vm;
  testutil::RecordingHost host;
  MF_CHECK(setupVM(R"(
    quest q {
      on start { emit Started(); }
      on event(Kill, target == "Wolf") { emit SawWolf(); }
    }
  )",
                   vm, host, sink));
  testutil::runQuest(vm, host,
                     {{"Kill", {{"target", "Boar"}}}, {"Kill", {{"target", "Wolf"}}}});
  MF_CHECK_EQ(host.emits.size(), static_cast<size_t>(2));
  if (host.emits.size() == 2) {
    MF_CHECK_STR_EQ(host.emits[0], "Started");
    MF_CHECK_STR_EQ(host.emits[1], "SawWolf");
  }
}

MF_TEST(vm_faults_on_division_by_zero) {
  mfc::DiagnosticSink sink;
  mfvm::VM vm;
  testutil::RecordingHost host;
  MF_CHECK(setupVM("quest q { on start { let x = 1 / 0; emit Bad(x); } }", vm, host, sink));
  vm.startQuest();
  const mfvm::RunStats stats = vm.runUntilAllIdle();
  MF_CHECK_EQ(stats.faults, static_cast<int64_t>(1));
  MF_CHECK(vm.lastError().find("division by zero") != std::string::npos);
  MF_CHECK(host.emits.empty());
}

MF_TEST(vm_calls_host_functions) {
  mfc::DiagnosticSink sink;
  mfvm::VM vm;
  testutil::RecordingHost host;
  MF_CHECK(setupVM(R"(
    quest q {
      on start {
        call GrantReward(100, "IronSword");
        emit Done();
      }
    }
  )",
                   vm, host, sink));
  vm.startQuest();
  vm.runUntilAllIdle();
  MF_CHECK_EQ(host.calls.size(), static_cast<size_t>(1));
  if (!host.calls.empty()) MF_CHECK_STR_EQ(host.calls[0], "GrantReward(100, IronSword)");
}

MF_TEST(vm_runs_hundred_thousand_coroutines) {
  mfc::DiagnosticSink sink;
  const testutil::Compilation compilation =
      testutil::compileScript("quest q { on start { } }", sink);
  MF_CHECK(compilation.ok);
  if (!compilation.ok) return;
  mfvm::VM vm;
  std::string error;
  MF_CHECK(vm.loadModule(compilation.module, &error));
  constexpr int kInstances = 100000;
  for (int i = 0; i < kInstances; ++i) {
    MF_CHECK(vm.startChunk("q/start") != 0);
  }
  const mfvm::RunStats stats = vm.runUntilIdle(20000000);
  MF_CHECK(!stats.budgetExceeded);
  MF_CHECK_EQ(stats.faults, static_cast<int64_t>(0));
  MF_CHECK_EQ(vm.liveInstanceCount(), static_cast<size_t>(0));
}
