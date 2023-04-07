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
    ASSERT_TRUE(audio.Init());
    audio.SetMasterVolume(0.5f);
    audio.SetSfxVolume(0.5f);
    audio.PlaySfx(make_sound(0.5f), 1.f);
    EXPECT_FLOAT_EQ(audio.last_sfx_gain(), engine::audio::final_gain(0.5f, 0.5f, 0.5f));
    EXPECT_FLOAT_EQ(audio.last_sfx_gain(), 0.125f);

    audio.SetMasterVolume(1.f);
    audio.SetSfxVolume(1.f);
    audio.PlaySfx(make_sound(0.5f), 0.5f);
    EXPECT_FLOAT_EQ(audio.last_sfx_gain(), engine::audio::final_gain(1.f, 1.f, 0.25f));

    audio.PlaySfx(make_sound(2.f), 1.f);
    EXPECT_FLOAT_EQ(audio.last_sfx_gain(), 1.f);
}

TEST(Audio, PoolSkipWhenFull) {
    engine::AudioSystem audio;
    ASSERT_TRUE(audio.Init());

    const int pool = audio.sfx_pool_size();
    EXPECT_EQ(pool, engine::audio::kSfxPoolSize);

    const engine::Sound sound = make_sound();
    for (int i = 0; i < pool; ++i) {
        audio.PlaySfx(sound);
    }

    EXPECT_EQ(audio.sfx_playing_count(), pool);
    EXPECT_EQ(audio.sfx_play_count(), pool);

    audio.PlaySfx(sound);
    audio.PlaySfx(sound);
    audio.Update(0.016f);

    EXPECT_EQ(audio.sfx_playing_count(), pool);
    EXPECT_EQ(audio.sfx_play_count(), pool);
}

TEST(Audio, MusicABCrossfadeSwapsIndex) {
    engine::AudioSystem audio;
    ASSERT_TRUE(audio.Init());

    audio.PlayMusic(make_sound(), true, 0.5f);
    EXPECT_EQ(audio.active_music_index(), 0);
    EXPECT_TRUE(audio.music_slot_playing(0));
    EXPECT_FALSE(audio.music_slot_playing(1));
    EXPECT_TRUE(audio.IsMusicPlaying());
    EXPECT_FLOAT_EQ(audio.music_slot_gain(0), 1.f);

    audio.PlayMusic(make_sound(), true, 0.5f);
    EXPECT_EQ(audio.active_music_index(), 1);
    EXPECT_TRUE(audio.music_slot_playing(0));
    EXPECT_TRUE(audio.music_slot_playing(1));
    EXPECT_FLOAT_EQ(audio.music_slot_gain(1), 0.f);
    EXPECT_GT(audio.music_slot_gain(0), 0.f);

    audio.Update(0.25f);
    EXPECT_TRUE(audio.music_slot_playing(0));
    EXPECT_TRUE(audio.music_slot_playing(1));
    EXPECT_NEAR(audio.music_slot_gain(1), 0.5f, 0.001f);
    EXPECT_NEAR(audio.music_slot_gain(0), 0.5f, 0.001f);

    audio.Update(0.25f);
    EXPECT_EQ(audio.active_music_index(), 1);
    EXPECT_FALSE(audio.music_slot_playing(0));
    EXPECT_TRUE(audio.music_slot_playing(1));
    EXPECT_FLOAT_EQ(audio.music_slot_gain(1), 1.f);
}

TEST(Audio, InvalidLoopingHandleNoOp) {
    engine::AudioSystem audio;
    ASSERT_TRUE(audio.Init());

    const engine::LoopingSfxHandle valid = audio.CreateLoopingSfx();
    EXPECT_TRUE(valid.valid());
    audio.PlayLoopingSfx(valid, make_sound());
    const int acquired = audio.looping_track_count();
    EXPECT_EQ(acquired, 1);
    EXPECT_EQ(audio.sfx_playing_count(), 0);

    const engine::LoopingSfxHandle invalid{};
    EXPECT_FALSE(invalid.valid());
    audio.PlayLoopingSfx(invalid, make_sound());
    audio.StopLoopingSfx(invalid);
    audio.ReleaseLoopingSfx(invalid);

    const engine::LoopingSfxHandle garbage{999u};
    audio.PlayLoopingSfx(garbage, make_sound(), 0.1f);
    audio.StopLoopingSfx(garbage, 0.1f);
    audio.ReleaseLoopingSfx(garbage, 0.1f);
    audio.Update(1.f);

    EXPECT_EQ(audio.looping_track_count(), acquired);
    EXPECT_EQ(audio.sfx_play_count(), 0);
}
