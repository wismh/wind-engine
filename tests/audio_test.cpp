#include <gtest/gtest.h>

#include <engine/audio/audio_system.h>
#include <engine/audio/sound.h>

#include <memory>

namespace {

engine::Sound make_sound(float volume = 1.f) {
    engine::Sound sound;
    sound.clip = std::make_shared<engine::Audio>();
    sound.volume = volume;
    return sound;
}

}

TEST(Audio, GainFormula) {
    EXPECT_FLOAT_EQ(engine::audio::final_gain(1.f, 1.f, 1.f), 1.f);
    EXPECT_FLOAT_EQ(engine::audio::final_gain(0.5f, 0.5f, 0.5f), 0.125f);
    EXPECT_FLOAT_EQ(engine::audio::final_gain(0.f, 1.f, 1.f), 0.f);
    EXPECT_FLOAT_EQ(engine::audio::final_gain(2.f, 1.f, 1.f), 1.f);
    EXPECT_FLOAT_EQ(engine::audio::final_gain(1.f, 1.f, 2.f), 1.f);
    EXPECT_FLOAT_EQ(engine::audio::final_gain(-1.f, 1.f, 1.f), 0.f);

    engine::AudioSystem audio;
    ASSERT_TRUE(audio.init());
    audio.set_master_volume(0.5f);
    audio.set_sfx_volume(0.5f);
    audio.play_sfx(make_sound(0.5f), 1.f);
    EXPECT_FLOAT_EQ(audio.last_sfx_gain(), engine::audio::final_gain(0.5f, 0.5f, 0.5f));
    EXPECT_FLOAT_EQ(audio.last_sfx_gain(), 0.125f);

    audio.set_master_volume(1.f);
    audio.set_sfx_volume(1.f);
    audio.play_sfx(make_sound(0.5f), 0.5f);
    EXPECT_FLOAT_EQ(audio.last_sfx_gain(), engine::audio::final_gain(1.f, 1.f, 0.25f));

    audio.play_sfx(make_sound(2.f), 1.f);
    EXPECT_FLOAT_EQ(audio.last_sfx_gain(), 1.f);
}

TEST(Audio, PoolSkipWhenFull) {
    engine::AudioSystem audio;
    ASSERT_TRUE(audio.init());

    const int pool = audio.sfx_pool_size();
    EXPECT_EQ(pool, engine::audio::kSfxPoolSize);

    const engine::Sound sound = make_sound();
    for (int i = 0; i < pool; ++i) {
        audio.play_sfx(sound);
    }

    EXPECT_EQ(audio.sfx_playing_count(), pool);
    EXPECT_EQ(audio.sfx_play_count(), pool);

    audio.play_sfx(sound);
    audio.play_sfx(sound);
    audio.update(0.016f);

    EXPECT_EQ(audio.sfx_playing_count(), pool);
    EXPECT_EQ(audio.sfx_play_count(), pool);
}

TEST(Audio, MusicABCrossfadeSwapsIndex) {
    engine::AudioSystem audio;
    ASSERT_TRUE(audio.init());

    audio.play_music(make_sound(), true, 0.5f);
    EXPECT_EQ(audio.active_music_index(), 0);
    EXPECT_TRUE(audio.music_slot_playing(0));
    EXPECT_FALSE(audio.music_slot_playing(1));
    EXPECT_TRUE(audio.is_music_playing());
    EXPECT_FLOAT_EQ(audio.music_slot_gain(0), 1.f);

    audio.play_music(make_sound(), true, 0.5f);
    EXPECT_EQ(audio.active_music_index(), 1);
    EXPECT_TRUE(audio.music_slot_playing(0));
    EXPECT_TRUE(audio.music_slot_playing(1));
    EXPECT_FLOAT_EQ(audio.music_slot_gain(1), 0.f);
    EXPECT_GT(audio.music_slot_gain(0), 0.f);

    audio.update(0.25f);
    EXPECT_TRUE(audio.music_slot_playing(0));
    EXPECT_TRUE(audio.music_slot_playing(1));
    EXPECT_NEAR(audio.music_slot_gain(1), 0.5f, 0.001f);
    EXPECT_NEAR(audio.music_slot_gain(0), 0.5f, 0.001f);

    audio.update(0.25f);
    EXPECT_EQ(audio.active_music_index(), 1);
    EXPECT_FALSE(audio.music_slot_playing(0));
    EXPECT_TRUE(audio.music_slot_playing(1));
    EXPECT_FLOAT_EQ(audio.music_slot_gain(1), 1.f);
}

TEST(Audio, InvalidLoopingHandleNoOp) {
    engine::AudioSystem audio;
    ASSERT_TRUE(audio.init());

    const engine::LoopingSfxHandle valid = audio.create_looping_sfx();
    EXPECT_TRUE(valid.valid());
    audio.play_looping_sfx(valid, make_sound());
    const int acquired = audio.looping_track_count();
    EXPECT_EQ(acquired, 1);
    EXPECT_EQ(audio.sfx_playing_count(), 0);

    const engine::LoopingSfxHandle invalid{};
    EXPECT_FALSE(invalid.valid());
    audio.play_looping_sfx(invalid, make_sound());
    audio.stop_looping_sfx(invalid);
    audio.release_looping_sfx(invalid);

    const engine::LoopingSfxHandle garbage{999u};
    audio.play_looping_sfx(garbage, make_sound(), 0.1f);
    audio.stop_looping_sfx(garbage, 0.1f);
    audio.release_looping_sfx(garbage, 0.1f);
    audio.update(1.f);

    EXPECT_EQ(audio.looping_track_count(), acquired);
    EXPECT_EQ(audio.sfx_play_count(), 0);
}
