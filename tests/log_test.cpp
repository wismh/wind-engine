#include <gtest/gtest.h>

#include <engine/log.h>

TEST(Log, NullSinkDoesNotCrash) {
    engine::log::init();
    engine::log::info("info");
    engine::log::warn("warn");
    engine::log::error("error");
    SUCCEED();
}
