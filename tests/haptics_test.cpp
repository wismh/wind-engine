#include <gtest/gtest.h>

#include <engine/haptics/haptics_system.h>

TEST(Haptics, NotSupportedOnNativeBuild) {
    engine::HapticsSystem haptics;
    ASSERT_TRUE(haptics.init());
    EXPECT_FALSE(haptics.is_supported());
}

TEST(Haptics, VibrateRecordsRequest) {
    engine::HapticsSystem haptics;
    ASSERT_TRUE(haptics.init());

    haptics.vibrate(1.0f, 0.7f);
    EXPECT_TRUE(haptics.is_active());
    EXPECT_FLOAT_EQ(haptics.last_duration_seconds(), 1.0f);
    EXPECT_FLOAT_EQ(haptics.last_intensity(), 0.7f);
    EXPECT_EQ(haptics.vibrate_call_count(), 1);
}

TEST(Haptics, CancelClearsActiveAndCounts) {
    engine::HapticsSystem haptics;
    ASSERT_TRUE(haptics.init());

    haptics.vibrate(1.0f);
    haptics.cancel();
    EXPECT_FALSE(haptics.is_active());
    EXPECT_EQ(haptics.cancel_call_count(), 1);
}

TEST(Haptics, IntensityIsClampedToUnitRange) {
    engine::HapticsSystem haptics;
    ASSERT_TRUE(haptics.init());

    haptics.vibrate(1.0f, 5.0f);
    EXPECT_FLOAT_EQ(haptics.last_intensity(), 1.0f);
}

TEST(Haptics, NonPositiveDurationIsNoOp) {
    engine::HapticsSystem haptics;
    ASSERT_TRUE(haptics.init());

    haptics.vibrate(0.0f);
    EXPECT_FALSE(haptics.is_active());
    EXPECT_EQ(haptics.vibrate_call_count(), 0);

    haptics.vibrate(-1.0f);
    EXPECT_EQ(haptics.vibrate_call_count(), 0);
}

TEST(Haptics, NonPositiveIntensityIsNoOp) {
    engine::HapticsSystem haptics;
    ASSERT_TRUE(haptics.init());

    haptics.vibrate(1.0f, 0.0f);
    EXPECT_FALSE(haptics.is_active());
    EXPECT_EQ(haptics.vibrate_call_count(), 0);

    haptics.vibrate(1.0f, -0.5f);
    EXPECT_EQ(haptics.vibrate_call_count(), 0);
}

TEST(Haptics, NoOpRequestDoesNotCancelActiveVibration) {
    engine::HapticsSystem haptics;
    ASSERT_TRUE(haptics.init());

    haptics.vibrate(1.0f, 1.0f);
    haptics.vibrate(0.0f, 1.0f);
    EXPECT_TRUE(haptics.is_active());
    EXPECT_EQ(haptics.vibrate_call_count(), 1);
}
