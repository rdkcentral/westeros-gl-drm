#include <gtest/gtest.h>
#include "renderer_init_support.h"
#include "renderer_drm_support.h"
#include "renderer_test_support.h"
#include <xf86drm.h>
#include <sys/socket.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
struct InitSavedEnvironment {
    std::string name, value;
    bool present;
};
class RendererInitialization : public ::testing::Test {
protected:
    std::vector<InitSavedEnvironment> environment;
    bool active = false;
    RendererDRMTrace before{};
    RendererInitConfig &config() { return *RendererInitSettings(); }
    const RendererInitTrace &calls() { return *RendererInitCalls(); }
    void SetUp() override
    {
        // Cover every getenv reached by init, public lookup, mode selection,
        // and service startup. No inherited AV_PROGRESSION file is opened.
        for (const char *name : {"WESTEROS_GL_DEBUG", "WESTEROS_GL_DEBUG_FRAME",
             "WESTEROS_GL_GRAPHICS_MAX_SIZE", "WESTEROS_GL_FPS", "WESTEROS_DRM_CARD",
             "WESTEROS_GL_NO_PLANES", "WESTEROS_GL_USE_GBM_MODIFIERS",
             "WESTEROS_GL_NO_VIDEOSERVER", "WESTEROS_GL_NO_VBLANK", "WESTEROS_SECURE_GRAPHICS",
             "WESTEROS_GL_GLOBAL_ZOOM_ACTIVE", "WESTEROS_GL_ALLOW_4K_ZOOM",
             "WESTEROS_GL_USE_GENERIC_AVSYNC", "WESTEROS_GL_USE_REFRESH_LOCK",
             "WESTEROS_GL_MAX_MODE", "WESTEROS_GL_USE_PREFERRED_MODE", "WESTEROS_GL_USE_BEST_MODE",
             "AV_PROGRESSION", "XDG_RUNTIME_DIR"})
        {
            const char *value = getenv(name);
            environment.push_back({name, value ? value : "", value != nullptr});
            ASSERT_EQ(0, unsetenv(name));
        }
        RendererInitReset(); active = true;
        before = *RendererDRMCalls();
    }
    void TearDown() override
    {
        if (active)
        {
            RendererInitEnd();
            EXPECT_EQ(0, calls().liveResources);
            EXPECT_EQ(0, calls().liveConnectors);
            EXPECT_EQ(before.liveObjects, RendererDRMCalls()->liveObjects);
            EXPECT_EQ(before.liveProperties, RendererDRMCalls()->liveProperties);
        }
        for (const auto &entry : environment)
        {
            if (entry.present) EXPECT_EQ(0, setenv(entry.name.c_str(), entry.value.c_str(), 1));
            else EXPECT_EQ(0, unsetenv(entry.name.c_str()));
        }
    }
    void runtimeDirectory() { ASSERT_EQ(0, setenv("XDG_RUNTIME_DIR", "/l1-not-a-real-directory", 1)); }
};
} // namespace

TEST_F(RendererInitialization, InternalOpenFailureNeverQueriesHardware)
{
    config().openResult = -1;
    ASSERT_EQ(0, setenv("WESTEROS_DRM_CARD", "/l1/no-real-drm", 1));
    EXPECT_FALSE(RendererInitContext(false));
    EXPECT_FALSE(RendererInitGetState().present);
    EXPECT_STREQ("/l1/no-real-drm", calls().cardPath);
    EXPECT_EQ(1u, calls().opens);
    EXPECT_EQ(0u, calls().masterCalls);
    EXPECT_EQ(0u, calls().resourceQueries);
    EXPECT_EQ(0u, RendererCalls()->closeCalls);
}

TEST_F(RendererInitialization, ResourceFailureClosesOnlySyntheticCard)
{
    config().failResources = true;
    EXPECT_FALSE(RendererInitContext(false));
    EXPECT_EQ(1u, calls().masterCalls);
    EXPECT_EQ(RI_CARD_FD, calls().lastMasterFd);
    EXPECT_EQ(1u, calls().versionQueries);
    EXPECT_EQ(1u, calls().versionFrees);
    EXPECT_EQ(1u, calls().resourceQueries);
    EXPECT_EQ(0u, calls().gbmCreates);
    EXPECT_EQ(1u, RendererCalls()->closeCalls);
    EXPECT_EQ(0u, calls().threads);
}

TEST_F(RendererInitialization, GbmFailureReleasesDiscoveredConnectorAndResources)
{
    config().failGBM = true;
    EXPECT_FALSE(RendererInitContext(false));
    EXPECT_EQ(RI_CARD_FD, calls().lastGBMFd);
    EXPECT_EQ(1u, calls().connectorFrees);
    EXPECT_EQ(1u, calls().resourceFrees);
    EXPECT_EQ(0u, calls().encoderQueries);
    EXPECT_EQ(before.deviceDestroys, RendererDRMCalls()->deviceDestroys);
    EXPECT_EQ(1u, RendererCalls()->closeCalls);
}

TEST_F(RendererInitialization, NonMasterSuccessDiscoversCurrentModeWithoutThreads)
{
    ASSERT_TRUE(RendererInitContext(false));
    const auto state = RendererInitGetState();
    EXPECT_TRUE(state.present);
    EXPECT_FALSE(state.master);
    EXPECT_FALSE(state.atomic);
    EXPECT_TRUE(state.output && state.graphics && state.video);
    EXPECT_TRUE(state.videoServer && state.vblank && state.allow4k);
    EXPECT_FALSE(state.secure || state.globalZoom || state.zpos);
    EXPECT_EQ(-1, state.zoomMode);
    EXPECT_EQ(-1, state.videoDebugLevel);
    EXPECT_EQ(RD_CONNECTOR_ID, state.connectorId);
    EXPECT_EQ(21u, state.encoderId);
    EXPECT_EQ(RD_CRTC_ID, state.crtcId);
    EXPECT_EQ(1920, state.width);
    EXPECT_EQ(1080, state.height);
    EXPECT_EQ(60, state.rate);
    EXPECT_EQ(0u, calls().threads);
    EXPECT_EQ(0u, calls().sockets);
    EXPECT_EQ(2u, calls().crtcQueries);
    RendererInitDestroyContext();
    EXPECT_EQ(before.deviceDestroys + 1, RendererDRMCalls()->deviceDestroys);
    EXPECT_EQ(before.encoderFrees + 1, RendererDRMCalls()->encoderFrees);
    EXPECT_EQ(before.crtcFrees + 2, RendererDRMCalls()->crtcFrees);
    EXPECT_EQ(1u, calls().resourceFrees);
    EXPECT_EQ(1u, RendererCalls()->closeCalls);
}

TEST_F(RendererInitialization, PublicSuccessKeepsThreadFailuresNonFatalAndReferenceCounts)
{
    config().masterResult = 0;
    ASSERT_EQ(0, setenv("WESTEROS_GL_NO_PLANES", "1", 1));
    ASSERT_EQ(0, setenv("WESTEROS_GL_DEBUG", "3", 1));
    ASSERT_EQ(0, setenv("WESTEROS_GL_DEBUG_FRAME", "1", 1));
    ASSERT_TRUE(RendererInitContext(true));
    auto state = RendererInitGetState();
    EXPECT_TRUE(state.master);
    EXPECT_FALSE(state.planes || state.refreshStarted || state.offloadStarted);
    EXPECT_EQ(3, state.debugLevel);
    EXPECT_TRUE(state.frameDebug);
    EXPECT_EQ(1u, calls().offloadThreads);
    EXPECT_EQ(1u, calls().refreshThreads);
    EXPECT_EQ(2u, calls().threads);
    EXPECT_EQ(0u, calls().capCount);
    EXPECT_EQ(0u, calls().sockets);
    EXPECT_EQ(4u, RendererCalls()->symbolQueries);
    auto *same = WstGLInit();
    ASSERT_NE(nullptr, same);
    EXPECT_EQ(2, RendererReferenceCount());
    EXPECT_EQ(1u, calls().opens);
    EXPECT_EQ(4u, RendererCalls()->symbolQueries);
    WstGLTerm(same);
    EXPECT_EQ(1, RendererReferenceCount());
    EXPECT_EQ(0u, RendererCalls()->closeCalls);
    // Fixture destroys only the semaphore actually initialized by offload
    // startup; production context teardown then closes the synthetic device.
    RendererInitDestroyContext();
    EXPECT_EQ(1u, RendererCalls()->closeCalls);
    EXPECT_FALSE(RendererHasContext());
}

TEST_F(RendererInitialization, FlagsOverrideDefaultsAndGraphicsLimit)
{
    for (const char *name : {"WESTEROS_GL_NO_PLANES", "WESTEROS_GL_NO_VIDEOSERVER",
         "WESTEROS_GL_NO_VBLANK", "WESTEROS_GL_FPS", "WESTEROS_SECURE_GRAPHICS",
         "WESTEROS_GL_GLOBAL_ZOOM_ACTIVE"}) ASSERT_EQ(0, setenv(name, "1", 1));
    ASSERT_EQ(0, setenv("WESTEROS_GL_ALLOW_4K_ZOOM", "0", 1));
    ASSERT_EQ(0, setenv("WESTEROS_GL_GRAPHICS_MAX_SIZE", "1280x720", 1));
    ASSERT_TRUE(RendererInitContext(false));
    const auto s = RendererInitGetState();
    EXPECT_FALSE(s.planes || s.videoServer || s.vblank || s.allow4k);
    EXPECT_TRUE(s.secure && s.globalZoom && s.fps);
    EXPECT_EQ(1280, s.maxWidth); EXPECT_EQ(720, s.maxHeight);
    EXPECT_EQ(0u, calls().capCount);
    EXPECT_EQ(0u, calls().planeResourceQueries);
}

class RendererInitInvalidSize : public RendererInitialization,
    public ::testing::WithParamInterface<const char *> {};
TEST_P(RendererInitInvalidSize, DoesNotInstallInvalidGraphicsLimit)
{
    ASSERT_EQ(0, setenv("WESTEROS_GL_GRAPHICS_MAX_SIZE", GetParam(), 1));
    ASSERT_TRUE(RendererInitContext(false));
    EXPECT_EQ(0, RendererInitGetState().maxWidth);
    EXPECT_EQ(0, RendererInitGetState().maxHeight);
}
INSTANTIATE_TEST_SUITE_P(InvalidLimits, RendererInitInvalidSize,
    ::testing::Values("garbage", "0x720", "1280x0", "-1x720"));

TEST_F(RendererInitialization, ExplicitZeroFlagsPreserveFalseAndEnable4k)
{
    ASSERT_EQ(0, setenv("WESTEROS_SECURE_GRAPHICS", "0", 1));
    ASSERT_EQ(0, setenv("WESTEROS_GL_GLOBAL_ZOOM_ACTIVE", "0", 1));
    ASSERT_EQ(0, setenv("WESTEROS_GL_ALLOW_4K_ZOOM", "1", 1));
    ASSERT_TRUE(RendererInitContext(false));
    auto s = RendererInitGetState();
    EXPECT_FALSE(s.secure || s.globalZoom);
    EXPECT_TRUE(s.allow4k);
}

TEST_F(RendererInitialization, Vc4DriverEnablesZPositionPolicy)
{
    std::strcpy(config().driver, "vc4");
    ASSERT_TRUE(RendererInitContext(false));
    EXPECT_TRUE(RendererInitGetState().zpos);
    EXPECT_EQ(1u, calls().versionFrees);
}

TEST_F(RendererInitialization, MissingDriverVersionIsNonFatal)
{
    config().failVersion = true;
    ASSERT_TRUE(RendererInitContext(false));
    EXPECT_FALSE(RendererInitGetState().zpos);
    EXPECT_EQ(1u, calls().versionQueries);
    EXPECT_EQ(0u, calls().versionFrees);
}

TEST_F(RendererInitialization, SkipsMissingDisconnectedAndModelessConnectors)
{
    config().connectorCount = 4;
    config().connectors[0] = {10, 21, true, true, 2};
    config().connectors[1] = {11, 21, false, false, 2};
    config().connectors[2] = {12, 21, false, true, 0};
    config().connectors[3] = {RD_CONNECTOR_ID, 21, false, true, 2};
    ASSERT_TRUE(RendererInitContext(false));
    EXPECT_EQ(RD_CONNECTOR_ID, RendererInitGetState().connectorId);
    ASSERT_EQ(4u, calls().connectorQueries);
    EXPECT_EQ(10u, calls().connectorIds[0]); EXPECT_EQ(11u, calls().connectorIds[1]);
    EXPECT_EQ(12u, calls().connectorIds[2]); EXPECT_EQ(RD_CONNECTOR_ID, calls().connectorIds[3]);
    EXPECT_EQ(2u, calls().connectorFrees);
    RendererInitDestroyContext();
    EXPECT_EQ(3u, calls().connectorFrees);
}

TEST_F(RendererInitialization, NoConnectorStillCreatesDeviceAndFindsCompatibleEncoder)
{
    config().connectorCount = 0;
    ASSERT_TRUE(RendererInitContext(false));
    EXPECT_EQ(0u, RendererInitGetState().connectorId);
    EXPECT_EQ(21u, RendererInitGetState().encoderId);
    EXPECT_EQ(RD_CRTC_ID, RendererInitGetState().encoderCrtcId);
    EXPECT_EQ(2u, calls().encoderQueries);
    EXPECT_EQ(0u, calls().connectorQueries);
}

TEST_F(RendererInitialization, EncoderSearchReplacesCurrentSkipsFailuresAndRetargetsCrtc)
{
    config().encoderCount = 3;
    config().encoders[0] = {20, 0, 0, true};
    config().encoders[1] = {21, 0, 1, false};
    config().encoders[2] = {22, 0, 2, false};
    EXPECT_EQ(22u, RendererInitFindEncoder(1, 77, true));
    EXPECT_EQ(77u, calls().selectedEncoderCrtc);
    ASSERT_EQ(3u, calls().encoderQueries);
    EXPECT_EQ(20u, calls().encoderIds[0]); EXPECT_EQ(21u, calls().encoderIds[1]);
    EXPECT_EQ(22u, calls().encoderIds[2]);
    EXPECT_EQ(before.encoderFrees + 3, RendererDRMCalls()->encoderFrees);
}

TEST_F(RendererInitialization, EncoderSearchExhaustionReleasesRejectedEncoder)
{
    config().encoders[0].possibleCrtcs = 1;
    EXPECT_EQ(0u, RendererInitFindEncoder(1, 77, false));
    EXPECT_EQ(0u, calls().selectedEncoderCrtc);
    EXPECT_EQ(1u, calls().encoderQueries);
    EXPECT_EQ(before.encoderFrees + 1, RendererDRMCalls()->encoderFrees);
}

TEST_F(RendererInitialization, InitFallsBackAcrossCrtcsWhenConnectorEncoderDoesNotMatch)
{
    config().connectors[0].encoder = 99;
    config().encoders[0].possibleCrtcs = 2;
    config().crtcCount = 2; config().crtcs[1] = 32;
    ASSERT_TRUE(RendererInitContext(false));
    EXPECT_EQ(21u, RendererInitGetState().encoderId);
    EXPECT_EQ(32u, RendererInitGetState().encoderCrtcId);
    EXPECT_EQ(32u, RendererInitGetState().crtcId);
    EXPECT_EQ(3u, calls().crtcQueries);
    EXPECT_EQ(2u, calls().encoderQueries);
}

TEST_F(RendererInitialization, MissingEncoderContinuesToNextCandidate)
{
    config().encoderCount = 2;
    config().encoders[0].missing = true;
    config().encoders[1] = {22, RD_CRTC_ID, 1, false};
    config().connectors[0].encoder = 22;
    ASSERT_TRUE(RendererInitContext(false));
    EXPECT_EQ(22u, RendererInitGetState().encoderId);
    EXPECT_EQ(2u, calls().encoderQueries);
}

TEST_F(RendererInitialization, IncompatibleEncoderIsReleasedAndNoCrtcIsQueried)
{
    config().connectors[0].encoder = 99;
    config().encoders[0].possibleCrtcs = 0;
    ASSERT_TRUE(RendererInitContext(false));
    EXPECT_EQ(0u, RendererInitGetState().encoderId);
    EXPECT_EQ(0u, calls().crtcQueries);
    EXPECT_EQ(before.encoderFrees + 1, RendererDRMCalls()->encoderFrees);
    EXPECT_EQ(0u, calls().planeResourceQueries);
}

TEST_F(RendererInitialization, InvalidCurrentModeStillFindsCrtcIndex)
{
    config().validMode = false;
    ASSERT_TRUE(RendererInitContext(false));
    EXPECT_EQ(RD_CRTC_ID, RendererInitGetState().crtcId);
    EXPECT_EQ(0, RendererInitGetState().width);
    EXPECT_EQ(2u, calls().crtcQueries);
    EXPECT_EQ(1u, calls().planeResourceQueries);
}

TEST_F(RendererInitialization, MissingCrtcPreventsPlaneEnumeration)
{
    config().failCrtc = true;
    ASSERT_TRUE(RendererInitContext(false));
    EXPECT_EQ(0u, RendererInitGetState().crtcId);
    EXPECT_EQ(0, RendererInitGetState().width);
    EXPECT_EQ(2u, calls().crtcQueries);
    EXPECT_EQ(0u, calls().planeResourceQueries);
}

TEST_F(RendererInitialization, AtomicCapabilityAndPropertiesUseExistingDrmMocks)
{
    config().atomicResult = 0;
    ASSERT_TRUE(RendererInitContext(false));
    const auto s = RendererInitGetState();
    EXPECT_TRUE(s.atomic && s.connectorProps && s.crtcProps);
    ASSERT_EQ(2u, calls().capCount);
    EXPECT_EQ(static_cast<uint64_t>(DRM_CLIENT_CAP_UNIVERSAL_PLANES), calls().caps[0]);
    EXPECT_EQ(static_cast<uint64_t>(DRM_CLIENT_CAP_ATOMIC), calls().caps[1]);
    EXPECT_EQ(1u, calls().capValues[0]); EXPECT_EQ(1u, calls().capValues[1]);
    EXPECT_EQ(before.objectQueries + 2, RendererDRMCalls()->objectQueries);
    EXPECT_EQ(before.propertyQueries + 5, RendererDRMCalls()->propertyQueries);
    RendererInitDestroyContext();
    EXPECT_EQ(before.objectFrees + 2, RendererDRMCalls()->objectFrees);
    EXPECT_EQ(before.propertyFrees + 5, RendererDRMCalls()->propertyFrees);
}

TEST_F(RendererInitialization, UniversalCapabilityFailureDoesNotPreventAtomicProbe)
{
    config().universalResult = -1; config().atomicResult = 0;
    ASSERT_TRUE(RendererInitContext(false));
    EXPECT_TRUE(RendererInitGetState().atomic);
    EXPECT_EQ(2u, calls().capCount);
}

TEST_F(RendererInitialization, SharedMocksResetAcrossInitDrmAndInitLifetimes)
{
    config().atomicResult = 0;
    ASSERT_TRUE(RendererInitContext(false));
    EXPECT_GT(calls().encoderQueries, 0u);
    EXPECT_GT(RendererDRMCalls()->propertyQueries, 0u);
    RendererInitEnd(); active = false;

    // Poison shared faults AFTER teardown: the next fixture must reset them,
    // rather than depend on the preceding fixture's cleanup conventions.
    RendererDRMFailures()->failObject = true;
    RendererDRMFailures()->failPropertyId = 1;
    RendererDRMFailures()->failLock = true;
    {
        struct DrmCleanup { ~DrmCleanup() { RendererDRMDestroy(); } } cleanup;
        ASSERT_TRUE(RendererDRMCreate(true, true));
        EXPECT_EQ(0u, calls().encoderQueries);
        EXPECT_EQ(0u, calls().capCount);
        EXPECT_EQ(0u, RendererDRMCalls()->propertyQueries);
        for (int i = 0; i < 4; ++i)
            ASSERT_TRUE(RendererDRMAcquire(static_cast<RendererDRMObject>(i)));
        RendererDRMSwap(false);
        EXPECT_GT(RendererDRMCalls()->addCount, 0u);
        EXPECT_EQ(1u, RendererDRMCalls()->outstandingBO);
    } // Mixed graphics/video ownership must be reclaimed exactly once.
    EXPECT_EQ(0, RendererDRMCalls()->liveProperties);
    EXPECT_EQ(0u, RendererDRMCalls()->outstandingBO);
    EXPECT_EQ(2u, RendererDRMCalls()->planeFrees);
    EXPECT_GT(RendererDRMCalls()->rmCount, 0u);

    RendererDRMFailures()->failObject = true;
    RendererDRMFailures()->failPropertyId = 101;
    RendererDRMFailures()->commitResult = -1;
    RendererInitReset(); active = true;
    before = *RendererDRMCalls();
    EXPECT_EQ(0u, before.propertyQueries);
    EXPECT_EQ(0u, before.addCount);
    EXPECT_EQ(0u, before.rmCount);
    EXPECT_EQ(0u, before.ioctlCount);
    EXPECT_EQ(0u, before.adds[0].object);
    EXPECT_EQ(0u, before.removedFB[0]);
    EXPECT_FALSE(RendererDRMFailures()->failObject);
    EXPECT_EQ(0u, RendererDRMFailures()->failPropertyId);
    EXPECT_EQ(0, RendererDRMFailures()->commitResult);
    config().atomicResult = 0;
    ASSERT_TRUE(RendererInitContext(false));
    EXPECT_TRUE(RendererInitGetState().atomic);
    EXPECT_EQ(5u, RendererDRMCalls()->propertyQueries);
}

class RendererInitPropertyFailure : public RendererInitialization,
    public ::testing::WithParamInterface<unsigned> {};
TEST_P(RendererInitPropertyFailure, DropsAtomicAndReleasesPartialPropertyState)
{
    config().atomicResult = 0;
    RendererDRMFailures()->failPropertyId = GetParam();
    ASSERT_TRUE(RendererInitContext(false));
    auto s = RendererInitGetState();
    EXPECT_FALSE(s.atomic || s.connectorProps || s.crtcProps);
    EXPECT_EQ(before.liveObjects, RendererDRMCalls()->liveObjects);
    EXPECT_EQ(before.liveProperties, RendererDRMCalls()->liveProperties);
    EXPECT_GT(RendererDRMCalls()->propertyQueries, before.propertyQueries);
}
INSTANTIATE_TEST_SUITE_P(ConnectorAndCrtc, RendererInitPropertyFailure,
    ::testing::Values(1u, 2u, 101u, 103u));

TEST_F(RendererInitialization, PlaneResourceFailureDisablesPlanesWithoutFailingContext)
{
    config().failPlaneResources = true;
    ASSERT_TRUE(RendererInitContext(false));
    EXPECT_FALSE(RendererInitGetState().planes);
    EXPECT_EQ(0u, RendererInitGetState().planeCount);
    EXPECT_EQ(1u, calls().planeResourceQueries);
    EXPECT_EQ(0u, calls().planeResourceFrees);
    EXPECT_EQ(0u, calls().planeQueries);
}

TEST_F(RendererInitialization, MissingPlanesAreSkippedAndEnumerationStorageFreed)
{
    config().planeCount = 2; config().failPlane = true;
    ASSERT_TRUE(RendererInitContext(false));
    EXPECT_EQ(2u, calls().planeQueries);
    EXPECT_EQ(1u, calls().planeResourceFrees);
    EXPECT_EQ(0u, RendererInitGetState().planeCount);
    EXPECT_FALSE(RendererInitGetState().planes);
    EXPECT_EQ(before.planeFrees, RendererDRMCalls()->planeFrees);
}

TEST_F(RendererInitialization, PlanePropertyQueryFailureFreesHeapPlaneAndFormats)
{
    config().planeCount = 1;
    RendererDRMFailures()->failObject = true;
    ASSERT_TRUE(RendererInitContext(false));
    EXPECT_EQ(1u, calls().planeQueries);
    EXPECT_EQ(before.objectQueries + 1, RendererDRMCalls()->objectQueries);
    EXPECT_EQ(before.planeFrees + 1, RendererDRMCalls()->planeFrees);
    EXPECT_EQ(0u, RendererInitGetState().planeCount);
}

TEST_F(RendererInitialization, MasterSurvivesDisplayServiceFailureWithoutBinding)
{
    config().masterResult = 0;
    // XDG_RUNTIME_DIR is absent. No lockfile, socket or worker can be created.
    ASSERT_TRUE(RendererInitContext(false));
    EXPECT_TRUE(RendererInitGetState().master);
    EXPECT_EQ(0u, calls().lockOpens);
    EXPECT_EQ(0u, calls().sockets);
    EXPECT_EQ(1u, calls().reclaimedServices);
    EXPECT_EQ(1u, calls().offloadThreads);
    EXPECT_EQ(1u, calls().refreshThreads);
}

TEST_F(RendererInitialization, UpdateNullAndResourceLessContextsDoNotQueryConnectors)
{
    RendererInitUpdateNull();
    RendererInitPrepareUpdate(false, false, false, false);
    RendererInitUpdate();
    EXPECT_EQ(0u, calls().connectorQueries);
    EXPECT_EQ(0u, calls().encoderQueries);
    EXPECT_EQ(640, RendererInitGetState().width);
}

TEST_F(RendererInitialization, UpdateSelectsWindowModeAndAcquiresConnectorProperties)
{
    RendererInitPrepareUpdate(true, true, true, false);
    RendererInitUpdate();
    auto s = RendererInitGetState();
    EXPECT_EQ(RD_CONNECTOR_ID, s.connectorId);
    EXPECT_EQ(21u, s.encoderId);
    EXPECT_TRUE(s.atomic && s.connectorProps);
    EXPECT_EQ(1280, s.width); EXPECT_EQ(720, s.height);
    EXPECT_EQ(60, s.rate);
    EXPECT_FALSE(s.modeSet || s.modePending);
    EXPECT_EQ(before.propertyQueries + 2, RendererDRMCalls()->propertyQueries);
}

TEST_F(RendererInitialization, UpdatePreservesAlreadySetMode)
{
    RendererInitPrepareUpdate(true, true, false, true);
    RendererInitUpdate();
    EXPECT_EQ(640, RendererInitGetState().width);
    EXPECT_EQ(480, RendererInitGetState().height);
    EXPECT_TRUE(RendererInitGetState().modeSet);
    EXPECT_EQ(1u, calls().encoderQueries);
}

TEST_F(RendererInitialization, UpdateWithoutWindowLeavesCurrentSizeUntouched)
{
    RendererInitPrepareUpdate(true, false, false, false);
    RendererInitUpdate();
    EXPECT_EQ(640, RendererInitGetState().width);
    EXPECT_EQ(RD_CONNECTOR_ID, RendererInitGetState().connectorId);
    EXPECT_EQ(before.objectQueries, RendererDRMCalls()->objectQueries);
}

TEST_F(RendererInitialization, UpdateSkipsRejectedConnectorsAndRetargetsEncoder)
{
    config().connectorCount = 3;
    config().connectors[0] = {10, 99, true, true, 2};
    config().connectors[1] = {11, 99, false, false, 2};
    config().connectors[2] = {12, 99, false, true, 2};
    config().encoders[0].possibleCrtcs = 2;
    config().crtcCount = 2; config().crtcs[1] = 88;
    RendererInitPrepareUpdate(true, false, false, false);
    RendererInitUpdate();
    EXPECT_EQ(12u, RendererInitGetState().connectorId);
    EXPECT_EQ(88u, RendererInitGetState().encoderCrtcId);
    EXPECT_EQ(3u, calls().connectorQueries);
    EXPECT_EQ(1u, calls().connectorFrees);
    EXPECT_EQ(2u, calls().encoderQueries);
}

TEST_F(RendererInitialization, UpdateWithoutUsableConnectorDoesNotQueryEncoders)
{
    config().connectors[0].modes = 0;
    RendererInitPrepareUpdate(true, true, false, false);
    RendererInitUpdate();
    EXPECT_EQ(0u, RendererInitGetState().connectorId);
    EXPECT_EQ(1u, calls().connectorFrees);
    EXPECT_EQ(0u, calls().encoderQueries);
    EXPECT_EQ(640, RendererInitGetState().width);
}

TEST_F(RendererInitialization, UpdateSkipsMissingAndRejectsIncompatibleEncoders)
{
    config().encoderCount = 2;
    config().encoders[0].missing = true;
    config().encoders[1] = {22, RD_CRTC_ID, 0, false};
    RendererInitPrepareUpdate(true, false, false, false);
    RendererInitUpdate();
    EXPECT_EQ(0u, RendererInitGetState().encoderId);
    EXPECT_EQ(2u, calls().encoderQueries);
    EXPECT_EQ(before.encoderFrees + 1, RendererDRMCalls()->encoderFrees);
}

enum class ServiceFault { MissingDirectory, LongPath, LockOpen, Flock, Socket, Bind, Listen };
class RendererServiceFailure : public RendererInitialization,
    public ::testing::WithParamInterface<ServiceFault> {};
TEST_P(RendererServiceFailure, LeavesOutputUnpublishedAndStopsAtFailingOperation)
{
    const auto fault = GetParam();
    runtimeDirectory();
    switch (fault)
    {
        case ServiceFault::MissingDirectory: ASSERT_EQ(0, unsetenv("XDG_RUNTIME_DIR")); break;
        case ServiceFault::LongPath: ASSERT_EQ(0, setenv("XDG_RUNTIME_DIR", std::string(160, 'x').c_str(), 1)); break;
        case ServiceFault::LockOpen: config().lockOpenResult = -1; break;
        case ServiceFault::Flock: config().flockResult = -1; break;
        case ServiceFault::Socket: config().socketResult = -1; break;
        case ServiceFault::Bind: config().bindResult = -1; break;
        case ServiceFault::Listen: config().listenResult = -1; break;
    }
    auto result = RendererInitService(0);
    EXPECT_FALSE(result.result || result.published || result.threadStarted);
    EXPECT_TRUE(result.pathsCleared);
    EXPECT_EQ(1u, calls().reclaimedServices); // Explicit fixture reclamation, not production cleanup.
    const bool opened = fault >= ServiceFault::Flock;
    const bool socket = fault >= ServiceFault::Bind;
    EXPECT_EQ(opened ? RI_LOCK_FD : -1, result.lockFd);
    EXPECT_EQ(socket ? RI_SOCKET_FD : -1, result.socketFd);
    EXPECT_EQ(fault >= ServiceFault::LockOpen ? 1u : 0u, calls().lockOpens);
    EXPECT_EQ(opened ? 1u : 0u, calls().flocks);
    EXPECT_EQ(fault >= ServiceFault::Socket ? 1u : 0u, calls().sockets);
    EXPECT_EQ(socket ? 1u : 0u, calls().binds);
    EXPECT_EQ(fault == ServiceFault::Listen ? 1u : 0u, calls().listens);
    EXPECT_EQ((opened ? 1u : 0u) + (socket ? 1u : 0u), RendererCalls()->closeCalls);
    EXPECT_EQ(socket ? 1u : 0u, calls().shutdowns);
    EXPECT_EQ(0u, calls().threads);
}
INSTANTIATE_TEST_SUITE_P(EveryServiceBoundary, RendererServiceFailure, ::testing::Values(
    ServiceFault::MissingDirectory, ServiceFault::LongPath, ServiceFault::LockOpen,
    ServiceFault::Flock, ServiceFault::Socket, ServiceFault::Bind, ServiceFault::Listen));

TEST_F(RendererInitialization, ServiceSuccessBuildsAddressAndClosesMockDescriptors)
{
    runtimeDirectory();
    const auto result = RendererInitService(0);
    EXPECT_TRUE(result.result && result.published);
    EXPECT_FALSE(result.pathsCleared || result.threadStarted);
    EXPECT_EQ(RI_LOCK_FD, result.lockFd); EXPECT_EQ(RI_SOCKET_FD, result.socketFd);
    EXPECT_STREQ("/l1-not-a-real-directory/fixture.lock", calls().lockPath);
    EXPECT_STREQ("/l1-not-a-real-directory/fixture", calls().boundPath);
    EXPECT_EQ(PF_LOCAL, calls().socketDomain);
    EXPECT_EQ(SOCK_STREAM | SOCK_CLOEXEC, calls().socketType);
    EXPECT_EQ(0, calls().socketProtocol); EXPECT_EQ(1, calls().backlog);
    EXPECT_EQ(2u, RendererCalls()->closeCalls);
    EXPECT_EQ(RI_SOCKET_FD, calls().shutdownFd); EXPECT_EQ(SHUT_RDWR, calls().shutdownHow);
    ASSERT_EQ(3u, calls().unlinks);
    EXPECT_STREQ(calls().boundPath, calls().unlinked[0]);
    EXPECT_STREQ(calls().boundPath, calls().unlinked[1]);
    EXPECT_STREQ(calls().lockPath, calls().unlinked[2]);
    EXPECT_EQ(0u, calls().reclaimedServices);
}

TEST_F(RendererInitialization, ServiceRetainsResourcesUntilFinalReference)
{
    runtimeDirectory();
    EXPECT_TRUE(RendererInitServiceRefcount());
    EXPECT_EQ(1u, calls().shutdowns);
    EXPECT_EQ(2u, RendererCalls()->closeCalls);
    EXPECT_EQ(3u, calls().unlinks);
}

class RendererServiceKind : public RendererInitialization,
    public ::testing::WithParamInterface<int> {};
TEST_P(RendererServiceKind, ThreadCreationFailureKeepsPublishedServiceForTeardown)
{
    runtimeDirectory();
    auto result = RendererInitService(GetParam());
    EXPECT_FALSE(result.result || result.threadStarted);
    EXPECT_TRUE(result.published);
    EXPECT_FALSE(result.pathsCleared);
    EXPECT_EQ(1u, calls().serviceThreads);
    EXPECT_EQ(1u, calls().threads);
    EXPECT_EQ(0u, calls().reclaimedServices);
    EXPECT_EQ(2u, RendererCalls()->closeCalls);
    EXPECT_EQ(1u, calls().shutdowns);
}
TEST_P(RendererServiceKind, ServiceFailureNeverAttemptsThreadCreation)
{
    auto result = RendererInitService(GetParam());
    EXPECT_FALSE(result.result || result.published);
    EXPECT_TRUE(result.pathsCleared);
    EXPECT_EQ(0u, calls().threads);
    EXPECT_EQ(0u, calls().sockets);
    EXPECT_EQ(1u, calls().reclaimedServices);
}
INSTANTIATE_TEST_SUITE_P(DisplayAndVideo, RendererServiceKind, ::testing::Values(1, 2));

class RendererConnectionKind : public RendererInitialization,
    public ::testing::WithParamInterface<bool> {};
TEST_P(RendererConnectionKind, FailedThreadReturnsNullWithoutTakingDescriptorOwnership)
{
    EXPECT_TRUE(RendererInitConnectionFailure(GetParam()));
    EXPECT_EQ(1u, calls().attrInits); EXPECT_EQ(1u, calls().attrDetaches);
    EXPECT_EQ(1u, calls().connectionThreads);
    EXPECT_EQ(0u, calls().shutdowns); EXPECT_EQ(0u, RendererCalls()->closeCalls);
}
TEST_P(RendererConnectionKind, AttributeErrorsStillReachSafeThreadFailure)
{
    config().attrInitResult = EINVAL; config().attrDetachResult = EINVAL;
    EXPECT_TRUE(RendererInitConnectionFailure(GetParam()));
    EXPECT_EQ(1u, calls().attrInits); EXPECT_EQ(1u, calls().attrDetaches);
    EXPECT_EQ(1u, calls().connectionThreads);
    EXPECT_EQ(0u, RendererCalls()->closeCalls);
}
TEST_P(RendererConnectionKind, DestroyIdleConnectionClosesSyntheticDescriptor)
{
    RendererInitDestroyConnection(GetParam(), true);
    EXPECT_EQ(1u, calls().shutdowns);
    EXPECT_EQ(RI_SOCKET_FD, calls().shutdownFd); EXPECT_EQ(SHUT_RDWR, calls().shutdownHow);
    EXPECT_EQ(1u, RendererCalls()->closeCalls);
    EXPECT_EQ(0u, calls().threads);
}
TEST_P(RendererConnectionKind, DestroyConnectionWithoutDescriptorSkipsSocketOperations)
{
    RendererInitDestroyConnection(GetParam(), false);
    EXPECT_EQ(0u, calls().shutdowns); EXPECT_EQ(0u, RendererCalls()->closeCalls);
    EXPECT_EQ(0u, calls().threads);
}
INSTANTIATE_TEST_SUITE_P(DisplayAndVideo, RendererConnectionKind, ::testing::Bool());