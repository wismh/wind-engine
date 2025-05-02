#include <gtest/gtest.h>

#include "process/line_splitter.h"
#include "process/process_call_state.h"
#include "process/process_command_line.h"

#include <engine/process/process_call.h>
#include <engine/process/process_launcher.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

std::vector<std::string> split(std::initializer_list<std::string_view> chunks, bool finish = true) {
    engine::LineSplitter splitter;
    std::vector<std::string> lines;
    for (const std::string_view chunk : chunks) {
        splitter.feed(chunk, lines);
    }
    if (finish) {
        splitter.finish(lines);
    }
    return lines;
}

// Polls until `call` has a result or ten seconds pass, collecting its output on the way.
std::optional<engine::ProcessResult> finish(
        engine::ProcessLauncher& launcher, engine::ProcessCall& call, std::vector<std::string>& lines) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        launcher.poll();
        for (std::string& line : call.take_output()) {
            lines.push_back(std::move(line));
        }
        if (std::optional<engine::ProcessResult> result = call.take()) {
            for (std::string& line : call.take_output()) {
                lines.push_back(std::move(line));
            }
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return std::nullopt;
}

engine::ProcessDesc cmd(std::vector<std::string> arguments) {
    arguments.insert(arguments.begin(), "/c");
    return engine::ProcessDesc{.program = "cmd.exe", .arguments = std::move(arguments)};
}

}

TEST(LineSplitter, CutsLinesAcrossChunksAndDropsCarriageReturns) {
    EXPECT_EQ(split({"one\r\ntw", "o\n", "thr", "ee"}), (std::vector<std::string>{"one", "two", "three"}));
    EXPECT_EQ(split({"\n\r\n"}), (std::vector<std::string>{"", ""}));
    EXPECT_EQ(split({"a\r", "\nb"}), (std::vector<std::string>{"a", "b"}));
}

TEST(LineSplitter, KeepsTheUnfinishedLineUntilFinish) {
    EXPECT_EQ(split({"done\nhalf"}, false), (std::vector<std::string>{"done"}));
    EXPECT_TRUE(split({}).empty());
}

TEST(ProcessCommandLine, QuotesOnlyWhatNeedsIt) {
    EXPECT_EQ(engine::quote_windows_argument("--build"), "--build");
    EXPECT_EQ(engine::quote_windows_argument("C:\\a\\b"), "C:\\a\\b");
    EXPECT_EQ(engine::quote_windows_argument(""), "\"\"");
    EXPECT_EQ(engine::quote_windows_argument("a b"), "\"a b\"");
    EXPECT_EQ(engine::quote_windows_argument("tab\there"), "\"tab\there\"");
}

TEST(ProcessCommandLine, EscapesQuotesAndTheBackslashesBeforeThem) {
    EXPECT_EQ(engine::quote_windows_argument("say \"hi\""), "\"say \\\"hi\\\"\"");
    EXPECT_EQ(engine::quote_windows_argument("a\\\"b"), "\"a\\\\\\\"b\"");
    // A trailing backslash would escape the closing quote.
    EXPECT_EQ(engine::quote_windows_argument("C:\\my dir\\"), "\"C:\\my dir\\\\\"");
    EXPECT_EQ(engine::quote_windows_argument("a\\\\b c"), "\"a\\\\b c\"");
}

TEST(ProcessCommandLine, QuotesTheProgramAndJoinsTheArguments) {
    const std::vector<std::string> arguments{"--build", "C:/my build", "--config", "DebugGame"};
    EXPECT_EQ(engine::windows_command_line("C:/Program Files/CMake/bin/cmake.exe", arguments),
            "\"C:/Program Files/CMake/bin/cmake.exe\" --build \"C:/my build\" --config DebugGame");
    EXPECT_EQ(engine::windows_command_line("cmd.exe", {}), "\"cmd.exe\"");
}

TEST(ProcessEnvironment, ReplacesByNameAndAppendsTheRest) {
    const std::vector<engine::ProcessVariable> set{{"PATH", "C:\\tools"}, {"VSLANG", "1033"}};
    EXPECT_EQ(engine::merge_environment({"Path=C:\\Windows", "=C:=C:\\work", "TEMP=C:\\t"}, set, true),
            (std::vector<std::string>{"PATH=C:\\tools", "=C:=C:\\work", "TEMP=C:\\t", "VSLANG=1033"}));
    EXPECT_EQ(engine::merge_environment({"Path=/bin"}, set, false),
            (std::vector<std::string>{"Path=/bin", "PATH=C:\\tools", "VSLANG=1033"}));
}

TEST(ProcessEnvironment, NamesStartingWithEqualsAreNotTheEmptyName) {
    const std::vector<engine::ProcessVariable> set{{"=C:", "D:\\other"}};
    EXPECT_EQ(engine::merge_environment({"=C:=C:\\work"}, set, true), (std::vector<std::string>{"=C:=D:\\other"}));
}

TEST(ProcessCall, EmptyCallHasNothing) {
    engine::ProcessCall call;
    EXPECT_FALSE(call.pending());
    EXPECT_TRUE(call.take_output().empty());
    EXPECT_FALSE(call.take().has_value());
    call.cancel();
}

TEST(ProcessCall, ResolvedCallHandsTheResultOnceAndKeepsOutputForTakeOutput) {
    engine::ProcessCall call = engine::ProcessCall::resolved(engine::ProcessExit{.code = 2}, {"a", "b"});
    EXPECT_FALSE(call.pending());
    const std::optional<engine::ProcessResult> result = call.take();
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ((*result)->code, 2);
    EXPECT_FALSE(call.take().has_value());
    EXPECT_FALSE(call.pending());
    EXPECT_EQ(call.take_output(), (std::vector<std::string>{"a", "b"}));
    EXPECT_TRUE(call.take_output().empty());
}

TEST(ProcessCall, CancelAndDestroyMarkThePendingState) {
    auto state = std::make_shared<engine::ProcessCallState>();
    {
        engine::ProcessCall call{state};
        EXPECT_TRUE(call.pending());
    }
    EXPECT_TRUE(state->cancelled());

    auto second = std::make_shared<engine::ProcessCallState>();
    engine::ProcessCall call{second};
    call.cancel();
    EXPECT_TRUE(second->cancelled());
    EXPECT_FALSE(call.pending());
    EXPECT_FALSE(call.take().has_value());
}

TEST(ProcessCall, MoveAssignCancelsTheCallItReplaces) {
    auto first = std::make_shared<engine::ProcessCallState>();
    engine::ProcessCall call{first};
    call = engine::ProcessCall::resolved(engine::ProcessExit{});
    EXPECT_TRUE(first->cancelled());
    EXPECT_TRUE(call.take().has_value());
}

TEST(ProcessCallState, DeliversLinesOnlyOnDeliverAndDropsThemOnceCancelled) {
    engine::ProcessCallState state;
    state.push_lines({"one"});
    EXPECT_TRUE(state.output.empty());
    EXPECT_FALSE(state.deliver_output());
    EXPECT_EQ(state.output, (std::vector<std::string>{"one"}));

    state.push_lines({"two"});
    state.close_output();
    state.cancel();
    EXPECT_TRUE(state.deliver_output());
    EXPECT_EQ(state.output, (std::vector<std::string>{"one"}));
}

TEST(ProcessLauncher, StartErrorsArriveOnTheNextPoll) {
    engine::ProcessLauncher launcher;
    engine::ProcessCall call = launcher.run({.program = "wind-no-such-program-176", .arguments = {}});
    EXPECT_TRUE(call.pending());
    EXPECT_FALSE(call.take().has_value());
    launcher.poll();
    const std::optional<engine::ProcessResult> result = call.take();
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->has_value());
#if defined(_WIN32)
    EXPECT_EQ(result->error(), engine::ProcessError::NotFound);
#else
    EXPECT_EQ(result->error(), engine::ProcessError::Unsupported);
#endif
}

TEST(ProcessLauncher, DisposeCancelsCallsStillWaitingForTheirError) {
    engine::ProcessLauncher launcher;
    engine::ProcessCall call = launcher.run({.program = "wind-no-such-program-176", .arguments = {}});
    launcher.dispose();
    launcher.poll();
    EXPECT_FALSE(call.pending());
    EXPECT_FALSE(call.take().has_value());
}

#if defined(_WIN32)

TEST(ProcessLauncher, RunsAProgramAndDeliversItsLinesAndExitCode) {
    engine::ProcessLauncher launcher;
    EXPECT_TRUE(launcher.is_supported());
    engine::ProcessCall call = launcher.run(cmd({"echo", "one&", "echo", "two&", "exit", "3"}));
    std::vector<std::string> lines;
    const std::optional<engine::ProcessResult> result = finish(launcher, call, lines);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ((*result)->code, 3);
    EXPECT_EQ(lines, (std::vector<std::string>{"one", "two"}));
    EXPECT_FALSE(call.pending());
}

TEST(ProcessLauncher, MergesStandardErrorIntoTheSameLinesInOrder) {
    engine::ProcessLauncher launcher;
    engine::ProcessCall call = launcher.run(cmd({"echo", "out&", "(echo", "err)1>&2&", "echo", "again"}));
    std::vector<std::string> lines;
    ASSERT_TRUE(finish(launcher, call, lines).has_value());
    EXPECT_EQ(lines, (std::vector<std::string>{"out", "err", "again"}));
}

TEST(ProcessLauncher, SetsVariablesOnTopOfTheParentEnvironment) {
    engine::ProcessLauncher launcher;
    engine::ProcessDesc desc = cmd({"echo", "%WIND_PROCESS_TEST%-%SystemRoot%"});
    desc.environment = {{"WIND_PROCESS_TEST", "hello"}};
    engine::ProcessCall call = launcher.run(std::move(desc));
    std::vector<std::string> lines;
    ASSERT_TRUE(finish(launcher, call, lines).has_value());
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_TRUE(lines[0].starts_with("hello-")) << lines[0];
    EXPECT_NE(lines[0], "hello-%SystemRoot%");
}

TEST(ProcessLauncher, RunsInTheWorkingDirectory) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "wind_process_test_cwd";
    std::filesystem::create_directories(directory);
    engine::ProcessLauncher launcher;
    engine::ProcessDesc desc = cmd({"cd"});
    desc.working_directory = directory;
    engine::ProcessCall call = launcher.run(std::move(desc));
    std::vector<std::string> lines;
    ASSERT_TRUE(finish(launcher, call, lines).has_value());
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_TRUE(std::filesystem::equivalent(lines[0], directory)) << lines[0];
    std::filesystem::remove_all(directory);
}

TEST(ProcessLauncher, MissingWorkingDirectoryIsStartFailed) {
    engine::ProcessLauncher launcher;
    engine::ProcessDesc desc = cmd({"cd"});
    desc.working_directory = std::filesystem::temp_directory_path() / "wind_process_test_missing" / "nope";
    engine::ProcessCall call = launcher.run(std::move(desc));
    launcher.poll();
    const std::optional<engine::ProcessResult> result = call.take();
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->has_value());
    EXPECT_EQ(result->error(), engine::ProcessError::StartFailed);
}

TEST(ProcessLauncher, ProcessesLeftRunningDoNotHoldTheCallOpen) {
    // `start /b` leaves ping running with the output pipe after cmd exits. The call still finishes with cmd.
    engine::ProcessLauncher launcher;
    const auto started = std::chrono::steady_clock::now();
    engine::ProcessCall call =
            launcher.run(cmd({"start", "/b", "ping", "-n", "30", "127.0.0.1", ">nul", "&", "echo", "done"}));
    std::vector<std::string> lines;
    const std::optional<engine::ProcessResult> result = finish(launcher, call, lines);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->has_value());
    EXPECT_EQ((*result)->code, 0);
    EXPECT_EQ(lines, (std::vector<std::string>{"done"}));
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(8));
}

TEST(ProcessLauncher, CancelEndsTheProgramAndDeliversNothing) {
    engine::ProcessLauncher launcher;
    engine::ProcessCall call = launcher.run(cmd({"echo", "first&", "ping", "-n", "30", "127.0.0.1"}));
    launcher.poll();
    call.cancel();
    EXPECT_FALSE(call.pending());
    // dispose waits for the reader thread, which only returns once every process of the job has ended.
    const auto started = std::chrono::steady_clock::now();
    launcher.poll();
    launcher.dispose();
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(8));
    EXPECT_TRUE(call.take_output().empty());
    EXPECT_FALSE(call.take().has_value());
}

TEST(ProcessLauncher, DisposeEndsRunningProgramsAndLeavesTheirCallsEmpty) {
    engine::ProcessLauncher launcher;
    engine::ProcessCall call = launcher.run(cmd({"ping", "-n", "30", "127.0.0.1"}));
    launcher.poll();
    const auto started = std::chrono::steady_clock::now();
    launcher.dispose();
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(8));
    launcher.poll();
    EXPECT_FALSE(call.pending());
    EXPECT_FALSE(call.take().has_value());
}

TEST(ProcessLauncher, LaunchStartsAnIndependentProgram) {
    engine::ProcessLauncher launcher;
    EXPECT_TRUE(launcher.launch(cmd({"exit", "0"})).has_value());
    const std::expected<void, engine::ProcessError> missing =
            launcher.launch({.program = "wind-no-such-program-176", .arguments = {}});
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error(), engine::ProcessError::NotFound);
}

#else

TEST(ProcessLauncher, IsUnsupportedOffWindows) {
    engine::ProcessLauncher launcher;
    EXPECT_FALSE(launcher.is_supported());
    const std::expected<void, engine::ProcessError> launched = launcher.launch({.program = "true", .arguments = {}});
    ASSERT_FALSE(launched.has_value());
    EXPECT_EQ(launched.error(), engine::ProcessError::Unsupported);
}

#endif
