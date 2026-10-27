#include <gtest/gtest.h>
#include "renderer_test_support.h"
#include <gbm.h>
#include <array>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
struct SavedEnvironment {
    std::string name, value;
    bool present;
};

class WesterosRenderer : public ::testing::Test {
protected:
    WstGLCtx *ctx = nullptr;
    std::vector<SavedEnvironment> environment;

    void SetUp() override
    {
        // Prevent developer settings from influencing the deterministic fixture.
        for (const char *name : {"WESTEROS_GL_DEBUG", "WESTEROS_GL_DEBUG_FRAME",
             "WESTEROS_GL_GRAPHICS_MAX_SIZE", "WESTEROS_GL_FPS",
             "WESTEROS_DRM_CARD", "WESTEROS_GL_MODE"})
        {
            const char *value = getenv(name);
            environment.push_back({name, value ? value : "", value != nullptr});
            ASSERT_EQ(0, unsetenv(name));
        }
        RendererReset();
        ctx = RendererCreateContext(1920, 1080);
        ASSERT_NE(nullptr, ctx);
    }

    void TearDown() override
    {
        RendererCleanup();
        for (const auto &item : environment)
        {
            if (item.present) setenv(item.name.c_str(), item.value.c_str(), 1);
            else unsetenv(item.name.c_str());
        }
    }
};

struct SizeEvents {
    unsigned count = 0;
    int width = 0, height = 0;
};
void OnSize(void *data, int width, int height)
{
    auto *events = static_cast<SizeEvents *>(data);
    ++events->count;
    events->width = width;
    events->height = height;
}
void OnOtherSize(void *data, int width, int height) { OnSize(data, width, height); }
} // namespace

TEST_F(WesterosRenderer, ReportsModesetCapability)
{
    unsigned caps = 0;
    ASSERT_TRUE(WstGLGetDisplayCaps(ctx, &caps));
    EXPECT_EQ(static_cast<unsigned>(WstGLDisplayCap_modeset), caps);
    EXPECT_FALSE(WstGLGetDisplayCaps(nullptr, &caps));
    EXPECT_FALSE(WstGLGetDisplayCaps(ctx, nullptr));
}

TEST_F(WesterosRenderer, ReportsDisplayAndFivePercentSafeArea)
{
    WstGLDisplayInfo info{};
    ASSERT_TRUE(WstGLGetDisplayInfo(ctx, &info));
    EXPECT_EQ(1920, info.width);
    EXPECT_EQ(1080, info.height);
    EXPECT_EQ(96, info.safeArea.x);
    EXPECT_EQ(54, info.safeArea.y);
    EXPECT_EQ(1728, info.safeArea.w);
    EXPECT_EQ(972, info.safeArea.h);
    int x = 0, y = 0, width = 0, height = 0;
    ASSERT_TRUE(WstGLGetDisplaySafeArea(ctx, &x, &y, &width, &height));
    EXPECT_EQ(info.safeArea.x, x);
    EXPECT_EQ(info.safeArea.y, y);
    EXPECT_EQ(info.safeArea.w, width);
    EXPECT_EQ(info.safeArea.h, height);
}

TEST_F(WesterosRenderer, RejectsInvalidDisplayOutputPointers)
{
    WstGLDisplayInfo info{};
    int value = -1;
    EXPECT_FALSE(WstGLGetDisplayInfo(nullptr, &info));
    EXPECT_FALSE(WstGLGetDisplayInfo(ctx, nullptr));
    EXPECT_FALSE(WstGLGetDisplaySafeArea(nullptr, &value, &value, &value, &value));
    EXPECT_FALSE(WstGLGetDisplaySafeArea(ctx, nullptr, &value, &value, &value));
    EXPECT_FALSE(WstGLGetDisplaySafeArea(ctx, &value, nullptr, &value, &value));
    EXPECT_FALSE(WstGLGetDisplaySafeArea(ctx, &value, &value, nullptr, &value));
    EXPECT_FALSE(WstGLGetDisplaySafeArea(ctx, &value, &value, &value, nullptr));
    EXPECT_EQ(-1, value);
}

TEST_F(WesterosRenderer, ClampsReportedSizeBeforeCalculatingSafeArea)
{
    RendererSetGraphicsLimit(1280, 720);
    WstGLDisplayInfo info{};
    ASSERT_TRUE(WstGLGetDisplayInfo(ctx, &info));
    EXPECT_EQ(1280, info.width);
    EXPECT_EQ(720, info.height);
    EXPECT_EQ(64, info.safeArea.x);
    EXPECT_EQ(36, info.safeArea.y);
    EXPECT_EQ(1152, info.safeArea.w);
    EXPECT_EQ(648, info.safeArea.h);
}

struct ModeCase {
    const char *text;
    int width, height, rate;
    bool interlaced;
};
class RendererModeSyntax : public WesterosRenderer,
    public ::testing::WithParamInterface<ModeCase> {};

TEST_P(RendererModeSyntax, SelectsMatchingMode)
{
    const auto &mode = GetParam();
    RendererAddMode(mode.width, mode.height, mode.rate, mode.interlaced);
    ASSERT_TRUE(WstGLSetDisplayMode(ctx, mode.text));
    const auto state = RendererGetModeState();
    EXPECT_EQ(mode.width, state.width);
    EXPECT_EQ(mode.height, state.height);
    EXPECT_EQ(mode.rate, state.rate);
    EXPECT_EQ(mode.interlaced, state.interlaced);
    EXPECT_TRUE(state.pending);
    EXPECT_TRUE(state.explicitlySelected);
    EXPECT_EQ(mode.width != 1920 || mode.height != 1080, state.sizeChanged);
    EXPECT_EQ(1u, RendererCalls()->connectorFrees);
}

INSTANTIATE_TEST_SUITE_P(SupportedFormats, RendererModeSyntax, ::testing::Values(
    ModeCase{"1920x1080p60", 1920, 1080, 60, false},
    ModeCase{"1920x1080i50", 1920, 1080, 50, true},
    ModeCase{"1280x720x60", 1280, 720, 60, false},
    ModeCase{"1920x1080", 1920, 1080, 60, false},
    ModeCase{"1920x1080i", 1920, 1080, 50, true},
    ModeCase{"1080p60hz", 1920, 1080, 60, false},
    ModeCase{"1080i50hz", 1920, 1080, 50, true},
    ModeCase{"480p", 720, 480, 60, false},
    ModeCase{"576i", 720, 576, 50, true},
    ModeCase{"720p", 1280, 720, 60, false},
    ModeCase{"1440p", 2560, 1440, 60, false},
    ModeCase{"2160p", 3840, 2160, 60, false},
    ModeCase{"2880p", 5120, 2880, 60, false},
    ModeCase{"4320p", 7680, 4320, 60, false},
    ModeCase{"smpte24hz", 4096, 2160, 24, false}));

TEST_F(WesterosRenderer, SelectsHighestProgressiveRateWhenUnspecified)
{
    RendererAddMode(1920, 1080, 24, false);
    RendererAddMode(1920, 1080, 120, true);
    RendererAddMode(1920, 1080, 60, false);
    RendererAddMode(1280, 720, 120, false);
    ASSERT_TRUE(WstGLSetDisplayMode(ctx, "1920x1080"));
    EXPECT_EQ(60, RendererGetModeState().rate);
    EXPECT_FALSE(RendererGetModeState().interlaced);
}

TEST_F(WesterosRenderer, ExplicitRateDoesNotSelectFasterMode)
{
    RendererAddMode(1920, 1080, 60, false);
    RendererAddMode(1920, 1080, 24, false);
    ASSERT_TRUE(WstGLSetDisplayMode(ctx, "1080p24hz"));
    EXPECT_EQ(24, RendererGetModeState().rate);
}

TEST_F(WesterosRenderer, RejectsInvalidAndUnavailableModesWithoutChangingState)
{
    RendererAddMode(1920, 1080, 60, false);
    EXPECT_FALSE(WstGLSetDisplayMode(nullptr, "1080p"));
    EXPECT_FALSE(WstGLSetDisplayMode(ctx, nullptr));
    for (const char *mode : {"", "invalid", "0x0", "999p", "720p", "1080i60hz", "1080p25hz"})
    {
        SCOPED_TRACE(mode);
        EXPECT_FALSE(WstGLSetDisplayMode(ctx, mode));
        EXPECT_FALSE(RendererGetModeState().pending);
    }
}

TEST_F(WesterosRenderer, HandlesMissingDeviceAndResourceQueryFailure)
{
    RendererSetDeviceAvailable(false);
    EXPECT_FALSE(WstGLSetDisplayMode(ctx, "1080p"));
    EXPECT_EQ(0u, RendererCalls()->resourceQueries);
    RendererSetDeviceAvailable(true);
    RendererSetResourceFailure(true);
    EXPECT_FALSE(WstGLSetDisplayMode(ctx, "1080p"));
    EXPECT_EQ(1u, RendererCalls()->resourceQueries);
    EXPECT_EQ(0u, RendererCalls()->connectorQueries);
}

TEST_F(WesterosRenderer, HandlesMissingDisconnectedAndModelessConnectors)
{
    RendererSetConnectorFailure(true);
    EXPECT_FALSE(WstGLSetDisplayMode(ctx, "1080p"));
    RendererSetConnectorFailure(false);
    EXPECT_FALSE(WstGLSetDisplayMode(ctx, "1080p"));
    RendererAddMode(1920, 1080, 60, false);
    RendererSetConnectorConnected(false);
    EXPECT_FALSE(WstGLSetDisplayMode(ctx, "1080p"));
    EXPECT_FALSE(RendererGetModeState().pending);
}

TEST_F(WesterosRenderer, NotifiesListenerInitiallyAndOnlyOnSizeChange)
{
    SizeEvents events;
    ASSERT_TRUE(WstGLAddDisplaySizeListener(ctx, &events, OnSize));
    EXPECT_EQ(1u, events.count);
    EXPECT_EQ(1920, events.width);
    EXPECT_EQ(1080, events.height);
    RendererNotifySize();
    EXPECT_EQ(1u, events.count);
    RendererSetCurrentSize(1280, 720);
    RendererNotifySize();
    EXPECT_EQ(2u, events.count);
    EXPECT_EQ(1280, events.width);
    EXPECT_EQ(720, events.height);
    ASSERT_TRUE(WstGLRemoveDisplaySizeListener(ctx, OnSize));
    RendererSetCurrentSize(640, 480);
    RendererNotifySize();
    EXPECT_EQ(2u, events.count);
}

TEST_F(WesterosRenderer, RejectsDuplicateListenerAndRemovesHeadAndTail)
{
    SizeEvents first, second;
    EXPECT_FALSE(WstGLAddDisplaySizeListener(nullptr, &first, OnSize));
    ASSERT_TRUE(WstGLAddDisplaySizeListener(ctx, &first, OnSize));
    EXPECT_FALSE(WstGLAddDisplaySizeListener(ctx, &second, OnSize));
    ASSERT_TRUE(WstGLAddDisplaySizeListener(ctx, &second, OnOtherSize));
    EXPECT_FALSE(WstGLRemoveDisplaySizeListener(nullptr, OnSize));
    EXPECT_TRUE(WstGLRemoveDisplaySizeListener(ctx, OnOtherSize));
    EXPECT_TRUE(WstGLRemoveDisplaySizeListener(ctx, OnSize));
    EXPECT_FALSE(WstGLRemoveDisplaySizeListener(ctx, OnSize));
}

TEST_F(WesterosRenderer, ListenerReceivesClampedGraphicsDimensions)
{
    RendererSetGraphicsLimit(640, 480);
    SizeEvents events;
    ASSERT_TRUE(WstGLAddDisplaySizeListener(ctx, &events, OnSize));
    EXPECT_EQ(640, events.width);
    EXPECT_EQ(480, events.height);
    EXPECT_TRUE(WstGLRemoveDisplaySizeListener(ctx, OnSize));
}

TEST_F(WesterosRenderer, CreatesAndDestroysWindowThroughMockedGbm)
{
    void *window = WstGLCreateNativeWindow(ctx, 0, 0, 800, 600);
    ASSERT_NE(nullptr, window);
    EXPECT_EQ(1u, RendererWindowCount());
    EXPECT_EQ(800u, RendererCalls()->surfaceWidth);
    EXPECT_EQ(600u, RendererCalls()->surfaceHeight);
    EXPECT_EQ(static_cast<uint32_t>(GBM_FORMAT_ARGB8888), RendererCalls()->surfaceFormat);
    EXPECT_EQ(static_cast<uint32_t>(GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING), RendererCalls()->surfaceFlags);
    WstGLDestroyNativeWindow(ctx, window);
    EXPECT_EQ(0u, RendererWindowCount());
    EXPECT_EQ(1u, RendererCalls()->surfaceDestroys);
    EXPECT_FALSE(RendererModeIsSet());
}

TEST_F(WesterosRenderer, ClampsWindowSizeAndMaintainsMultipleWindows)
{
    RendererSetGraphicsLimit(1280, 720);
    void *first = WstGLCreateNativeWindow(ctx, 0, 0, 1920, 1080);
    ASSERT_NE(nullptr, first);
    EXPECT_EQ(1280u, RendererCalls()->surfaceWidth);
    EXPECT_EQ(720u, RendererCalls()->surfaceHeight);
    void *second = WstGLCreateNativeWindow(ctx, 0, 0, 640, 480);
    ASSERT_NE(nullptr, second);
    EXPECT_EQ(2u, RendererWindowCount());
    WstGLDestroyNativeWindow(ctx, first); // remove the tail, then the head
    EXPECT_EQ(1u, RendererWindowCount());
    EXPECT_TRUE(RendererModeIsSet());
    WstGLDestroyNativeWindow(ctx, second);
    EXPECT_EQ(0u, RendererWindowCount());
    EXPECT_EQ(2u, RendererCalls()->surfaceDestroys);
}

TEST_F(WesterosRenderer, PreservesExplicitModeWhenLastWindowIsDestroyed)
{
    RendererSetExplicitMode(true);
    void *window = WstGLCreateNativeWindow(ctx, 0, 0, 640, 480);
    ASSERT_NE(nullptr, window);
    WstGLDestroyNativeWindow(ctx, nullptr);
    EXPECT_EQ(0u, RendererCalls()->surfaceDestroys);
    WstGLDestroyNativeWindow(ctx, window);
    EXPECT_TRUE(RendererModeIsSet());
}

class RendererMissingEgl : public WesterosRenderer,
    public ::testing::WithParamInterface<const char *> {};
TEST_P(RendererMissingEgl, FailsInitializationWithoutOpeningDevice)
{
    RendererSetMissingSymbol(GetParam());
    EXPECT_EQ(nullptr, WstGLInit());
    EXPECT_GT(RendererCalls()->symbolQueries, 0u);
    EXPECT_EQ(0u, RendererCalls()->openCalls);
    EXPECT_EQ(1, RendererReferenceCount());
}
INSTANTIATE_TEST_SUITE_P(RequiredSymbols, RendererMissingEgl, ::testing::Values(
    "eglGetDisplay", "eglCreateContext", "eglCreateWindowSurface", "eglSwapBuffers"));

TEST_F(WesterosRenderer, ReusesContextAndReleasesOnlyAfterFinalReference)
{
    EXPECT_EQ(ctx, WstGLInit());
    EXPECT_EQ(2, RendererReferenceCount());
    EXPECT_EQ(0u, RendererCalls()->openCalls);
    WstGLTerm(ctx);
    EXPECT_EQ(1, RendererReferenceCount());
    EXPECT_TRUE(RendererHasContext());
    WstGLTerm(ctx);
    EXPECT_FALSE(RendererHasContext());
    EXPECT_EQ(1u, RendererCalls()->closeCalls);
    WstGLTerm(nullptr);
}

TEST_F(WesterosRenderer, DeviceInitializerHandlesOpenFailureWithoutHardware)
{
    RendererCleanup();
    ctx = nullptr;
    EXPECT_EQ(nullptr, RendererInitDevice());
    EXPECT_EQ(1u, RendererCalls()->openCalls);
    EXPECT_STREQ("/dev/dri/card0", RendererCalls()->cardPath);
    ASSERT_EQ(0, setenv("WESTEROS_DRM_CARD", "/test/card", 1));
    EXPECT_EQ(nullptr, RendererInitDevice());
    EXPECT_EQ(2u, RendererCalls()->openCalls);
    EXPECT_STREQ("/test/card", RendererCalls()->cardPath);
    EXPECT_FALSE(RendererHasContext());
}

TEST_F(WesterosRenderer, EncodesProtocolIntegersInNetworkByteOrder)
{
    std::array<unsigned char, 4> u32{};
    EXPECT_EQ(4, RendererPutU32(u32.data(), 0x12345678u));
    EXPECT_EQ((std::array<unsigned char, 4>{{0x12, 0x34, 0x56, 0x78}}), u32);
    EXPECT_EQ(0x12345678u, RendererGetU32(u32.data()));
    std::array<unsigned char, 8> s64{};
    EXPECT_EQ(8, RendererPutS64(s64.data(), 0x0102030405060708LL));
    EXPECT_EQ((std::array<unsigned char, 8>{{1, 2, 3, 4, 5, 6, 7, 8}}), s64);
    EXPECT_EQ(0x0102030405060708LL, RendererGetS64(s64.data()));
}

TEST_F(WesterosRenderer, NormalizesNegativeVideoRectangleDimensions)
{
    const auto rect = RendererSetVideoRect(0, 0, -1, -1);
    EXPECT_EQ(4, rect.width);
    EXPECT_EQ(4, rect.height);
    EXPECT_EQ(1920u, rect.visibleWidth);
    EXPECT_EQ(1080u, rect.visibleHeight);
    EXPECT_EQ(0u, rect.skipX);
    EXPECT_EQ(0u, rect.skipY);
}

TEST_F(WesterosRenderer, KeepsVideoRectanglePartiallyVisibleAtScreenEdges)
{
    auto rect = RendererSetVideoRect(2000, 1200, 640, 480);
    EXPECT_EQ(1918, rect.x);
    EXPECT_EQ(1078, rect.y);
    rect = RendererSetVideoRect(-800, -600, 640, 480);
    EXPECT_EQ(-638, rect.x);
    EXPECT_EQ(-478, rect.y);
    EXPECT_EQ(640, rect.width);
    EXPECT_EQ(480, rect.height);
}