#include <gtest/gtest.h>
#include "renderer_frame_support.h"
#include "renderer_test_support.h"
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

namespace {
struct FrameSavedEnvironment {
    std::string name, value;
    bool present;
};

class RendererFrames : public ::testing::Test {
protected:
    RendererFrameFixture *frames = nullptr;
    RendererOverlayFixture *overlays = nullptr;
    std::vector<FrameSavedEnvironment> environment;

    void SetUp() override
    {
        for (const char *name : {"WESTEROS_GL_USE_PREFERRED_MODE",
             "WESTEROS_GL_USE_BEST_MODE", "WESTEROS_GL_MAX_MODE",
             "WESTEROS_GL_FRM_TRUNCATE", "AV_PROGRESSION"})
        {
            const char *value = getenv(name);
            environment.push_back({name, value ? value : "", value != nullptr});
            ASSERT_EQ(0, unsetenv(name));
        }
        RendererReset();
        ASSERT_NE(nullptr, RendererCreateContext(1920, 1080));
        RendererFrameReleaseReset();
        frames = RendererFramesCreate(false);
        overlays = RendererOverlaysCreate();
        ASSERT_NE(nullptr, frames);
        ASSERT_NE(nullptr, overlays);
    }
    void TearDown() override
    {
        RendererFramesDestroy(frames);
        RendererOverlaysDestroy(overlays);
        RendererCleanup();
        for (const auto &saved : environment)
        {
            if (saved.present) setenv(saved.name.c_str(), saved.value.c_str(), 1);
            else unsetenv(saved.name.c_str());
        }
    }
    void Immediate()
    {
        RendererFramesDestroy(frames);
        frames = RendererFramesCreate(true);
        ASSERT_NE(nullptr, frames);
    }
    void Lists(std::initializer_list<int> available, std::initializer_list<int> used)
    {
        const auto state = RendererOverlaysLists(overlays);
        ASSERT_TRUE(state.linksValid);
        EXPECT_EQ(static_cast<int>(available.size() + used.size()), state.count);
        EXPECT_EQ(static_cast<int>(used.size()), state.usedCount);
        ASSERT_EQ(static_cast<int>(available.size()), state.availableCount);
        ASSERT_EQ(static_cast<int>(used.size()), state.usedListCount);
        int i = 0;
        for (int value : available) EXPECT_EQ(value, state.available[i++]);
        i = 0;
        for (int value : used) EXPECT_EQ(value, state.used[i++]);
    }
    void Rect(const RendererFrameView &frame, int x, int y, int w, int h)
    {
        EXPECT_EQ(x, frame.x); EXPECT_EQ(y, frame.y);
        EXPECT_EQ(w, frame.width); EXPECT_EQ(h, frame.height);
        EXPECT_EQ(1920u, frame.visibleWidth); EXPECT_EQ(1080u, frame.visibleHeight);
    }
    void AddRateModes()
    {
        RendererFrameCurrentRate(60);
        RendererAddMode(1920, 1080, 60, false);
        RendererAddMode(1920, 1080, 120, false);
        RendererAddMode(1920, 1080, 48, false);
        RendererAddMode(1280, 720, 144, false);
    }
};
} // namespace

TEST_F(RendererFrames, CreatesInitializedQueueAndTimingState)
{
    const auto state = RendererFramesState(frames);
    EXPECT_EQ(0, state.size); EXPECT_EQ(16, state.capacity);
    EXPECT_EQ(-1, state.currentId); EXPECT_EQ(-1, state.syncSession);
    EXPECT_FLOAT_EQ(1.0f, state.rate);
    EXPECT_EQ(0, state.drops); EXPECT_EQ(0, state.flipBase);
    EXPECT_EQ(0, state.frameBase); EXPECT_EQ(0, state.flipCurrent);
    EXPECT_FALSE(state.paused); EXPECT_FALSE(state.advance);
    EXPECT_FALSE(state.underflow); EXPECT_FALSE(state.resetBaseTime);
    for (int i = 0; i < state.capacity; ++i)
    {
        const auto frame = RendererFramesAt(frames, i);
        EXPECT_EQ(-1, frame.id); EXPECT_EQ(-1, frame.fd0);
        EXPECT_EQ(-1, frame.fd1); EXPECT_EQ(-1, frame.fd2);
    }
    RendererFramesDestroy(nullptr);
}

TEST_F(RendererFrames, PushCopiesFramesAndGrowthPreservesAllEntries)
{
    for (int i = 0; i < 34; ++i) RendererFramesPush(frames, i, 100000LL + 40000LL * i);
    const auto state = RendererFramesState(frames);
    ASSERT_EQ(34, state.size);
    ASSERT_EQ(67, state.capacity); // 16 -> 33 -> 67, two genuine reallocations.
    for (int i = 0; i < state.size; ++i)
    {
        const auto frame = RendererFramesAt(frames, i);
        EXPECT_EQ(i, frame.id); EXPECT_EQ(i + 1000, frame.number);
        EXPECT_EQ(100000LL + 40000LL * i, frame.time);
        EXPECT_EQ(-1, frame.fd0); EXPECT_FALSE(frame.canExpire);
    }
    for (int i = state.size; i < state.capacity; ++i)
    {
        const auto frame = RendererFramesAt(frames, i);
        EXPECT_EQ(-1, frame.id); EXPECT_EQ(-1, frame.fd0);
        EXPECT_EQ(-1, frame.fd1); EXPECT_EQ(-1, frame.fd2);
    }
}

TEST_F(RendererFrames, DestructionReleasesPendingButNotDisplayedBuffer)
{
    RendererFramesPush(frames, 0x01020304, 1000);
    RendererFramesPush(frames, 11, 41000);
    RendererFramesPush(frames, 12, 81000);
    RendererFramesDisplayed(frames, 11, 41000);
    RendererFramesDestroy(frames);
    frames = nullptr;
    ASSERT_EQ(2, RendererFrameReleaseCount());
    EXPECT_EQ(0x01020304, RendererFrameReleasedId(0));
    EXPECT_EQ(12, RendererFrameReleasedId(1));
    EXPECT_TRUE(RendererFrameReleaseWireValid());
    EXPECT_EQ(0u, RendererCalls()->openCalls);
    EXPECT_EQ(0u, RendererCalls()->closeCalls);
}

TEST_F(RendererFrames, EmptyImmediateQueueDoesNotUnderflow)
{
    Immediate();
    RendererFramesDisplayed(frames, 99, 1);
    RendererFrameView result{};
    EXPECT_FALSE(RendererFramesPop(frames, 100000, 16000, &result));
    EXPECT_FALSE(RendererFramesState(frames).underflow);
    EXPECT_EQ(0, RendererFrameReleaseCount());
}

TEST_F(RendererFrames, ImmediateDropsAllButNewestInOrder)
{
    Immediate();
    for (int i = 1; i <= 4; ++i) RendererFramesPush(frames, i, i * 40000);
    RendererFrameView result{};
    ASSERT_TRUE(RendererFramesPop(frames, 100000, 16000, &result));
    EXPECT_EQ(4, result.id); EXPECT_EQ(160000, result.time);
    EXPECT_EQ(1, RendererFramesState(frames).size);
    EXPECT_EQ(3, RendererFramesState(frames).drops);
    ASSERT_EQ(3, RendererFrameReleaseCount());
    for (int i = 0; i < 3; ++i) EXPECT_EQ(i + 1, RendererFrameReleasedId(i));
    EXPECT_TRUE(RendererFrameReleaseWireValid());
    ASSERT_TRUE(RendererFramesPop(frames, 116000, 16000, &result));
    EXPECT_EQ(4, result.id);
    EXPECT_EQ(3, RendererFrameReleaseCount()); // Holding does not release twice.
}

TEST_F(RendererFrames, ImmediateDoesNotDropOrReleaseCurrentFrame)
{
    Immediate();
    RendererFramesPush(frames, 1, 1000);
    RendererFramesPush(frames, 2, 41000);
    RendererFramesPush(frames, 3, 81000);
    RendererFramesDisplayed(frames, 1, 1000);
    RendererFrameView result{};
    ASSERT_TRUE(RendererFramesPop(frames, 100000, 16000, &result));
    EXPECT_EQ(3, result.id);
    EXPECT_EQ(1, RendererFramesState(frames).drops);
    ASSERT_EQ(1, RendererFrameReleaseCount());
    EXPECT_EQ(2, RendererFrameReleasedId(0));
}

TEST_F(RendererFrames, TimedStartupWaitsForThirdFrame)
{
    RendererFrameView result{};
    EXPECT_FALSE(RendererFramesPop(frames, 100000, 16000, &result));
    RendererFramesPush(frames, 1, 1000);
    EXPECT_FALSE(RendererFramesPop(frames, 100000, 16000, &result));
    RendererFramesPush(frames, 2, 41000);
    EXPECT_FALSE(RendererFramesPop(frames, 100000, 16000, &result));
    RendererFramesPush(frames, 3, 81000);
    ASSERT_TRUE(RendererFramesPop(frames, 100000, 16000, &result));
    EXPECT_EQ(1, result.id); EXPECT_TRUE(result.canExpire);
    const auto state = RendererFramesState(frames);
    EXPECT_EQ(100000, state.flipBase); EXPECT_EQ(1000, state.frameBase);
    EXPECT_EQ(100000, state.flipCurrent); EXPECT_EQ(1000, state.frameCurrent);
    EXPECT_EQ(3, state.size); EXPECT_EQ(1, state.currentId);
}

TEST_F(RendererFrames, TimedQueueHoldsThenAdvancesAtVblankLookahead)
{
    for (int i = 1; i <= 3; ++i) RendererFramesPush(frames, i, 1000 + (i - 1) * 40000);
    RendererFrameView result{};
    ASSERT_TRUE(RendererFramesPop(frames, 100000, 16000, &result));
    ASSERT_TRUE(RendererFramesPop(frames, 116000, 16000, &result));
    EXPECT_EQ(1, result.id); EXPECT_EQ(3, RendererFramesState(frames).size);
    ASSERT_TRUE(RendererFramesPop(frames, 132000, 16000, &result));
    EXPECT_EQ(2, result.id); EXPECT_EQ(2, RendererFramesState(frames).size);
    const auto state = RendererFramesState(frames);
    EXPECT_EQ(140000, state.flipCurrent);
    EXPECT_EQ(-24000, state.adjust); EXPECT_EQ(17000, state.frameCurrent);
    EXPECT_EQ(0, state.drops); EXPECT_EQ(0, RendererFrameReleaseCount());
}

TEST_F(RendererFrames, TooEarlyFrameRetainsDisplayedPlane)
{
    RendererFramesPush(frames, 2, 101000);
    RendererFramesDisplayed(frames, 1, 1000);
    RendererFramesTiming(frames, 100000, 1000, 0, 1.0f);
    RendererFrameView result{};
    ASSERT_TRUE(RendererFramesPop(frames, 100000, 16000, &result));
    EXPECT_EQ(1, result.id); EXPECT_EQ(1000, result.time);
    EXPECT_EQ(1, RendererFramesState(frames).size);
    EXPECT_EQ(0, RendererFrameReleaseCount());
}

TEST_F(RendererFrames, LateFollowingFrameIsDroppedAndQueueCompacted)
{
    RendererFramesPush(frames, 1, 1000);
    RendererFramesPush(frames, 2, 5000);
    RendererFramesPush(frames, 3, 41000);
    RendererFrameView result{};
    ASSERT_TRUE(RendererFramesPop(frames, 100000, 16000, &result));
    RendererFramesDisplayed(frames, 1, 1000);
    ASSERT_TRUE(RendererFramesPop(frames, 116000, 16000, &result));
    EXPECT_EQ(1, result.id);
    EXPECT_EQ(2, RendererFramesState(frames).size);
    EXPECT_EQ(1, RendererFramesState(frames).drops);
    EXPECT_EQ(3, RendererFramesAt(frames, 1).id);
    ASSERT_EQ(1, RendererFrameReleaseCount());
    EXPECT_EQ(2, RendererFrameReleasedId(0));
}

TEST_F(RendererFrames, CustomExpiryDropsOldBufferButKeepsDisplayedPlane)
{
    RendererFramesPush(frames, 1, 1000);
    RendererFramesTiming(frames, 100000, 1000, 10000, 1.0f);
    RendererFrameView result{};
    ASSERT_TRUE(RendererFramesPop(frames, 100000, 1000, &result));
    ASSERT_TRUE(result.canExpire);
    RendererFramesDisplayed(frames, 99, 900);
    ASSERT_TRUE(RendererFramesPop(frames, 111000, 1000, &result));
    EXPECT_EQ(99, result.id);
    EXPECT_EQ(0, RendererFramesState(frames).size);
    EXPECT_EQ(1, RendererFramesState(frames).drops);
    ASSERT_EQ(1, RendererFrameReleaseCount());
    EXPECT_EQ(1, RendererFrameReleasedId(0));
}

TEST_F(RendererFrames, ExpiredCurrentBufferIsRemovedWithoutDuplicateRelease)
{
    RendererFramesPush(frames, 1, 1000);
    RendererFramesTiming(frames, 100000, 1000, 10000, 1.0f);
    RendererFrameView result{};
    ASSERT_TRUE(RendererFramesPop(frames, 100000, 1000, &result));
    RendererFramesDisplayed(frames, 1, 1000);
    ASSERT_TRUE(RendererFramesPop(frames, 111000, 1000, &result));
    EXPECT_EQ(1, result.id); EXPECT_EQ(0, RendererFramesState(frames).size);
    EXPECT_EQ(0, RendererFramesState(frames).drops);
    EXPECT_EQ(0, RendererFrameReleaseCount());
}

TEST_F(RendererFrames, PlaybackRateScalesPresentationTime)
{
    RendererFramesPush(frames, 2, 41000);
    RendererFramesTiming(frames, 100000, 1000, 0, 2.0f);
    RendererFrameView result{};
    EXPECT_FALSE(RendererFramesPop(frames, 100000, 10000, &result));
    ASSERT_TRUE(RendererFramesPop(frames, 110000, 10000, &result));
    EXPECT_EQ(2, result.id);
    EXPECT_EQ(120000, RendererFramesState(frames).flipCurrent);
}

TEST_F(RendererFrames, DefaultExpiryKeepsBoundaryThenReleasesExpiredFrame)
{
    RendererFramesPush(frames, 1, 1000);
    RendererFramesTiming(frames, 100000, 1000, 0, 1.0f);
    RendererFrameView result{};
    ASSERT_TRUE(RendererFramesPop(frames, 100000, 1000, &result));
    ASSERT_TRUE(RendererFramesPop(frames, 183000, 1000, &result));
    EXPECT_EQ(1, result.id); EXPECT_EQ(1, RendererFramesState(frames).size);
    RendererFramesDisplayed(frames, 99, 900);
    ASSERT_TRUE(RendererFramesPop(frames, 184000, 1000, &result));
    EXPECT_EQ(99, result.id); EXPECT_EQ(0, RendererFramesState(frames).size);
    ASSERT_EQ(1, RendererFrameReleaseCount());
    EXPECT_EQ(1, RendererFrameReleasedId(0));
}

TEST_F(RendererFrames, PauseHoldsQueueAndResumeResetsOnlyAfterPausedPop)
{
    RendererFramesPush(frames, 1, 1000);
    RendererFramesTiming(frames, 123000, 1000, 0, 1.0f);
    RendererFramesPause(frames, true);
    RendererFramesPause(frames, false);
    EXPECT_EQ(123000, RendererFramesState(frames).flipBase);
    RendererFramesPause(frames, true);
    RendererFrameView result{};
    EXPECT_FALSE(RendererFramesPop(frames, 200000, 16000, &result));
    EXPECT_EQ(1, RendererFramesState(frames).size);
    EXPECT_TRUE(RendererFramesState(frames).resetBaseTime);
    RendererFramesPause(frames, false);
    EXPECT_FALSE(RendererFramesState(frames).paused);
    EXPECT_FALSE(RendererFramesState(frames).resetBaseTime);
    EXPECT_EQ(0, RendererFramesState(frames).flipBase);
    EXPECT_EQ(1000, RendererFramesState(frames).frameBase);
}

TEST_F(RendererFrames, FrameAdvanceWhilePausedBypassesStartupBuffering)
{
    RendererFramesPush(frames, 7, 99000);
    RendererFramesPause(frames, true);
    RendererFramesAdvance(frames);
    EXPECT_TRUE(RendererFramesState(frames).advance);
    RendererFrameView result{};
    ASSERT_TRUE(RendererFramesPop(frames, 200000, 16000, &result));
    EXPECT_EQ(7, result.id); EXPECT_FALSE(result.canExpire);
    EXPECT_FALSE(RendererFramesState(frames).advance);
    EXPECT_TRUE(RendererFramesState(frames).paused);
    EXPECT_EQ(200000, RendererFramesState(frames).flipBase);
    EXPECT_FALSE(RendererFramesPop(frames, 500000, 16000, &result));
    EXPECT_EQ(1, RendererFramesState(frames).size);
}

TEST_F(RendererFrames, UnderflowRequiresAnExistingDisplayedFrame)
{
    RendererFrameView result{};
    EXPECT_FALSE(RendererFramesPop(frames, 100000, 16000, &result));
    EXPECT_FALSE(RendererFramesState(frames).underflow);
    RendererFramesDisplayed(frames, 77, 2000);
    EXPECT_FALSE(RendererFramesPop(frames, 116000, 16000, &result));
    EXPECT_TRUE(RendererFramesState(frames).underflow);
    EXPECT_EQ(77, RendererFramesState(frames).currentId);
}

TEST_F(RendererFrames, ReportedUnderflowIsSuppressedAndSuccessfulPopRearmsIt)
{
    RendererFramesDisplayed(frames, 77, 2000);
    RendererFramesUnderflowReported(frames, true);
    RendererFrameView result{};
    EXPECT_FALSE(RendererFramesPop(frames, 100000, 16000, &result));
    EXPECT_FALSE(RendererFramesState(frames).underflow);
    EXPECT_TRUE(RendererFramesState(frames).underflowReported);
    RendererFramesPush(frames, 78, 42000);
    ASSERT_TRUE(RendererFramesPop(frames, 116000, 16000, &result));
    EXPECT_EQ(78, result.id);
    EXPECT_FALSE(RendererFramesState(frames).underflowReported);
}

TEST_F(RendererFrames, PauseSuppressesUnderflow)
{
    RendererFramesDisplayed(frames, 77, 2000);
    RendererFramesPause(frames, true);
    RendererFrameView result{};
    EXPECT_FALSE(RendererFramesPop(frames, 100000, 16000, &result));
    EXPECT_FALSE(RendererFramesState(frames).underflow);
}

TEST_F(RendererFrames, RectUpdatesWholeQueueWithoutDirtyingCurrentWhenBuffered)
{
    RendererFramesPush(frames, 1, 1000);
    RendererFramesPush(frames, 2, 41000);
    RendererFramesRect(frames, 10, 20, 640, 360);
    Rect(RendererFramesAt(frames, 0), 10, 20, 640, 360);
    Rect(RendererFramesAt(frames, 1), 10, 20, 640, 360);
    EXPECT_EQ(0, RendererFramesCurrent(frames).width);
    const auto state = RendererFramesState(frames);
    EXPECT_FALSE(state.dirty); EXPECT_FALSE(state.ready); EXPECT_FALSE(state.forceDirty);
}

TEST_F(RendererFrames, RectUpdatesDisplayedFrameWhenPausedEvenWithBufferedFrames)
{
    for (int i = 0; i < 3; ++i) RendererFramesPush(frames, i, 1000 + 40000 * i);
    RendererFramesPause(frames, true);
    RendererFramesRect(frames, 30, 40, 1280, 720);
    for (int i = 0; i < 3; ++i) Rect(RendererFramesAt(frames, i), 30, 40, 1280, 720);
    Rect(RendererFramesCurrent(frames), 30, 40, 1280, 720);
    const auto state = RendererFramesState(frames);
    EXPECT_TRUE(state.dirty); EXPECT_TRUE(state.ready); EXPECT_TRUE(state.forceDirty);
}

TEST_F(RendererFrames, RectClampsCurrentAndSingleQueuedFrameOffscreenCoordinates)
{
    RendererFramesPush(frames, 5, 1000);
    RendererFramesRect(frames, 4000, 2000, -20, -30);
    Rect(RendererFramesAt(frames, 0), 1918, 1078, 4, 4);
    Rect(RendererFramesCurrent(frames), 1918, 1078, 4, 4);
    EXPECT_TRUE(RendererFramesState(frames).forceDirty);
}

TEST_F(RendererFrames, RectUpdatesCurrentWithNoQueuedFrames)
{
    RendererFramesRect(frames, 0, 0, 1920, 1080);
    Rect(RendererFramesCurrent(frames), 0, 0, 1920, 1080);
    EXPECT_EQ(0, RendererFramesState(frames).size);
    EXPECT_TRUE(RendererFramesState(frames).ready);
}

TEST_F(RendererFrames, EosAndSyncTypeAreNoOpsWithoutAvsyncBackend)
{
    // This explicitly characterizes the configured backend, not AV-sync coverage.
    RendererFramesPush(frames, 8, 12345);
    RendererFramesPause(frames, true);
    RendererFramesEos(frames);
    RendererFramesSyncType(frames, 1);
    RendererFramesSyncType(frames, 255);
    const auto state = RendererFramesState(frames);
    EXPECT_EQ(1, state.size); EXPECT_TRUE(state.paused);
    EXPECT_EQ(-1, state.syncSession); EXPECT_EQ(-1, state.currentId);
    EXPECT_EQ(8, RendererFramesAt(frames, 0).id);
    EXPECT_EQ(12345, RendererFramesAt(frames, 0).time);
    EXPECT_EQ(0, RendererFrameReleaseCount());
}

TEST_F(RendererFrames, OverlayEmptyPoolAndMissingPrimaryReturnNull)
{
    Lists({}, {});
    EXPECT_EQ(-1, RendererOverlaysPrimary(overlays));
    EXPECT_EQ(-1, RendererOverlaysAlloc(overlays, true, false));
    EXPECT_EQ(-1, RendererOverlaysAlloc(overlays, false, false));
    RendererOverlaysFree(overlays, -1);
    Lists({}, {});
}

TEST_F(RendererFrames, OverlayAvailableListSortsHeadMiddleTailAndEqualZ)
{
    RendererOverlaysAdd(overlays, 20, true, false, false);
    RendererOverlaysAdd(overlays, 10, true, false, false);
    RendererOverlaysAdd(overlays, 40, true, false, false);
    RendererOverlaysAdd(overlays, 30, true, false, false);
    RendererOverlaysAdd(overlays, 20, true, false, false);
    Lists({1, 4, 0, 3, 2}, {}); // Equal z inserts before an equal successor.
}

TEST_F(RendererFrames, OverlayGraphicsAllocatesHighestZAndExhaustsPool)
{
    for (int i = 0; i < 3; ++i) RendererOverlaysAdd(overlays, i * 10, true, false, false);
    EXPECT_EQ(2, RendererOverlaysAlloc(overlays, true, false));
    Lists({0, 1}, {2});
    EXPECT_EQ(1, RendererOverlaysAlloc(overlays, true, false));
    Lists({0}, {2, 1});
    EXPECT_EQ(0, RendererOverlaysAlloc(overlays, true, false));
    Lists({}, {2, 1, 0});
    EXPECT_EQ(-1, RendererOverlaysAlloc(overlays, true, false));
    RendererOverlaysFree(overlays, 1);
    Lists({1}, {2, 0});
    EXPECT_EQ(1, RendererOverlaysAlloc(overlays, true, false));
    Lists({}, {2, 0, 1});
}

TEST_F(RendererFrames, OverlayVideoSelectsMatchingOrNonMatchingPlane)
{
    RendererOverlaysAdd(overlays, 0, true, true, false);
    RendererOverlaysAdd(overlays, 10, true, false, false);
    RendererOverlaysAdd(overlays, 20, true, true, false);
    EXPECT_EQ(1, RendererOverlaysAlloc(overlays, false, false));
    Lists({0, 2}, {1});
    EXPECT_EQ(-1, RendererOverlaysAlloc(overlays, false, false));
    Lists({0, 2}, {1});
    EXPECT_EQ(0, RendererOverlaysAlloc(overlays, false, true));
    Lists({2}, {1, 0});
    EXPECT_EQ(2, RendererOverlaysAlloc(overlays, false, true));
    Lists({}, {1, 0, 2});
}

TEST_F(RendererFrames, OverlayVideoRejectsPoolWhoseHeadDoesNotSupportVideo)
{
    RendererOverlaysAdd(overlays, 0, false, false, false);
    RendererOverlaysAdd(overlays, 10, true, true, false);
    EXPECT_EQ(-1, RendererOverlaysAlloc(overlays, false, true));
    Lists({0, 1}, {});
    EXPECT_EQ(1, RendererOverlaysAlloc(overlays, true, false));
    Lists({0}, {1});
}

TEST_F(RendererFrames, OverlayPrimaryCanBeRemovedFromMiddleOnlyOnce)
{
    RendererOverlaysAdd(overlays, 0, true, false, false);
    RendererOverlaysAdd(overlays, 10, true, false, true);
    RendererOverlaysAdd(overlays, 20, true, false, false);
    EXPECT_EQ(1, RendererOverlaysPrimary(overlays));
    Lists({0, 2}, {1});
    EXPECT_EQ(-1, RendererOverlaysPrimary(overlays));
    Lists({0, 2}, {1});
    RendererOverlaysFree(overlays, 1);
    Lists({0, 1, 2}, {});
    EXPECT_EQ(1, RendererOverlaysPrimary(overlays));
    Lists({0, 2}, {1});
}

TEST_F(RendererFrames, OverlayPrimarySingleNodeUpdatesBothListEnds)
{
    RendererOverlaysAdd(overlays, 0, true, false, true);
    EXPECT_EQ(0, RendererOverlaysPrimary(overlays));
    Lists({}, {0});
    RendererOverlaysFree(overlays, 0);
    Lists({0}, {});
}

TEST_F(RendererFrames, OverlayPrimaryAppendsToExistingUsedList)
{
    RendererOverlaysAdd(overlays, 0, true, false, true);
    RendererOverlaysAdd(overlays, 10, true, false, false);
    RendererOverlaysAdd(overlays, 20, true, false, false);
    ASSERT_EQ(2, RendererOverlaysAlloc(overlays, true, false));
    ASSERT_EQ(0, RendererOverlaysPrimary(overlays));
    Lists({1}, {2, 0});
    RendererOverlaysFree(overlays, 0);
    Lists({0, 1}, {2});
}

TEST_F(RendererFrames, OverlayFreeHeadMiddleTailRestoresSortedPool)
{
    for (int i = 0; i < 4; ++i) RendererOverlaysAdd(overlays, i * 10, true, false, false);
    for (int i = 3; i >= 0; --i) ASSERT_EQ(i, RendererOverlaysAlloc(overlays, true, false));
    RendererOverlaysFree(overlays, 2); Lists({2}, {3, 1, 0});
    RendererOverlaysFree(overlays, 3); Lists({2, 3}, {1, 0});
    RendererOverlaysFree(overlays, 0); Lists({0, 2, 3}, {1});
    RendererOverlaysFree(overlays, 1); Lists({0, 1, 2, 3}, {});
}

TEST_F(RendererFrames, OverlayFreeResetsPlaybackAndBufferOwnership)
{
    RendererOverlaysAdd(overlays, 0, true, false, false);
    ASSERT_EQ(0, RendererOverlaysAlloc(overlays, true, false));
    RendererOverlaysSeed(overlays, 0, false);
    RendererOverlaysFree(overlays, 0);
    const auto state = RendererOverlaysState(overlays, 0);
    EXPECT_FALSE(state.inUse); EXPECT_FALSE(state.hide); EXPECT_FALSE(state.hidden);
    EXPECT_FALSE(state.hasConnection); EXPECT_FALSE(state.keepLast);
    EXPECT_EQ(-1, state.resourceId); EXPECT_EQ(0, state.frameCount);
    EXPECT_EQ(0, state.frameBase); EXPECT_EQ(0, state.flipBase);
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ(-1, state.buffers[i]); EXPECT_FALSE(state.hasPlane[i]);
        EXPECT_FALSE(state.kept[i]);
    }
    Lists({0}, {});
}

TEST_F(RendererFrames, OverlayFreeKeepsOnlyCurrentBufferWhenRequested)
{
    RendererOverlaysAdd(overlays, 0, true, false, false);
    ASSERT_EQ(0, RendererOverlaysAlloc(overlays, true, false));
    RendererOverlaysSeed(overlays, 0, true);
    RendererOverlaysFree(overlays, 0);
    const auto state = RendererOverlaysState(overlays, 0);
    EXPECT_FALSE(state.keepLast); EXPECT_FALSE(state.inUse);
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_EQ(i == 1 ? 101 : -1, state.buffers[i]);
        EXPECT_EQ(i == 1, state.kept[i]); EXPECT_FALSE(state.hasPlane[i]);
    }
    Lists({0}, {});
}

TEST_F(RendererFrames, ModeSelectionUsesDriverMatchRatherThanFirstResolutionMatch)
{
    RendererAddMode(1280, 720, 60, false);
    RendererAddMode(1920, 1080, 24, false);
    RendererAddMode(1920, 1080, 60, false);
    RendererFrameModeType(2, true, false);
    RendererFrameSelectMode(1920, 1080, false);
    EXPECT_EQ(1920, RendererFrameCurrentModeWidth());
    EXPECT_EQ(1080, RendererFrameCurrentModeHeight());
    EXPECT_EQ(60, RendererFrameCurrentModeRate());
}

TEST_F(RendererFrames, ModeSelectionFallsBackToFirstWhenNoDriverMatch)
{
    RendererAddMode(1280, 720, 50, false);
    RendererAddMode(1920, 1080, 60, false);
    RendererFrameSelectMode(1920, 1080, false);
    EXPECT_EQ(1280, RendererFrameCurrentModeWidth());
    EXPECT_EQ(720, RendererFrameCurrentModeHeight());
    EXPECT_EQ(50, RendererFrameCurrentModeRate());
}

TEST_F(RendererFrames, AtomicPreferredModeUsesLastPreferredEntry)
{
    ASSERT_EQ(0, setenv("WESTEROS_GL_USE_PREFERRED_MODE", "1", 1));
    RendererAddMode(1280, 720, 50, false);
    RendererAddMode(1920, 1080, 60, false);
    RendererAddMode(3840, 2160, 30, false);
    RendererFrameModeType(0, true, true);
    RendererFrameModeType(2, true, true);
    RendererFrameSelectMode(1920, 1080, true);
    EXPECT_EQ(3840, RendererFrameCurrentModeWidth());
    EXPECT_EQ(2160, RendererFrameCurrentModeHeight());
    EXPECT_EQ(30, RendererFrameCurrentModeRate());
}

TEST_F(RendererFrames, NonAtomicModeIgnoresPreferredEnvironment)
{
    ASSERT_EQ(0, setenv("WESTEROS_GL_USE_PREFERRED_MODE", "1", 1));
    RendererAddMode(3840, 2160, 30, false);
    RendererAddMode(1920, 1080, 60, false);
    RendererFrameModeType(0, true, true);
    RendererFrameModeType(1, true, false);
    RendererFrameSelectMode(1920, 1080, false);
    EXPECT_EQ(1920, RendererFrameCurrentModeWidth());
    EXPECT_EQ(60, RendererFrameCurrentModeRate());
}

TEST_F(RendererFrames, BestModeChoosesLargestAreaThenHighestRefresh)
{
    ASSERT_EQ(0, setenv("WESTEROS_GL_USE_BEST_MODE", "1", 1));
    RendererAddMode(1280, 720, 120, false);
    RendererAddMode(3840, 2160, 30, false);
    RendererAddMode(3840, 2160, 60, false);
    RendererAddMode(3840, 2160, 24, false);
    RendererFrameSelectMode(1280, 720, true);
    EXPECT_EQ(3840, RendererFrameCurrentModeWidth());
    EXPECT_EQ(60, RendererFrameCurrentModeRate());
}

TEST_F(RendererFrames, BestModeHonorsMaximumAreaAndFallsBackIfAllExcluded)
{
    ASSERT_EQ(0, setenv("WESTEROS_GL_USE_BEST_MODE", "1", 1));
    ASSERT_EQ(0, setenv("WESTEROS_GL_MAX_MODE", "1920x1080", 1));
    RendererAddMode(1280, 720, 60, false);
    RendererAddMode(1920, 1080, 50, false);
    RendererAddMode(3840, 2160, 60, false);
    RendererFrameSelectMode(3840, 2160, true);
    EXPECT_EQ(1920, RendererFrameCurrentModeWidth());
    EXPECT_EQ(50, RendererFrameCurrentModeRate());
    ASSERT_EQ(0, setenv("WESTEROS_GL_MAX_MODE", "640x480", 1));
    RendererFrameSelectMode(3840, 2160, true);
    EXPECT_EQ(1280, RendererFrameCurrentModeWidth());
    ASSERT_EQ(0, setenv("WESTEROS_GL_MAX_MODE", "invalid", 1));
    RendererFrameSelectMode(1280, 720, true);
    EXPECT_EQ(3840, RendererFrameCurrentModeWidth());
}

TEST_F(RendererFrames, PreferredPolicyWinsOverBestAndFallsBackWithoutPreferred)
{
    ASSERT_EQ(0, setenv("WESTEROS_GL_USE_PREFERRED_MODE", "1", 1));
    ASSERT_EQ(0, setenv("WESTEROS_GL_USE_BEST_MODE", "1", 1));
    RendererAddMode(1280, 720, 50, false);
    RendererAddMode(1920, 1080, 60, false);
    RendererAddMode(3840, 2160, 60, false);
    RendererFrameModeType(1, true, true);
    RendererFrameSelectMode(3840, 2160, true);
    EXPECT_EQ(1920, RendererFrameCurrentModeWidth());
    RendererFrameModeType(1, true, false);
    RendererFrameSelectMode(3840, 2160, true);
    EXPECT_EQ(1280, RendererFrameCurrentModeWidth());
}

TEST_F(RendererFrames, RateChoosesHighestMultipleAtCurrentResolution)
{
    AddRateModes();
    RendererFrameSelectRate(24, 1);
    const auto state = RendererGetModeState();
    EXPECT_TRUE(state.pending); EXPECT_EQ(120, state.rate);
    EXPECT_EQ(1920, state.width); EXPECT_EQ(1080, state.height);
    EXPECT_FALSE(state.sizeChanged); EXPECT_FALSE(state.explicitlySelected);
}

TEST_F(RendererFrames, RateNormalizesNonpositiveDenominator)
{
    AddRateModes();
    RendererFrameSelectRate(24, 0);
    EXPECT_EQ(120, RendererGetModeState().rate);
    RendererFrameSelectRate(24, -1);
    EXPECT_EQ(120, RendererGetModeState().rate);
}

TEST_F(RendererFrames, RateDoesNotRequestChangeWhenCurrentIsBestMatch)
{
    AddRateModes();
    RendererFrameCurrentRate(120);
    RendererFrameSelectRate(24, 1);
    EXPECT_FALSE(RendererGetModeState().pending);
    EXPECT_EQ(120, RendererFrameCurrentModeRate());
}

TEST_F(RendererFrames, RateLeavesStateAloneWhenNoMultipleOrOnlyOneMode)
{
    RendererFrameCurrentRate(60);
    RendererAddMode(1920, 1080, 60, false);
    RendererFrameSelectRate(24, 1);
    EXPECT_FALSE(RendererGetModeState().pending);
    RendererAddMode(1920, 1080, 120, false);
    RendererFrameSelectRate(27, 1);
    EXPECT_FALSE(RendererGetModeState().pending);
    EXPECT_EQ(60, RendererFrameCurrentModeRate());
}