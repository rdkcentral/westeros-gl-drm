#include <gtest/gtest.h>
#include "renderer_drm_support.h"
#include "renderer_test_support.h"
#include <xf86drmMode.h>
#include <cstdint>
#include <cstring>

namespace {
RendererDRMFrame Frame(int id, int x = 0, int y = 0, int w = 640, int h = 360,
                       unsigned fw = 1280, unsigned fh = 720)
{
    RendererDRMFrame f{};
    f.id = id; f.fb = 1000 + id;
    f.handle0 = 2000 + id; f.handle1 = 3000 + id;
    f.fd0 = 200 + id; f.fd1 = 300 + id; f.fd2 = 400 + id;
    f.x = x; f.y = y; f.width = w; f.height = h;
    f.visibleWidth = fw; f.visibleHeight = fh;
    return f;
}
void ExpectFrame(const RendererDRMFrame &actual, const RendererDRMFrame &expected)
{
    EXPECT_EQ(expected.id, actual.id); EXPECT_EQ(expected.fb, actual.fb);
    EXPECT_EQ(expected.handle0, actual.handle0); EXPECT_EQ(expected.handle1, actual.handle1);
    EXPECT_EQ(expected.fd0, actual.fd0); EXPECT_EQ(expected.fd1, actual.fd1); EXPECT_EQ(expected.fd2, actual.fd2);
    EXPECT_EQ(expected.x, actual.x); EXPECT_EQ(expected.y, actual.y);
    EXPECT_EQ(expected.width, actual.width); EXPECT_EQ(expected.height, actual.height);
    EXPECT_EQ(expected.visibleWidth, actual.visibleWidth); EXPECT_EQ(expected.visibleHeight, actual.visibleHeight);
    EXPECT_EQ(expected.hide, actual.hide); EXPECT_EQ(expected.hidden, actual.hidden);
}
unsigned PropertyCount(unsigned object, const char *name)
{
    unsigned count = 0;
    const auto &t = *RendererDRMCalls();
    for (unsigned i = 0; i < t.addCount; ++i)
        if (t.adds[i].object == object && !std::strcmp(t.adds[i].name, name)) ++count;
    return count;
}
void ExpectProperty(unsigned object, const char *name, uint64_t value)
{
    const auto &t = *RendererDRMCalls();
    ASSERT_EQ(1u, PropertyCount(object, name)) << object << ":" << name;
    for (unsigned i = 0; i < t.addCount; ++i)
        if (t.adds[i].object == object && !std::strcmp(t.adds[i].name, name))
        {
            EXPECT_EQ(value, t.adds[i].value) << name;
            const auto kind = object == RD_CONNECTOR_ID ? RD_CONNECTOR : object == RD_CRTC_ID ? RD_CRTC :
                object == RD_GRAPHICS_ID ? RD_GRAPHICS : RD_VIDEO;
            EXPECT_EQ(RendererDRMPropertyId(kind, name), t.adds[i].property);
        }
}
void ExpectNextConsumed()
{
    const auto n = RendererDRMGetState().frames[RD_NEXT];
    EXPECT_EQ(0u, n.fb); EXPECT_EQ(0u, n.handle0); EXPECT_EQ(0u, n.handle1);
    EXPECT_EQ(-1, n.fd0); EXPECT_EQ(-1, n.fd1); EXPECT_EQ(-1, n.fd2);
    EXPECT_FALSE(n.hide); EXPECT_FALSE(n.hidden);
}
class DRMProperties : public testing::TestWithParam<int> {
protected:
    void SetUp() override { ASSERT_TRUE(RendererDRMCreate(false, false)); }
    void TearDown() override
    {
        RendererDRMDestroy();
        EXPECT_EQ(0, RendererDRMCalls()->liveObjects);
        EXPECT_EQ(0, RendererDRMCalls()->liveProperties);
    }
    RendererDRMObject object() const { return static_cast<RendererDRMObject>(GetParam()); }
    unsigned count() const { return object() == RD_CONNECTOR ? 2 : object() == RD_CRTC ? 3 : 12; }
};
TEST_P(DRMProperties, AcquireOwnsEveryPropertyAndReleaseIsIdempotent)
{
    ASSERT_TRUE(RendererDRMAcquire(object()));
    ASSERT_TRUE(RendererDRMPropertiesPresent(object()));
    EXPECT_EQ(1, RendererDRMCalls()->liveObjects);
    EXPECT_EQ(static_cast<int>(count()), RendererDRMCalls()->liveProperties);
    EXPECT_EQ(count(), RendererDRMCalls()->propertyQueries);
    const unsigned ids[] = { RD_CONNECTOR_ID, RD_CRTC_ID, RD_GRAPHICS_ID, RD_VIDEO_ID };
    const unsigned types[] = { DRM_MODE_OBJECT_CONNECTOR, DRM_MODE_OBJECT_CRTC, DRM_MODE_OBJECT_PLANE, DRM_MODE_OBJECT_PLANE };
    EXPECT_EQ(ids[GetParam()], RendererDRMCalls()->queryObject);
    EXPECT_EQ(types[GetParam()], RendererDRMCalls()->queryType);
    RendererDRMRelease(object()); RendererDRMRelease(object());
    EXPECT_FALSE(RendererDRMPropertiesPresent(object()));
    EXPECT_EQ(1u, RendererDRMCalls()->objectFrees);
    EXPECT_EQ(count(), RendererDRMCalls()->propertyFrees);
    EXPECT_EQ(0, RendererDRMCalls()->liveObjects);
    EXPECT_EQ(0, RendererDRMCalls()->liveProperties);
}
TEST_P(DRMProperties, ObjectQueryFailureDisablesAtomicAndLeavesNoPartialState)
{
    RendererDRMFailures()->failObject = true;
    EXPECT_FALSE(RendererDRMAcquire(object()));
    EXPECT_FALSE(RendererDRMGetOptions().atomic);
    EXPECT_FALSE(RendererDRMPropertiesPresent(object()));
    EXPECT_EQ(0u, RendererDRMCalls()->propertyQueries);
    EXPECT_EQ(0u, RendererDRMCalls()->objectFrees);
}
TEST_P(DRMProperties, FirstPropertyFailureReleasesObjectOnly)
{
    RendererDRMFailures()->failPropertyId = GetParam() * 100 + 1;
    EXPECT_FALSE(RendererDRMAcquire(object()));
    EXPECT_FALSE(RendererDRMGetOptions().atomic);
    EXPECT_FALSE(RendererDRMPropertiesPresent(object()));
    EXPECT_EQ(1u, RendererDRMCalls()->objectFrees);
    EXPECT_EQ(0u, RendererDRMCalls()->propertyFrees);
    EXPECT_EQ(0, RendererDRMCalls()->liveObjects);
}
TEST_P(DRMProperties, LastPropertyFailureUnwindsAllEarlierProperties)
{
    RendererDRMFailures()->failPropertyId = GetParam() * 100 + count();
    EXPECT_FALSE(RendererDRMAcquire(object()));
    EXPECT_EQ(count(), RendererDRMCalls()->propertyQueries);
    EXPECT_EQ(count() - 1, RendererDRMCalls()->propertyFrees);
    EXPECT_EQ(1u, RendererDRMCalls()->objectFrees);
    EXPECT_EQ(0, RendererDRMCalls()->liveProperties);
    EXPECT_FALSE(RendererDRMPropertiesPresent(object()));
}
TEST_P(DRMProperties, CanReacquireAfterAnUnwoundFailure)
{
    RendererDRMFailures()->failPropertyId = GetParam() * 100 + 2;
    ASSERT_FALSE(RendererDRMAcquire(object()));
    RendererDRMFailures()->failPropertyId = 0;
    ASSERT_TRUE(RendererDRMAcquire(object()));
    EXPECT_EQ(static_cast<int>(count()), RendererDRMCalls()->liveProperties);
    // Acquisition does not silently re-enable atomic after a previous error.
    EXPECT_FALSE(RendererDRMGetOptions().atomic);
    RendererDRMRelease(object());
    EXPECT_EQ(2u, RendererDRMCalls()->objectFrees);
    EXPECT_EQ(count() + 1, RendererDRMCalls()->propertyFrees);
}
INSTANTIATE_TEST_SUITE_P(AllObjects, DRMProperties, testing::Values(0, 1, 2, 3));

class RendererDRM : public testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(RendererDRMCreate(true, true));
        for (int i = 0; i < 4; ++i) ASSERT_TRUE(RendererDRMAcquire(static_cast<RendererDRMObject>(i)));
    }
    void TearDown() override
    {
        RendererDRMDestroy();
        const auto &t = *RendererDRMCalls();
        EXPECT_EQ(0, t.liveProperties); EXPECT_EQ(0, t.liveObjects);
        EXPECT_EQ(0, t.liveRequests); EXPECT_EQ(0, t.liveBlobs);
        EXPECT_EQ(0u, t.outstandingBO); EXPECT_EQ(0u, t.outstandingFB); EXPECT_EQ(0u, t.unexpectedIoctl);
        EXPECT_FALSE(RendererHasContext());
    }
    void Configure(bool atomic, bool windowDirty = true)
    {
        auto o = RendererDRMGetOptions(); o.atomic = atomic; o.windowDirty = windowDirty;
        RendererDRMConfigure(o);
    }
    void SeedRotation()
    {
        RendererDRMSetFrame(RD_NEXT, Frame(11));
        RendererDRMSetFrame(RD_CURRENT, Frame(12));
        RendererDRMSetFrame(RD_PREVIOUS, Frame(13));
    }
};
TEST_F(RendererDRM, MissingConnectorAcquisitionIsANoop)
{
    RendererDRMRelease(RD_CONNECTOR); RendererDRMDetachConnector();
    const auto queries = RendererDRMCalls()->objectQueries;
    EXPECT_TRUE(RendererDRMAcquire(RD_CONNECTOR));
    EXPECT_TRUE(RendererDRMGetOptions().atomic);
    EXPECT_FALSE(RendererDRMPropertiesPresent(RD_CONNECTOR));
    EXPECT_EQ(queries, RendererDRMCalls()->objectQueries);
}
TEST_F(RendererDRM, AtomicPropertyUsesExactNameIdAndFull64BitValue)
{
    const uint64_t value = UINT64_C(0xfedcba9876543210);
    RendererDRMAddProperty(RD_VIDEO, "zpos", value);
    ASSERT_EQ(1u, RendererDRMCalls()->addCount);
    ExpectProperty(RD_VIDEO_ID, "zpos", value);
    EXPECT_EQ(0, RendererDRMCalls()->liveRequests);
}
TEST_F(RendererDRM, UnknownAndCaseMismatchedPropertiesAreNotSubmitted)
{
    RendererDRMAddProperty(RD_VIDEO, "fb_id", 77);
    RendererDRMAddProperty(RD_VIDEO, "FB_ID_suffix", 88);
    RendererDRMAddProperty(RD_VIDEO, "absent", 99);
    EXPECT_EQ(0u, RendererDRMCalls()->addCount);
    EXPECT_EQ(3u, RendererDRMCalls()->atomicFrees);
}
TEST_F(RendererDRM, AtomicAddFailureStillRecordsRequestedValueAndFreesRequest)
{
    RendererDRMFailures()->atomicAddResult = -1;
    RendererDRMAddProperty(RD_CRTC, "ACTIVE", 1);
    ExpectProperty(RD_CRTC_ID, "ACTIVE", 1);
    EXPECT_EQ(1u, RendererDRMCalls()->atomicFrees);
    EXPECT_EQ(0, RendererDRMCalls()->liveRequests);
}
TEST_F(RendererDRM, AtomicInitialModesetBuildsBlobAndCompleteGraphicsPlane)
{
    auto o = RendererDRMGetOptions(); o.modeSet = false; RendererDRMConfigure(o);
    RendererDRMSwap(false);
    const auto &t = *RendererDRMCalls();
    EXPECT_EQ(DRM_MODE_ATOMIC_ALLOW_MODESET, t.commitFlags);
    EXPECT_EQ(15u, t.addCount);
    ExpectProperty(RD_CONNECTOR_ID, "CRTC_ID", RD_CRTC_ID);
    ExpectProperty(RD_CRTC_ID, "MODE_ID", RD_BLOB_ID);
    ExpectProperty(RD_CRTC_ID, "ACTIVE", 1);
    ExpectProperty(RD_GRAPHICS_ID, "FB_ID", 901);
    ExpectProperty(RD_GRAPHICS_ID, "CRTC_ID", RD_CRTC_ID);
    ExpectProperty(RD_GRAPHICS_ID, "SRC_X", 0); ExpectProperty(RD_GRAPHICS_ID, "SRC_Y", 0);
    ExpectProperty(RD_GRAPHICS_ID, "SRC_W", 1280u << 16); ExpectProperty(RD_GRAPHICS_ID, "SRC_H", 720u << 16);
    ExpectProperty(RD_GRAPHICS_ID, "CRTC_X", 0); ExpectProperty(RD_GRAPHICS_ID, "CRTC_Y", 0);
    ExpectProperty(RD_GRAPHICS_ID, "CRTC_W", 1920); ExpectProperty(RD_GRAPHICS_ID, "CRTC_H", 1080);
    ExpectProperty(RD_GRAPHICS_ID, "IN_FENCE_FD", UINT64_MAX); ExpectProperty(RD_GRAPHICS_ID, "zpos", 3);
    EXPECT_EQ(1920u, t.blobWidth); EXPECT_EQ(1080u, t.blobHeight); EXPECT_EQ(60u, t.blobRate);
    EXPECT_EQ(1u, t.blobDestroys); EXPECT_EQ(1u, t.atomicFrees);
    EXPECT_EQ(1280u, t.fbWidth); EXPECT_EQ(720u, t.fbHeight);
    EXPECT_EQ(32u, t.fbDepth); EXPECT_EQ(32u, t.fbBpp);
    EXPECT_EQ(4096u, t.fbStride); EXPECT_EQ(501u, t.fbHandle);
    const auto s = RendererDRMGetState();
    EXPECT_TRUE(s.modeSet); EXPECT_EQ(60, s.defaultRate); EXPECT_EQ(901u, s.fb); EXPECT_EQ(1u, s.bo);
    EXPECT_FALSE(s.windowDirty); EXPECT_EQ(0u, s.previousFB);
}
TEST_F(RendererDRM, AtomicAllocationFailureDoesNotTouchBuffersOrMode)
{
    RendererDRMFailures()->failAtomicAlloc = true;
    auto o = RendererDRMGetOptions(); o.modeSet = false; RendererDRMConfigure(o);
    RendererDRMSwap(true);
    EXPECT_EQ(0u, RendererDRMCalls()->locks); EXPECT_EQ(0u, RendererDRMCalls()->commitCount);
    EXPECT_EQ(0u, RendererDRMCalls()->atomicFrees);
    EXPECT_FALSE(RendererDRMGetState().modeSet); EXPECT_TRUE(RendererDRMGetState().windowDirty);
}
TEST_F(RendererDRM, AtomicCommitFailureDoesNotMarkModeSetButRetainsSubmittedBuffers)
{
    RendererDRMFailures()->commitResult = -1;
    auto o = RendererDRMGetOptions(); o.modeSet = false; RendererDRMConfigure(o);
    SeedRotation(); RendererDRMSwap(false);
    const auto s = RendererDRMGetState();
    EXPECT_FALSE(s.modeSet); EXPECT_EQ(0, s.defaultRate);
    EXPECT_EQ(901u, s.fb); EXPECT_EQ(1u, s.bo);
    ExpectFrame(s.frames[RD_CURRENT], Frame(11)); ExpectFrame(s.frames[RD_FREE], Frame(13));
    EXPECT_EQ(1u, RendererDRMCalls()->blobDestroys); EXPECT_EQ(0, RendererDRMCalls()->liveRequests);
    // Characterize current behavior: outer swap clears dirty even on commit failure.
    EXPECT_FALSE(s.windowDirty); EXPECT_FALSE(s.videoDirty); EXPECT_FALSE(s.ready);
}
TEST_F(RendererDRM, BlobFailureOmitsModeAndActiveButStillAttemptsCommit)
{
    RendererDRMFailures()->blobResult = -1;
    auto o = RendererDRMGetOptions(); o.modeSet = false; RendererDRMConfigure(o);
    RendererDRMSwap(true);
    EXPECT_EQ(0u, PropertyCount(RD_CRTC_ID, "MODE_ID")); EXPECT_EQ(0u, PropertyCount(RD_CRTC_ID, "ACTIVE"));
    ExpectProperty(RD_CONNECTOR_ID, "CRTC_ID", RD_CRTC_ID);
    EXPECT_EQ(1u, RendererDRMCalls()->commitCount); EXPECT_EQ(0u, RendererDRMCalls()->blobDestroys);
    // A successful commit currently marks mode set even when blob creation failed.
    EXPECT_TRUE(RendererDRMGetState().modeSet); EXPECT_EQ(60, RendererDRMGetState().defaultRate);
}
TEST_F(RendererDRM, BlobDestroyFailureStillReleasesAtomicRequest)
{
    RendererDRMFailures()->blobDestroyResult = -1;
    auto o = RendererDRMGetOptions(); o.modeSet = false; RendererDRMConfigure(o);
    RendererDRMSwap(true);
    EXPECT_TRUE(RendererDRMGetState().modeSet); EXPECT_EQ(1u, RendererDRMCalls()->blobDestroys);
    EXPECT_EQ(1u, RendererDRMCalls()->atomicFrees); EXPECT_EQ(0, RendererDRMCalls()->liveRequests);
    EXPECT_EQ(1, RendererDRMCalls()->liveBlobs); // Failed destruction has not reclaimed the mock blob.
}
TEST_F(RendererDRM, OutputDisableDetachesConnectorAndDeactivatesCrtc)
{
    auto o = RendererDRMGetOptions(); o.output = false; RendererDRMConfigure(o);
    SeedRotation(); RendererDRMSwap(false);
    EXPECT_EQ(3u, RendererDRMCalls()->addCount);
    ExpectProperty(RD_CONNECTOR_ID, "CRTC_ID", 0);
    ExpectProperty(RD_CRTC_ID, "MODE_ID", 0); ExpectProperty(RD_CRTC_ID, "ACTIVE", 0);
    EXPECT_FALSE(RendererDRMGetState().modeSet);
    EXPECT_EQ(DRM_MODE_ATOMIC_ALLOW_MODESET, RendererDRMCalls()->commitFlags);
    EXPECT_EQ(0u, PropertyCount(RD_GRAPHICS_ID, "FB_ID")); EXPECT_EQ(0u, PropertyCount(RD_VIDEO_ID, "FB_ID"));
    // Rotation still occurs while output is disabled; retain ownership until release.
    ExpectFrame(RendererDRMGetState().frames[RD_CURRENT], Frame(11));
}
TEST_F(RendererDRM, AlreadyDisabledOutputDoesNotEmitDuplicateModesetProperties)
{
    auto o = RendererDRMGetOptions(); o.output = false; o.modeSet = false; o.windowDirty = false;
    RendererDRMConfigure(o); RendererDRMSwap(true);
    EXPECT_EQ(0u, RendererDRMCalls()->addCount); EXPECT_EQ(0u, RendererDRMCalls()->commitFlags);
    EXPECT_EQ(0u, RendererDRMCalls()->blobCreates); EXPECT_FALSE(RendererDRMGetState().modeSet);
}
TEST_F(RendererDRM, GraphicsDisableMovesScaledPlaneAlmostEntirelyOffscreen)
{
    auto o = RendererDRMGetOptions(); o.graphics = false; RendererDRMConfigure(o);
    RendererDRMSwap(false);
    ExpectProperty(RD_GRAPHICS_ID, "CRTC_Y", static_cast<uint64_t>(-1078LL));
    ExpectProperty(RD_GRAPHICS_ID, "CRTC_H", 1080);
    ExpectProperty(RD_GRAPHICS_ID, "FB_ID", 901);
}
TEST_F(RendererDRM, ActualSizeCentersSmallerGraphics)
{
    auto o = RendererDRMGetOptions(); o.actualSize = true; RendererDRMConfigure(o);
    RendererDRMSwap(false);
    ExpectProperty(RD_GRAPHICS_ID, "CRTC_X", 320); ExpectProperty(RD_GRAPHICS_ID, "CRTC_Y", 180);
    ExpectProperty(RD_GRAPHICS_ID, "CRTC_W", 1280); ExpectProperty(RD_GRAPHICS_ID, "CRTC_H", 720);
}
TEST_F(RendererDRM, ActualSizeDoesNotCenterOversizedGraphics)
{
    auto o = RendererDRMGetOptions(); o.actualSize = true; o.windowWidth = 2048; o.windowHeight = 1200;
    RendererDRMConfigure(o); RendererDRMSwap(false);
    ExpectProperty(RD_GRAPHICS_ID, "CRTC_X", 0); ExpectProperty(RD_GRAPHICS_ID, "CRTC_Y", 0);
    ExpectProperty(RD_GRAPHICS_ID, "CRTC_W", 2048); ExpectProperty(RD_GRAPHICS_ID, "CRTC_H", 1200);
}
TEST_F(RendererDRM, ZOrderCanBeOmittedOnBothPlanes)
{
    auto o = RendererDRMGetOptions(); o.zpos = false; RendererDRMConfigure(o);
    RendererDRMSetFrame(RD_NEXT, Frame(11)); RendererDRMSwap(false);
    EXPECT_EQ(0u, PropertyCount(RD_GRAPHICS_ID, "zpos")); EXPECT_EQ(0u, PropertyCount(RD_VIDEO_ID, "zpos"));
    EXPECT_EQ(22u, RendererDRMCalls()->addCount);
}
TEST_F(RendererDRM, AtomicLockFailureLeavesGraphicsUnchangedAndCommitsEmptyRequest)
{
    RendererDRMFailures()->failLock = true; RendererDRMSwap(false);
    EXPECT_EQ(1u, RendererDRMCalls()->locks); EXPECT_EQ(0u, RendererDRMCalls()->addFBCount);
    EXPECT_EQ(0u, RendererDRMCalls()->releases); EXPECT_EQ(0u, RendererDRMCalls()->addCount);
    EXPECT_EQ(0u, RendererDRMGetState().bo); EXPECT_EQ(0u, RendererDRMGetState().fb);
    EXPECT_EQ(1u, RendererDRMCalls()->commitCount);
}
TEST_F(RendererDRM, AtomicFramebufferFailureReleasesNewBoAndKeepsOldFramebuffer)
{
    RendererDRMSwap(false);
    auto o = RendererDRMGetOptions(); o.windowDirty = true; RendererDRMConfigure(o);
    RendererDRMFailures()->addFBResult = -1; RendererDRMSwap(false);
    EXPECT_EQ(2u, RendererDRMCalls()->locks); EXPECT_EQ(1u, RendererDRMCalls()->releases);
    EXPECT_EQ(2u, RendererDRMCalls()->releasedBO[0]);
    EXPECT_EQ(901u, RendererDRMGetState().fb); EXPECT_EQ(1u, RendererDRMGetState().bo);
    EXPECT_EQ(0u, RendererDRMGetState().previousFB); EXPECT_EQ(1u, RendererDRMCalls()->outstandingBO);
}
TEST_F(RendererDRM, CleanWindowIsNotLockedOrSubmitted)
{
    Configure(true, false); RendererDRMSwap(false);
    EXPECT_EQ(0u, RendererDRMCalls()->locks); EXPECT_EQ(0u, RendererDRMCalls()->addCount);
    EXPECT_EQ(1u, RendererDRMCalls()->commitCount);
}
TEST_F(RendererDRM, AtomicVideoRotatesAllSlotsAndSubmitsExactGeometry)
{
    Configure(true, false); SeedRotation(); RendererDRMSwap(false);
    const auto s = RendererDRMGetState();
    ExpectFrame(s.frames[RD_CURRENT], Frame(11)); ExpectFrame(s.frames[RD_PREVIOUS], Frame(12));
    ExpectFrame(s.frames[RD_FREE], Frame(13)); ExpectNextConsumed();
    ExpectProperty(RD_VIDEO_ID, "FB_ID", 1011); ExpectProperty(RD_VIDEO_ID, "CRTC_ID", RD_CRTC_ID);
    ExpectProperty(RD_VIDEO_ID, "SRC_X", 0); ExpectProperty(RD_VIDEO_ID, "SRC_Y", 0);
    ExpectProperty(RD_VIDEO_ID, "SRC_W", 1280u << 16); ExpectProperty(RD_VIDEO_ID, "SRC_H", 720u << 16);
    ExpectProperty(RD_VIDEO_ID, "CRTC_X", 0); ExpectProperty(RD_VIDEO_ID, "CRTC_Y", 0);
    ExpectProperty(RD_VIDEO_ID, "CRTC_W", 960); ExpectProperty(RD_VIDEO_ID, "CRTC_H", 540);
    ExpectProperty(RD_VIDEO_ID, "IN_FENCE_FD", UINT64_MAX); ExpectProperty(RD_VIDEO_ID, "zpos", 7);
    EXPECT_FALSE(s.videoDirty); EXPECT_FALSE(s.ready);
}
TEST_F(RendererDRM, AtomicCurrentFrameCanBeRepositionedWithoutRotation)
{
    Configure(true, false); RendererDRMSetFrame(RD_CURRENT, Frame(12, 100, 40));
    RendererDRMSwap(false);
    ExpectProperty(RD_VIDEO_ID, "FB_ID", 1012);
    ExpectProperty(RD_VIDEO_ID, "CRTC_X", 150); ExpectProperty(RD_VIDEO_ID, "CRTC_Y", 60);
    ExpectFrame(RendererDRMGetState().frames[RD_CURRENT], Frame(12, 100, 40));
    EXPECT_EQ(0u, RendererDRMGetState().frames[RD_PREVIOUS].fb);
    EXPECT_EQ(0u, RendererDRMGetState().frames[RD_FREE].fb);
}
TEST_F(RendererDRM, HiddenAtomicVideoMovesToBottomRightWithoutDroppingFrame)
{
    Configure(true, false); auto o = RendererDRMGetOptions(); o.hide = true; RendererDRMConfigure(o);
    RendererDRMSetFrame(RD_NEXT, Frame(11)); RendererDRMSwap(false);
    ExpectProperty(RD_VIDEO_ID, "CRTC_X", 1918); ExpectProperty(RD_VIDEO_ID, "CRTC_Y", 1078);
    ExpectProperty(RD_VIDEO_ID, "FB_ID", 1011); EXPECT_EQ(1011u, RendererDRMGetState().frames[RD_CURRENT].fb);
}
TEST_F(RendererDRM, DisabledVideoRotatesButDoesNotSubmitPlaneProperties)
{
    Configure(true, false); auto o = RendererDRMGetOptions(); o.video = false; RendererDRMConfigure(o);
    SeedRotation(); RendererDRMSwap(false);
    EXPECT_EQ(0u, RendererDRMCalls()->addCount);
    ExpectFrame(RendererDRMGetState().frames[RD_CURRENT], Frame(11)); ExpectNextConsumed();
}

TEST_F(RendererDRM, MatchingGraphicsResolutionLeavesVideoCoordinatesUnscaled)
{
    Configure(true, false); auto o = RendererDRMGetOptions();
    o.windowWidth = 1920; o.windowHeight = 1080; RendererDRMConfigure(o);
    RendererDRMSetFrame(RD_NEXT, Frame(11, 100, 40)); RendererDRMSwap(false);
    ExpectProperty(RD_VIDEO_ID, "CRTC_X", 100); ExpectProperty(RD_VIDEO_ID, "CRTC_Y", 40);
    ExpectProperty(RD_VIDEO_ID, "CRTC_W", 640); ExpectProperty(RD_VIDEO_ID, "CRTC_H", 360);
    ExpectProperty(RD_VIDEO_ID, "SRC_W", 1280u << 16); ExpectProperty(RD_VIDEO_ID, "SRC_H", 720u << 16);
}
TEST_F(RendererDRM, AtomicVideoWithoutGraphicsCharacterizesFallbackHeight)
{
    RendererDRMDestroyWindow();
    auto o = RendererDRMGetOptions(); o.modeSet = true; RendererDRMConfigure(o);
    RendererDRMSetFrame(RD_NEXT, Frame(11, 100, 160)); RendererDRMSwap(false);
    // Production currently uses modeWidth as the graphics-height fallback.
    // Assert observed behavior, not the tempting but incorrect identity scale.
    ExpectProperty(RD_VIDEO_ID, "CRTC_X", 100); ExpectProperty(RD_VIDEO_ID, "CRTC_Y", 90);
    ExpectProperty(RD_VIDEO_ID, "CRTC_W", 640); ExpectProperty(RD_VIDEO_ID, "CRTC_H", 202);
    EXPECT_EQ(0u, RendererDRMCalls()->locks);
}
TEST_F(RendererDRM, AtomicFailedDisableStillClearsModeSetAsCurrentlyImplemented)
{
    auto o = RendererDRMGetOptions(); o.output = false; o.windowDirty = false;
    RendererDRMConfigure(o); RendererDRMFailures()->commitResult = -1;
    RendererDRMSwap(false);
    ExpectProperty(RD_CRTC_ID, "ACTIVE", 0); ExpectProperty(RD_CONNECTOR_ID, "CRTC_ID", 0);
    EXPECT_FALSE(RendererDRMGetState().modeSet); EXPECT_EQ(0, RendererDRMGetState().defaultRate);
    EXPECT_EQ(1u, RendererDRMCalls()->atomicFrees);
}

struct CropCase { int x, y, w, h; unsigned sx, sy, sw, sh, dx, dy, dw, dh; };
class AtomicCrop : public RendererDRM, public testing::WithParamInterface<CropCase> {};
TEST_P(AtomicCrop, ClipsAndScalesExactSourceAndDestinationRectangles)
{
    const auto p = GetParam(); Configure(true, false);
    RendererDRMSetFrame(RD_NEXT, Frame(11, p.x, p.y, p.w, p.h)); RendererDRMSwap(false);
    ExpectProperty(RD_VIDEO_ID, "SRC_X", p.sx << 16); ExpectProperty(RD_VIDEO_ID, "SRC_Y", p.sy << 16);
    ExpectProperty(RD_VIDEO_ID, "SRC_W", p.sw << 16); ExpectProperty(RD_VIDEO_ID, "SRC_H", p.sh << 16);
    ExpectProperty(RD_VIDEO_ID, "CRTC_X", p.dx); ExpectProperty(RD_VIDEO_ID, "CRTC_Y", p.dy);
    ExpectProperty(RD_VIDEO_ID, "CRTC_W", p.dw); ExpectProperty(RD_VIDEO_ID, "CRTC_H", p.dh);
    ExpectNextConsumed(); EXPECT_EQ(1011u, RendererDRMGetState().frames[RD_CURRENT].fb);
}
INSTANTIATE_TEST_SUITE_P(Edges, AtomicCrop, testing::Values(
    CropCase{-100, -50, 640, 360, 200, 100, 1080, 620, 0, 0, 810, 465},
    CropCase{1000, 600, 640, 360, 0, 0, 560, 240, 1500, 900, 420, 180},
    CropCase{-100, -50, 1600, 900, 80, 40, 1024, 576, 0, 0, 1920, 1080},
    CropCase{1500, 900, 640, 360, 0, 0, 0, 0, 2250, 1350, 0, 0}));

class AtomicSkip : public RendererDRM, public testing::WithParamInterface<int> {};
TEST_P(AtomicSkip, IneligiblePlaneNeverConsumesNextFrame)
{
    Configure(true, false); auto o = RendererDRMGetOptions();
    if (GetParam() == 0) o.videoDirty = false;
    if (GetParam() == 1) o.ready = false;
    if (GetParam() == 2) o.inUse = false;
    RendererDRMConfigure(o); RendererDRMSetFrame(RD_NEXT, Frame(11));
    RendererDRMSwap(true);
    EXPECT_EQ(0u, RendererDRMCalls()->addCount);
    ExpectFrame(RendererDRMGetState().frames[RD_NEXT], Frame(11));
    EXPECT_EQ(0u, RendererDRMGetState().frames[RD_CURRENT].fb);
}
INSTANTIATE_TEST_SUITE_P(Eligibility, AtomicSkip, testing::Values(0, 1, 2));

TEST_F(RendererDRM, LegacyFirstSwapProgramsCrtcInsteadOfRequestingPageFlip)
{
    Configure(false); auto o = RendererDRMGetOptions(); o.modeSet = false; RendererDRMConfigure(o);
    RendererDRMSwap(false);
    const auto &t = *RendererDRMCalls();
    EXPECT_EQ(1u, t.setCrtcCount); EXPECT_EQ(0u, t.pageFlips); EXPECT_EQ(0u, t.selects);
    EXPECT_EQ(RD_CRTC_ID, t.crtcId); EXPECT_EQ(901u, t.crtcFB); EXPECT_EQ(RD_CONNECTOR_ID, t.crtcConnector);
    EXPECT_EQ(1920u, t.crtcWidth); EXPECT_EQ(1080u, t.crtcHeight);
    EXPECT_EQ(1920u, t.fbWidth); EXPECT_EQ(1080u, t.fbHeight);
    EXPECT_EQ(32u, t.fbDepth); EXPECT_EQ(32u, t.fbBpp); EXPECT_EQ(4096u, t.fbStride); EXPECT_EQ(501u, t.fbHandle);
    EXPECT_TRUE(RendererDRMGetState().modeSet); EXPECT_FALSE(RendererDRMGetState().windowDirty);
}
TEST_F(RendererDRM, LegacyOverlayPageFlipUsesZeroFramebufferAndSynchronousEvent)
{
    Configure(false); RendererDRMSwap(false);
    const auto &t = *RendererDRMCalls();
    EXPECT_EQ(1u, t.pageFlips); EXPECT_EQ(0u, t.pageFlipFB); EXPECT_EQ(RD_CRTC_ID, t.pageFlipCrtc);
    EXPECT_EQ(DRM_MODE_PAGE_FLIP_EVENT, t.pageFlipFlags);
    ASSERT_EQ(1u, t.planeCount); const auto &p = t.planes[0];
    EXPECT_EQ(RD_GRAPHICS_ID, p.plane); EXPECT_EQ(RD_CRTC_ID, p.crtc); EXPECT_EQ(901u, p.fb);
    EXPECT_EQ(0u, p.flags); EXPECT_EQ(0, p.x); EXPECT_EQ(0, p.y);
    EXPECT_EQ(1920u, p.width); EXPECT_EQ(1080u, p.height);
    EXPECT_EQ(0u, p.sx); EXPECT_EQ(0u, p.sy); EXPECT_EQ(1920u << 16, p.sw); EXPECT_EQ(1080u << 16, p.sh);
    EXPECT_EQ(1u, t.selects); EXPECT_EQ(124, t.selectNfds);
    EXPECT_TRUE(t.selectSawDRM); EXPECT_TRUE(t.selectSawStdin); EXPECT_TRUE(t.selectNoTimeout);
    EXPECT_EQ(1u, t.events); EXPECT_EQ(2u, t.eventVersion); EXPECT_EQ(0, RendererDRMGetState().flipPending);
}
TEST_F(RendererDRM, LegacyWithoutWindowPlaneFlipsFramebufferDirectly)
{
    Configure(false); RendererDRMDetachWindowPlane(); RendererDRMSwap(false);
    EXPECT_EQ(901u, RendererDRMCalls()->pageFlipFB); EXPECT_EQ(0u, RendererDRMCalls()->planeCount);
    EXPECT_EQ(1u, RendererDRMCalls()->events); EXPECT_EQ(0, RendererDRMGetState().flipPending);
}
TEST_F(RendererDRM, LegacyPageFlipFailureSkipsEventWaitButStillSetsPlane)
{
    Configure(false); RendererDRMFailures()->pageFlipResult = -1; RendererDRMSwap(false);
    EXPECT_EQ(1u, RendererDRMCalls()->planeCount); EXPECT_EQ(0u, RendererDRMCalls()->selects);
    EXPECT_EQ(0u, RendererDRMCalls()->events); EXPECT_EQ(0, RendererDRMGetState().flipPending);
}
class LegacySelect : public RendererDRM, public testing::WithParamInterface<int> {};
TEST_P(LegacySelect, UnreadableOrFailedSelectDoesNotInventPageEvent)
{
    Configure(false); RendererDRMFailures()->selectResult = GetParam();
    RendererDRMFailures()->selectReady = false; RendererDRMSwap(false);
    EXPECT_EQ(1u, RendererDRMCalls()->selects); EXPECT_EQ(0u, RendererDRMCalls()->events);
    EXPECT_EQ(1, RendererDRMGetState().flipPending);
    RendererDRMPageEvent(1, 1); EXPECT_EQ(0, RendererDRMGetState().flipPending);
}
INSTANTIATE_TEST_SUITE_P(SelectResults, LegacySelect, testing::Values(-1, 0, 1));
TEST_F(RendererDRM, LegacyGraphicsSetPlaneFailureStillConsumesItsPageEvent)
{
    Configure(false); RendererDRMFailures()->setPlaneResult = -1; RendererDRMSwap(false);
    EXPECT_EQ(1u, RendererDRMCalls()->planeCount); EXPECT_EQ(1u, RendererDRMCalls()->events);
    EXPECT_EQ(0, RendererDRMGetState().flipPending); EXPECT_EQ(901u, RendererDRMGetState().fb);
}
TEST_F(RendererDRM, LegacyFramebufferFailureExitsBeforeModesetAndRetainsDirty)
{
    Configure(false); RendererDRMFailures()->addFBResult = -1; RendererDRMSwap(false);
    EXPECT_EQ(0u, RendererDRMCalls()->pageFlips); EXPECT_EQ(0u, RendererDRMCalls()->setCrtcCount);
    EXPECT_EQ(0u, RendererDRMGetState().fb); EXPECT_EQ(0u, RendererDRMGetState().bo);
    EXPECT_TRUE(RendererDRMGetState().windowDirty);
    // Production does not release the locked BO on this error; fixture reclaims it after assertions.
    EXPECT_EQ(0u, RendererDRMCalls()->releases); EXPECT_EQ(1u, RendererDRMCalls()->outstandingBO);
}
TEST_F(RendererDRM, LegacyCrtcFailureDoesNotMarkModeSetOrClearDirty)
{
    Configure(false); auto o = RendererDRMGetOptions(); o.modeSet = false; RendererDRMConfigure(o);
    RendererDRMFailures()->setCrtcResult = -1; RendererDRMSwap(false);
    EXPECT_FALSE(RendererDRMGetState().modeSet); EXPECT_TRUE(RendererDRMGetState().windowDirty);
    EXPECT_EQ(901u, RendererDRMGetState().fb); EXPECT_EQ(1u, RendererDRMGetState().bo);
    EXPECT_EQ(0u, RendererDRMCalls()->pageFlips);
}
TEST_F(RendererDRM, LegacyVideoClipsNegativeOriginScalesAndRotatesFrames)
{
    Configure(false, false); SeedRotation();
    const auto f = Frame(11, -100, -50); RendererDRMSetFrame(RD_NEXT, f); RendererDRMSwap(false);
    ASSERT_EQ(1u, RendererDRMCalls()->planeCount); const auto &p = RendererDRMCalls()->planes[0];
    EXPECT_EQ(RD_VIDEO_ID, p.plane); EXPECT_EQ(RD_CRTC_ID, p.crtc); EXPECT_EQ(1011u, p.fb);
    EXPECT_EQ(0, p.x); EXPECT_EQ(0, p.y); EXPECT_EQ(810u, p.width); EXPECT_EQ(465u, p.height);
    EXPECT_EQ(200u << 16, p.sx); EXPECT_EQ(100u << 16, p.sy);
    EXPECT_EQ(1080u << 16, p.sw); EXPECT_EQ(620u << 16, p.sh);
    ExpectFrame(RendererDRMGetState().frames[RD_CURRENT], f);
    ExpectFrame(RendererDRMGetState().frames[RD_PREVIOUS], Frame(12));
    ExpectFrame(RendererDRMGetState().frames[RD_FREE], Frame(13)); ExpectNextConsumed();
}
TEST_F(RendererDRM, LegacyVideoDoesNotApplyAtomicRightAndBottomEdgeCropping)
{
    Configure(false, false); RendererDRMSetFrame(RD_NEXT, Frame(11, 1000, 600)); RendererDRMSwap(false);
    ASSERT_EQ(1u, RendererDRMCalls()->planeCount); const auto &p = RendererDRMCalls()->planes[0];
    EXPECT_EQ(1500, p.x); EXPECT_EQ(900, p.y); EXPECT_EQ(960u, p.width); EXPECT_EQ(540u, p.height);
    EXPECT_EQ(1280u << 16, p.sw); EXPECT_EQ(720u << 16, p.sh);
    EXPECT_EQ(0u, p.sx); EXPECT_EQ(0u, p.sy);
    ExpectNextConsumed();
}
TEST_F(RendererDRM, LegacyCurrentFrameWithoutNextFrameIsNotResubmitted)
{
    Configure(false, false); RendererDRMSetFrame(RD_CURRENT, Frame(12)); RendererDRMSwap(false);
    EXPECT_EQ(0u, RendererDRMCalls()->planeCount);
    ExpectFrame(RendererDRMGetState().frames[RD_CURRENT], Frame(12));
    EXPECT_FALSE(RendererDRMGetState().ready); EXPECT_FALSE(RendererDRMGetState().videoDirty);
}
TEST_F(RendererDRM, LegacyVideoWithMatchingResolutionDoesNotScale)
{
    Configure(false, false); auto o = RendererDRMGetOptions();
    o.windowWidth = 1920; o.windowHeight = 1080; RendererDRMConfigure(o);
    RendererDRMSetFrame(RD_NEXT, Frame(11, 100, 40)); RendererDRMSwap(false);
    ASSERT_EQ(1u, RendererDRMCalls()->planeCount); const auto &p = RendererDRMCalls()->planes[0];
    EXPECT_EQ(100, p.x); EXPECT_EQ(40, p.y); EXPECT_EQ(640u, p.width); EXPECT_EQ(360u, p.height);
    EXPECT_EQ(1280u << 16, p.sw); EXPECT_EQ(720u << 16, p.sh);
}
TEST_F(RendererDRM, LegacyVideoFailureClosesBothHandlesAndAllThreeDescriptors)
{
    Configure(false, false); RendererDRMSetFrame(RD_NEXT, Frame(11));
    RendererDRMFailures()->setPlaneResult = -1; const auto closes = RendererCalls()->closeCalls;
    RendererDRMSwap(false);
    ASSERT_EQ(1u, RendererDRMCalls()->rmCount); EXPECT_EQ(1011u, RendererDRMCalls()->removedFB[0]);
    ASSERT_EQ(2u, RendererDRMCalls()->ioctlCount);
    EXPECT_EQ(2011u, RendererDRMCalls()->closedHandles[0]); EXPECT_EQ(3011u, RendererDRMCalls()->closedHandles[1]);
    EXPECT_EQ(closes + 3, RendererCalls()->closeCalls); ExpectNextConsumed();
    // The current slot retains stale metadata on failure; do not pretend rollback occurred.
    ExpectFrame(RendererDRMGetState().frames[RD_CURRENT], Frame(11));
}
TEST_F(RendererDRM, LegacyFailedVideoWithSharedHandleClosesHandleOnlyOnce)
{
    Configure(false, false); auto f = Frame(11); f.handle1 = f.handle0; f.fd0 = f.fd1 = f.fd2 = -1;
    RendererDRMSetFrame(RD_NEXT, f); RendererDRMFailures()->setPlaneResult = -1;
    RendererDRMFailures()->ioctlResult = -1; const auto closes = RendererCalls()->closeCalls;
    RendererDRMSwap(false);
    EXPECT_EQ(1u, RendererDRMCalls()->ioctlCount); EXPECT_EQ(2011u, RendererDRMCalls()->closedHandles[0]);
    EXPECT_EQ(closes, RendererCalls()->closeCalls); EXPECT_EQ(1u, RendererDRMCalls()->rmCount);
}
class LegacyHide : public RendererDRM, public testing::WithParamInterface<int> {};
TEST_P(LegacyHide, HideDetachesFramebufferAndPreservesPreviousFramesEvenOnError)
{
    Configure(false, false); RendererDRMSetFrame(RD_CURRENT, Frame(12)); RendererDRMSetFrame(RD_PREVIOUS, Frame(13));
    auto o = RendererDRMGetOptions(); o.nextHide = true; RendererDRMConfigure(o);
    RendererDRMFailures()->setPlaneResult = GetParam(); RendererDRMSwap(false);
    ASSERT_EQ(1u, RendererDRMCalls()->planeCount); const auto &p = RendererDRMCalls()->planes[0];
    EXPECT_EQ(RD_VIDEO_ID, p.plane); EXPECT_EQ(0u, p.fb); EXPECT_EQ(RD_CRTC_ID, p.crtc);
    EXPECT_EQ(1920u, p.width); EXPECT_EQ(1080u, p.height);
    EXPECT_EQ(1920u << 16, p.sw); EXPECT_EQ(1080u << 16, p.sh);
    const auto s = RendererDRMGetState();
    ExpectFrame(s.frames[RD_PREVIOUS], Frame(12)); ExpectFrame(s.frames[RD_FREE], Frame(13));
    EXPECT_EQ(0u, s.frames[RD_CURRENT].fb); EXPECT_EQ(-1, s.frames[RD_CURRENT].id);
    EXPECT_EQ(0u, s.frames[RD_CURRENT].handle0); EXPECT_EQ(0u, s.frames[RD_CURRENT].handle1);
    EXPECT_EQ(-1, s.frames[RD_CURRENT].fd0); EXPECT_EQ(-1, s.frames[RD_CURRENT].fd1); EXPECT_EQ(-1, s.frames[RD_CURRENT].fd2);
    EXPECT_TRUE(s.nextHide); EXPECT_FALSE(s.nextHidden); EXPECT_FALSE(s.ready); EXPECT_FALSE(s.videoDirty);
}
INSTANTIATE_TEST_SUITE_P(SetPlaneResults, LegacyHide, testing::Values(0, -1));
TEST_F(RendererDRM, AlreadyHiddenLegacyPlaneIsNotSubmittedAgain)
{
    Configure(false, false); auto o = RendererDRMGetOptions(); o.nextHide = o.nextHidden = true;
    RendererDRMConfigure(o); RendererDRMSwap(false);
    EXPECT_EQ(0u, RendererDRMCalls()->planeCount); EXPECT_FALSE(RendererDRMGetState().nextHidden);
    EXPECT_FALSE(RendererDRMGetState().videoDirty); EXPECT_FALSE(RendererDRMGetState().ready);
}
TEST_F(RendererDRM, PendingModeIsConsumedThroughAtomicDispatcherAndNotificationCompletes)
{
    auto o = RendererDRMGetOptions(); o.modePending = true; RendererDRMConfigure(o);
    RendererDRMSwap(false);
    EXPECT_EQ(DRM_MODE_ATOMIC_ALLOW_MODESET, RendererDRMCalls()->commitFlags);
    EXPECT_EQ(1u, RendererDRMCalls()->blobCreates);
    EXPECT_FALSE(RendererDRMGetState().modePending); EXPECT_FALSE(RendererDRMGetState().notifySize);
    EXPECT_TRUE(RendererDRMGetState().modeSet); EXPECT_EQ(60, RendererDRMGetState().defaultRate);
}
TEST_F(RendererDRM, MissingConnectorAndFailedRefreshExitBeforeAnyHardwareWork)
{
    RendererDRMDetachConnector(); RendererDRMSwap(false);
    EXPECT_EQ(0u, RendererDRMCalls()->atomicAllocs); EXPECT_EQ(0u, RendererDRMCalls()->locks);
    EXPECT_EQ(0u, RendererDRMCalls()->pageFlips); EXPECT_TRUE(RendererDRMGetState().windowDirty);
    EXPECT_GT(RendererCalls()->resourceQueries, 0u);
    EXPECT_GT(RendererCalls()->connectorQueries, 1u);
}
TEST_F(RendererDRM, ReleasePreviousGraphicsRemovesOldFramebufferAndReturnsExactlyOldBo)
{
    RendererDRMSwap(false); const auto first = RendererDRMGetState();
    auto o = RendererDRMGetOptions(); o.windowDirty = true; RendererDRMConfigure(o); RendererDRMSwap(false);
    auto s = RendererDRMGetState();
    EXPECT_EQ(first.fb, s.previousFB); EXPECT_EQ(first.bo, s.previousBO);
    EXPECT_EQ(902u, s.fb); EXPECT_EQ(2u, s.bo);
    RendererDRMReleasePrevious(); s = RendererDRMGetState();
    EXPECT_EQ(0u, s.previousFB); EXPECT_EQ(0u, s.previousBO); EXPECT_EQ(902u, s.fb); EXPECT_EQ(2u, s.bo);
    ASSERT_EQ(1u, RendererDRMCalls()->rmCount); EXPECT_EQ(901u, RendererDRMCalls()->removedFB[0]);
    ASSERT_EQ(1u, RendererDRMCalls()->releases); EXPECT_EQ(1u, RendererDRMCalls()->releasedBO[0]);
    RendererDRMReleasePrevious(); EXPECT_EQ(1u, RendererDRMCalls()->rmCount); EXPECT_EQ(1u, RendererDRMCalls()->releases);
}
TEST_F(RendererDRM, ReleaseFreeVideoQueuesOwnershipThenSynchronouslyClosesResources)
{
    Configure(true, false); SeedRotation(); RendererDRMSwap(false);
    RendererDRMReleasePrevious(); auto s = RendererDRMGetState();
    EXPECT_EQ(1, s.queuedReleases); EXPECT_EQ(-1, s.frames[RD_FREE].id); EXPECT_EQ(0u, s.frames[RD_FREE].fb);
    EXPECT_EQ(0u, s.frames[RD_FREE].handle0); EXPECT_EQ(0u, s.frames[RD_FREE].handle1);
    EXPECT_EQ(-1, s.frames[RD_FREE].fd0); EXPECT_EQ(-1, s.frames[RD_FREE].fd1); EXPECT_EQ(-1, s.frames[RD_FREE].fd2);
    EXPECT_EQ(0u, RendererDRMCalls()->rmCount);
    const auto closes = RendererCalls()->closeCalls;
    RendererDRMDrainReleases();
    EXPECT_EQ(0, RendererDRMGetState().queuedReleases);
    ASSERT_EQ(1u, RendererDRMCalls()->releasedFrames); EXPECT_EQ(13u, RendererDRMCalls()->releasedFrameIds[0]);
    ASSERT_EQ(1u, RendererDRMCalls()->rmCount); EXPECT_EQ(1013u, RendererDRMCalls()->removedFB[0]);
    ASSERT_EQ(2u, RendererDRMCalls()->ioctlCount);
    EXPECT_EQ(2013u, RendererDRMCalls()->closedHandles[0]); EXPECT_EQ(3013u, RendererDRMCalls()->closedHandles[1]);
    EXPECT_EQ(closes + 3, RendererCalls()->closeCalls);
    RendererDRMReleasePrevious(); RendererDRMDrainReleases();
    EXPECT_EQ(1u, RendererDRMCalls()->releasedFrames); EXPECT_EQ(1u, RendererDRMCalls()->rmCount);
    ExpectFrame(RendererDRMGetState().frames[RD_CURRENT], Frame(11));
}
TEST_F(RendererDRM, FailedPreviousFramebufferRemovalStillReturnsBoAndClearsPreviousState)
{
    RendererDRMSwap(false); auto o = RendererDRMGetOptions(); o.windowDirty = true; RendererDRMConfigure(o);
    RendererDRMSwap(false); RendererDRMFailures()->rmFBResult = -1;
    RendererDRMReleasePrevious();
    EXPECT_EQ(1u, RendererDRMCalls()->releases); EXPECT_EQ(1u, RendererDRMCalls()->releasedBO[0]);
    EXPECT_EQ(0u, RendererDRMGetState().previousBO); EXPECT_EQ(0u, RendererDRMGetState().previousFB);
    EXPECT_EQ(2u, RendererDRMCalls()->outstandingFB); // The failed removal remains fixture-owned.
}
TEST_F(RendererDRM, ReleasePreviousWithNoBuffersDoesNotQueueOrCloseAnything)
{
    const auto closes = RendererCalls()->closeCalls;
    RendererDRMReleasePrevious(); RendererDRMDrainReleases();
    EXPECT_EQ(0u, RendererDRMCalls()->releasedFrames); EXPECT_EQ(0u, RendererDRMCalls()->rmCount);
    EXPECT_EQ(0u, RendererDRMCalls()->releases); EXPECT_EQ(0u, RendererDRMCalls()->ioctlCount);
    EXPECT_EQ(closes, RendererCalls()->closeCalls); EXPECT_EQ(-1, RendererDRMGetState().frames[RD_FREE].id);
}
TEST_F(RendererDRM, PageFlipHandlerNeverUnderflowsPendingCount)
{
    RendererDRMPageEvent(0, 1); EXPECT_EQ(0, RendererDRMGetState().flipPending);
    RendererDRMPageEvent(3, 1); EXPECT_EQ(2, RendererDRMGetState().flipPending);
    RendererDRMPageEvent(2, 2); EXPECT_EQ(0, RendererDRMGetState().flipPending);
    RendererDRMPageEvent(1, 4); EXPECT_EQ(0, RendererDRMGetState().flipPending);
}
TEST_F(RendererDRM, WindowDestructionReturnsPreviousBufferAndResetsLastWindowMode)
{
    RendererDRMSwap(false); auto o = RendererDRMGetOptions(); o.windowDirty = true; RendererDRMConfigure(o);
    RendererDRMSwap(false); RendererDRMDestroyWindow();
    EXPECT_EQ(0u, RendererWindowCount()); EXPECT_FALSE(RendererModeIsSet());
    EXPECT_EQ(1u, RendererCalls()->surfaceDestroys);
    ASSERT_EQ(1u, RendererDRMCalls()->releases); EXPECT_EQ(1u, RendererDRMCalls()->releasedBO[0]);
    ASSERT_EQ(1u, RendererDRMCalls()->rmCount); EXPECT_EQ(901u, RendererDRMCalls()->removedFB[0]);
    // Current BO is not released by production destruction. Explicitly expose that fact.
    EXPECT_EQ(1u, RendererDRMCalls()->outstandingBO);
}
TEST_F(RendererDRM, FullTeardownFreesAcquiredPropertiesPlanesAndDevice)
{
    RendererDRMSwap(false); RendererDRMDestroy();
    const auto &t = *RendererDRMCalls();
    EXPECT_EQ(29u, t.propertyFrees); EXPECT_EQ(4u, t.objectFrees);
    EXPECT_EQ(2u, t.planeFrees); EXPECT_EQ(1u, t.crtcFrees); EXPECT_EQ(1u, t.encoderFrees);
    EXPECT_EQ(1u, t.deviceDestroys); EXPECT_EQ(1u, RendererCalls()->surfaceDestroys);
    EXPECT_EQ(0u, t.outstandingBO); EXPECT_FALSE(RendererHasContext());
}
} // namespace