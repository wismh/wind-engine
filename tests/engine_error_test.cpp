#include <gtest/gtest.h>

#include <engine/core/engine_error.h>

#include <array>
#include <set>
#include <string>
#include <string_view>

namespace {

constexpr std::array kAll{
        engine::EngineError::VideoInit,
        engine::EngineError::WindowCreate,
        engine::EngineError::AudioInit,
        engine::EngineError::HapticsInit,
        engine::EngineError::AssetsRoot,
        engine::EngineError::EngineCatalog,
        engine::EngineError::UiFont,
        engine::EngineError::GameCatalog,
};

}

TEST(EngineError, ToStringIsTheEnumeratorName) {
    EXPECT_EQ(engine::to_string(engine::EngineError::VideoInit), "VideoInit");
    EXPECT_EQ(engine::to_string(engine::EngineError::WindowCreate), "WindowCreate");
    EXPECT_EQ(engine::to_string(engine::EngineError::AudioInit), "AudioInit");
    EXPECT_EQ(engine::to_string(engine::EngineError::HapticsInit), "HapticsInit");
    EXPECT_EQ(engine::to_string(engine::EngineError::AssetsRoot), "AssetsRoot");
    EXPECT_EQ(engine::to_string(engine::EngineError::EngineCatalog), "EngineCatalog");
    EXPECT_EQ(engine::to_string(engine::EngineError::UiFont), "UiFont");
    EXPECT_EQ(engine::to_string(engine::EngineError::GameCatalog), "GameCatalog");
}

TEST(EngineError, EveryCauseHasItsOwnNameAndMessage) {
    std::set<std::string_view> names;
    std::set<std::string_view> messages;
    for (const engine::EngineError error : kAll) {
        EXPECT_NE(engine::to_string(error), "Unknown");
        EXPECT_FALSE(engine::message(error).empty());
        EXPECT_NE(engine::message(error), "Unknown startup error");
        names.insert(engine::to_string(error));
        messages.insert(engine::message(error));
    }
    EXPECT_EQ(names.size(), kAll.size());
    EXPECT_EQ(messages.size(), kAll.size());
}

TEST(EngineError, MessagesNameTheCause) {
    EXPECT_EQ(engine::message(engine::EngineError::AssetsRoot), "Assets root is missing");
    EXPECT_EQ(engine::message(engine::EngineError::EngineCatalog), "Failed to load engine catalog");
    EXPECT_EQ(engine::message(engine::EngineError::UiFont), "Failed to load UI font");
    EXPECT_EQ(engine::message(engine::EngineError::GameCatalog), "Failed to load game catalog");
    EXPECT_NE(engine::message(engine::EngineError::VideoInit).find("video"), std::string_view::npos);
    EXPECT_NE(engine::message(engine::EngineError::WindowCreate).find("window"), std::string_view::npos);
    EXPECT_NE(engine::message(engine::EngineError::AudioInit).find("audio"), std::string_view::npos);
    EXPECT_NE(engine::message(engine::EngineError::HapticsInit).find("haptics"), std::string_view::npos);
}

TEST(EngineError, AnOutOfRangeValueIsUnknown) {
    const auto bogus = static_cast<engine::EngineError>(999);
    EXPECT_EQ(engine::to_string(bogus), "Unknown");
    EXPECT_EQ(engine::message(bogus), "Unknown startup error");
}
