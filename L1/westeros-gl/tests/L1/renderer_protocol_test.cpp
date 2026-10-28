#include <gtest/gtest.h>
#include "renderer_test_support.h"
#include "renderer_protocol_support.h"
#include "renderer_frame_support.h"
#include <sys/socket.h>
#include <sys/uio.h>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

namespace {
using Bytes = std::vector<unsigned char>;

auto SettingsTuple(const RendererProtocolSettings &s)
{
    return std::make_tuple(s.display, s.graphics, s.video, s.actualSize,
        s.autoFRM, s.globalZoom, s.allow4k, s.dirty, s.forceDirty, s.zoom,
        s.zoomVersion, s.videoDebug, s.logLevel);
}

class RendererProtocol : public ::testing::Test {
protected:
    RendererProtocolFixture *fixture = nullptr;
    void SetUp() override
    {
        RendererReset();
        ASSERT_NE(nullptr, RendererCreateContext(1920, 1080));
        fixture = RendererProtocolCreate();
        ASSERT_NE(nullptr, fixture);
        RendererProtocolMode(1920, 1080, 60, false, true);
    }
    void TearDown() override
    {
        if (fixture) EXPECT_TRUE(RendererProtocolMutexesUnlocked(fixture));
        RendererProtocolDestroy(fixture);
        RendererCleanup();
    }
    void Packet(int index, const Bytes &expected)
    {
        ASSERT_LT(index, RendererProtocolPacketCount());
        const auto packet = RendererProtocolPacketAt(index);
        EXPECT_TRUE(packet.envelopeValid);
        EXPECT_EQ(-1, packet.fd); // No socket is opened by the fixture.
        EXPECT_EQ(MSG_NOSIGNAL, packet.flags);
        ASSERT_EQ(expected.size(), packet.length);
        EXPECT_EQ(expected, Bytes(packet.bytes, packet.bytes + packet.length));
    }
    Bytes DisplayBytes(const std::string &response)
    {
        Bytes expected{'D', 'S', static_cast<unsigned char>(response.size() + 1)};
        expected.insert(expected.end(), response.begin(), response.end());
        expected.push_back(0);
        return expected;
    }
    void Command(const std::string &command, const std::string &expected,
                 bool hideContext = false)
    {
        SCOPED_TRACE(command);
        RendererProtocolCaptureReset();
        EXPECT_STREQ(expected.c_str(), RendererProtocolCommand(fixture,
            command.c_str(), static_cast<int>(command.size()), hideContext));
        EXPECT_EQ(static_cast<int>(expected.size()), RendererProtocolResponseLength(fixture));
        ASSERT_EQ(1, RendererProtocolPacketCount());
        Packet(0, DisplayBytes(expected));
        EXPECT_TRUE(RendererProtocolMutexesUnlocked(fixture));
    }
};

struct BadCommand { const char *input; const char *response; };
class RendererProtocolBadCommand : public RendererProtocol,
    public ::testing::WithParamInterface<BadCommand> {};

TEST_P(RendererProtocolBadCommand, ExactReplyAndNoStateMutation)
{
    const auto before = SettingsTuple(RendererProtocolGetSettings());
    Command(GetParam().input, GetParam().response);
    EXPECT_EQ(before, SettingsTuple(RendererProtocolGetSettings()));
    EXPECT_FALSE(RendererGetModeState().pending);
    EXPECT_EQ(0u, RendererCalls()->resourceQueries);
}

INSTANTIATE_TEST_SUITE_P(ParserBranches, RendererProtocolBadCommand, ::testing::Values(
    BadCommand{"get", "-1: get missing argument(s)"},
    BadCommand{"set", "-1: set missing argument(s)"},
    BadCommand{"GET mode", "-1: unknown cmd"},
    BadCommand{"getter mode", "-1: unknown cmd"},
    BadCommand{"unknown", "-1: unknown cmd"},
    BadCommand{"get modes", "-1: get bad argument(s)"},
    BadCommand{"set unknown 1", "-1: set bad argument(s)"},
    BadCommand{"get display", "-1: get display missing argument(s)"},
    BadCommand{"get graphics", "-1: get graphics missing argument(s)"},
    BadCommand{"get video", "-1: get video missing argument(s)"},
    BadCommand{"get display unknown", ""},
    BadCommand{"get graphics unknown", ""},
    BadCommand{"get video unknown", ""},
    BadCommand{"set mode", "-1: set mode missing argument(s)"},
    BadCommand{"set display", ""},
    BadCommand{"set graphics", ""},
    BadCommand{"set video", ""},
    BadCommand{"set display unknown", "-1: set display missing argument(s)"},
    BadCommand{"set graphics unknown", "-1: set graphics missing argument(s)"},
    BadCommand{"set video unknown", "-1: set video missing argument(s)"},
    BadCommand{"set display enable", "-1: set display enable missing argument(s)"},
    BadCommand{"set graphics enable", "-1: set graphics enable missing argument(s)"},
    BadCommand{"set graphics actual-size", "-1: set graphics actual-size missing argument(s)"},
    BadCommand{"set video enable", "-1: set video enable missing argument(s)"},
    BadCommand{"set graphics enable 2", ""},
    BadCommand{"set graphics actual-size true", ""},
    BadCommand{"set video enable 01", ""},
    BadCommand{"set auto-frm-mode", "-1: set auto-frm-mode missing argument(s)"},
    BadCommand{"set auto-frm-mode 2", "-1: set auto-frm-mode invalid argument(s)"},
    BadCommand{"set zoom-mode", "-1: set zoom-mode missing argument(s)"},
    BadCommand{"set zoom-mode -1", "-1: set zoom-mode invalid argument(s)"},
    BadCommand{"set global-zoom-active", "-1: set global-zoom-active missing argument(s)"},
    BadCommand{"set global-zoom-active yes", "-1: set global-zoom-active invalid argument(s)"},
    BadCommand{"set allow-4k-zoom", "-1: set allow-4k-zoom missing argument(s)"},
    BadCommand{"set allow-4k-zoom 10", "-1: set allow-4k-zoom invalid argument(s)"},
    BadCommand{"set video-debug-level", "-1: set video-debug-level missing argument(s)"},
    // Existing diagnostic typo is characterized, not corrected in production.
    BadCommand{"set video-debug-level -2", "-1: set zoom-mode invalid argument(s)"},
    BadCommand{"set loglevel", ""},
    BadCommand{"   ", ""},
    BadCommand{"get\tmode", "-1: unknown cmd"}));

struct BooleanSetting {
    const char *name;
    const char *setReply;
    bool RendererProtocolSettings::*member;
    bool dirties;
    bool versions;
};
class RendererProtocolBoolean : public RendererProtocol,
    public ::testing::WithParamInterface<BooleanSetting> {};

TEST_P(RendererProtocolBoolean, SetGetBothValuesAndOnlyExpectedStateChanges)
{
    const auto p = GetParam();
    for (int value : {1, 0})
    {
        RendererProtocolClearDirty();
        auto expected = RendererProtocolGetSettings();
        expected.*(p.member) = value != 0;
        expected.dirty = expected.forceDirty = p.dirties;
        if (p.versions) ++expected.zoomVersion;
        const std::string v = std::to_string(value);
        Command(std::string("set ") + p.name + " " + v, std::string("0: ") + p.setReply + " " + v);
        EXPECT_EQ(SettingsTuple(expected), SettingsTuple(RendererProtocolGetSettings()));
        Command(std::string("get ") + p.name, std::string("0: ") + p.name + " " + v);
        EXPECT_EQ(SettingsTuple(expected), SettingsTuple(RendererProtocolGetSettings()));
    }
}

INSTANTIATE_TEST_SUITE_P(AllBooleanSettings, RendererProtocolBoolean, ::testing::Values(
    BooleanSetting{"display enable", "display set enable", &RendererProtocolSettings::display, true, false},
    BooleanSetting{"graphics enable", "graphics set enable", &RendererProtocolSettings::graphics, true, false},
    BooleanSetting{"graphics actual-size", "graphics set actual-size", &RendererProtocolSettings::actualSize, true, false},
    BooleanSetting{"video enable", "video set enable", &RendererProtocolSettings::video, false, false},
    BooleanSetting{"auto-frm-mode", "auto-frm-mode", &RendererProtocolSettings::autoFRM, false, false},
    BooleanSetting{"global-zoom-active", "global-zoom-active", &RendererProtocolSettings::globalZoom, false, true},
    BooleanSetting{"allow-4k-zoom", "allow-4k-zoom", &RendererProtocolSettings::allow4k, false, true}));

TEST_F(RendererProtocol, UnchangedDisplayValueReturnsEmptyWithoutDirtying)
{
    Command("set display enable 0", "");
    EXPECT_FALSE(RendererProtocolGetSettings().dirty);
    Command("set display enable 1", "0: display set enable 1");
    RendererProtocolClearDirty();
    Command("set display enable 1", "");
    EXPECT_TRUE(RendererProtocolGetSettings().display);
    EXPECT_FALSE(RendererProtocolGetSettings().dirty);
    EXPECT_FALSE(RendererProtocolGetSettings().forceDirty);
}

TEST_F(RendererProtocol, InvalidDisplayBooleanCurrentlyCoercesMinusOneToTrue)
{
    // Unlike graphics/video, production compares -1 with a bool instead of
    // validating it. Assert this observable behavior without endorsing it.
    Command("set display enable invalid", "0: display set enable -1");
    const auto s = RendererProtocolGetSettings();
    EXPECT_TRUE(s.display);
    EXPECT_TRUE(s.dirty);
    EXPECT_TRUE(s.forceDirty);
    Command("get display enable", "0: display enable 1");
}

TEST_F(RendererProtocol, ZoomPolicyVersionIncrementsEvenForRepeatedValue)
{
    Command("set global-zoom-active 0", "0: global-zoom-active 0");
    EXPECT_EQ(1, RendererProtocolGetSettings().zoomVersion);
    Command("set allow-4k-zoom 0", "0: allow-4k-zoom 0");
    EXPECT_EQ(2, RendererProtocolGetSettings().zoomVersion);
    Command("set allow-4k-zoom 0", "0: allow-4k-zoom 0");
    EXPECT_EQ(3, RendererProtocolGetSettings().zoomVersion);
    EXPECT_FALSE(RendererProtocolGetSettings().dirty);
}

TEST_F(RendererProtocol, NumericSettingsAcceptNonnegativeIntegersAndAtoiPrefix)
{
    for (const char *name : {"zoom-mode", "video-debug-level"})
    {
        const bool zoom = std::string(name) == "zoom-mode";
        for (const auto &item : std::vector<std::pair<std::string, int>>{
                 {"7", 7}, {"0", 0}, {"12suffix", 12}, {"nonnumeric", 0}})
        {
            auto expected = RendererProtocolGetSettings();
            if (zoom) expected.zoom = item.second;
            else expected.videoDebug = item.second;
            const std::string reply = std::string("0: ") + name + " " + std::to_string(item.second);
            Command(std::string("set ") + name + " " + item.first, reply);
            Command(std::string("get ") + name, reply);
            EXPECT_EQ(SettingsTuple(expected), SettingsTuple(RendererProtocolGetSettings()));
        }
    }
}

TEST_F(RendererProtocol, LogLevelClampsBothBoundsAndParsesNumericPrefix)
{
    for (const auto &item : std::vector<std::pair<std::string, int>>{
             {"-25", 0}, {"3", 3}, {"99", 6}, {"2tail", 2}, {"text", 0}})
    {
        auto expected = RendererProtocolGetSettings();
        expected.logLevel = item.second;
        Command("set loglevel " + item.first, "0: set loglevel " + std::to_string(item.second));
        Command("get loglevel", "0: loglevel " + std::to_string(item.second));
        EXPECT_EQ(SettingsTuple(expected), SettingsTuple(RendererProtocolGetSettings()));
    }
}

TEST_F(RendererProtocol, SpacesAndTrailingTokensFollowProductionTokenizer)
{
    Command("  get   mode  ignored ", "0: mode 1920x1080px60");
    Command("set zoom-mode 5 ignored", "0: zoom-mode 5");
    EXPECT_EQ(5, RendererProtocolGetSettings().zoom);
}

TEST_F(RendererProtocol, ModeReadReportsProgressiveInterlacedAndUnavailable)
{
    Command("get mode", "0: mode 1920x1080px60");
    RendererProtocolMode(720, 576, 50, true, true);
    Command("get mode", "0: mode 720x576ix50");
    RendererProtocolMode(720, 576, 50, true, false);
    Command("get mode", "-1: mode unavailable");
    Command("set mode 576i", "-1: set mode unavailable");
    Command("get mode", "-1: mode unavailable", true);
    Command("set mode 576i", "-1: set mode unavailable", true);
    EXPECT_EQ(0u, RendererCalls()->resourceQueries);
    EXPECT_FALSE(RendererProtocolGetSettings().dirty);
}

TEST_F(RendererProtocol, ModeSetSelectsRealConnectorModeAndMarksPendingAndDirty)
{
    RendererAddMode(1280, 720, 60, false);
    Command("set mode 720p", "0: set mode 720p");
    const auto m = RendererGetModeState();
    EXPECT_EQ(1280, m.width); EXPECT_EQ(720, m.height); EXPECT_EQ(60, m.rate);
    EXPECT_FALSE(m.interlaced);
    EXPECT_TRUE(m.pending); EXPECT_TRUE(m.sizeChanged); EXPECT_TRUE(m.explicitlySelected);
    EXPECT_TRUE(RendererProtocolGetSettings().dirty);
    EXPECT_TRUE(RendererProtocolGetSettings().forceDirty);
    EXPECT_EQ(1u, RendererCalls()->resourceQueries);
    EXPECT_EQ(1u, RendererCalls()->connectorQueries);
    EXPECT_EQ(1u, RendererCalls()->connectorFrees);
    // Requesting a mode does not pretend the asynchronous modeset completed.
    Command("get mode", "0: mode 1920x1080px60");
}

TEST_F(RendererProtocol, FailedModeRequestsDoNotDirtyOrScheduleAModeset)
{
    RendererAddMode(1920, 1080, 60, false);
    for (const char *mode : {"bad", "720p", "1080i50hz", "1080p25hz"})
    {
        Command(std::string("set mode ") + mode, std::string("-1: set mode ") + mode);
        EXPECT_FALSE(RendererProtocolGetSettings().dirty);
        EXPECT_FALSE(RendererProtocolGetSettings().forceDirty);
        EXPECT_FALSE(RendererGetModeState().pending);
    }
    RendererSetResourceFailure(true);
    Command("set mode 1080p", "-1: set mode 1080p");
    EXPECT_FALSE(RendererGetModeState().pending);
    EXPECT_FALSE(RendererProtocolGetSettings().dirty);
}

TEST_F(RendererProtocol, ZeroLengthAndNullMessageReusePriorResponseButWhitespaceClearsIt)
{
    Command("get mode", "0: mode 1920x1080px60");
    for (const char *input : {static_cast<const char *>(nullptr), "set zoom-mode 9", ""})
    {
        RendererProtocolCaptureReset();
        EXPECT_STREQ("0: mode 1920x1080px60", RendererProtocolCommand(fixture, input, 0, false));
        ASSERT_EQ(1, RendererProtocolPacketCount());
        Packet(0, DisplayBytes("0: mode 1920x1080px60"));
        EXPECT_EQ(0, RendererProtocolGetSettings().zoom);
    }
    Command("   ", "");
}

TEST_F(RendererProtocol, DisplaySendRetriesDoNotRepeatSettingMutationAndFailureDoesNotRollItBack)
{
    RendererProtocolTransport(2, 0);
    const char *command = "set global-zoom-active 1";
    EXPECT_STREQ("0: global-zoom-active 1", RendererProtocolCommand(fixture,
        command, static_cast<int>(std::strlen(command)), false));
    ASSERT_EQ(3, RendererProtocolPacketCount());
    for (int i = 0; i < 3; ++i) Packet(i, DisplayBytes("0: global-zoom-active 1"));
    EXPECT_EQ(1, RendererProtocolGetSettings().zoomVersion);
    EXPECT_TRUE(RendererProtocolGetSettings().globalZoom);
    RendererProtocolCaptureReset();
    RendererProtocolTransport(0, -1);
    command = "set global-zoom-active 0";
    EXPECT_STREQ("0: global-zoom-active 0", RendererProtocolCommand(fixture,
        command, static_cast<int>(std::strlen(command)), false));
    ASSERT_EQ(1, RendererProtocolPacketCount());
    Packet(0, DisplayBytes("0: global-zoom-active 0"));
    EXPECT_EQ(2, RendererProtocolGetSettings().zoomVersion);
    EXPECT_FALSE(RendererProtocolGetSettings().globalZoom);
}

class RendererProtocolResponseSend : public RendererProtocol,
    public ::testing::WithParamInterface<std::tuple<int, int>> {};

TEST_P(RendererProtocolResponseSend, FramesTextAndTerminatorAndHandlesTransportResults)
{
    const int length = std::get<0>(GetParam()), outcome = std::get<1>(GetParam());
    const std::string text(length, 'x');
    RendererProtocolTransport(outcome == 2 ? 2 : 0, outcome == 2 ? 0 : outcome);
    RendererProtocolResponse(fixture, text.c_str());
    const int attempts = outcome == 2 ? 3 : 1;
    ASSERT_EQ(attempts, RendererProtocolPacketCount());
    for (int i = 0; i < attempts; ++i) Packet(i, DisplayBytes(text));
    EXPECT_EQ(length, RendererProtocolResponseLength(fixture));
    EXPECT_TRUE(RendererProtocolMutexesUnlocked(fixture));
}
INSTANTIATE_TEST_SUITE_P(BoundariesAndFailures, RendererProtocolResponseSend,
    ::testing::Combine(::testing::Values(0, 1, 254), ::testing::Values(0, 1, -1, 2)));

// Independently specified network-order byte vectors: no production encoder or
// decoder is used to compute the oracle (including negative status/timestamps).
Bytes VideoBytes(char kind)
{
    switch (kind)
    {
        case 'R': return {'V','S',5,'R',0x12,0x34,0x56,0x78};
        case 'B': return {'V','S',5,'B',0x12,0x34,0x56,0x78};
        case 'D': return {'V','S',5,'D',0x12,0x34,0x56,0x78};
        case 'S': return {'V','S',13,'S',1,2,3,4,5,6,7,8,0x12,0x34,0x56,0x78};
        case 'U': return {'V','S',9,'U',1,2,3,4,5,6,7,8};
        case 'Z': return {'V','S',13,'Z',0,0,0,1,0,0,0,0,0x12,0x34,0x56,0x78};
        case 'Q': return {'V','S',9,'Q',0x12,0x34,0x56,0x78,0xff,0xff,0xff,0xfe};
        default: return {};
    }
}
class RendererProtocolVideoSend : public RendererProtocol,
    public ::testing::WithParamInterface<std::tuple<char, int>> {};

TEST_P(RendererProtocolVideoSend, ExactWireBytesRetriesAndSuccessOnlyPeerState)
{
    const char kind = std::get<0>(GetParam());
    const int outcome = std::get<1>(GetParam());
    Command("set global-zoom-active 1", "0: global-zoom-active 1");
    RendererProtocolCaptureReset();
    RendererProtocolTransport(outcome == 2 ? 2 : 0, outcome == 2 ? 0 : outcome);
    RendererProtocolVideo(fixture, kind, 0x0102030405060708LL, 0x12345678, -2);
    const bool success = outcome == 0 || outcome == 2;
    const int attempts = outcome == 2 ? 3 : 1;
    ASSERT_EQ(attempts, RendererProtocolPacketCount());
    for (int i = 0; i < attempts; ++i) Packet(i, VideoBytes(kind));
    const auto state = RendererProtocolPeer(fixture);
    EXPECT_EQ(success && kind == 'R' ? 0x12345678 : -7, state.refreshRate);
    EXPECT_EQ(success && kind == 'Z' ? 0x12345678 : -7, state.zoom);
    EXPECT_EQ(success && kind == 'Z' ? 1 : -7, state.zoomVersion);
    EXPECT_EQ(success && kind == 'D' ? 0x12345678 : -7, state.debugLevel);
    EXPECT_TRUE(RendererProtocolMutexesUnlocked(fixture));
}
INSTANTIATE_TEST_SUITE_P(AllSevenMessages, RendererProtocolVideoSend,
    ::testing::Combine(::testing::Values('R','B','S','U','Z','D','Q'), ::testing::Values(0,1,-1,2)));

TEST_F(RendererProtocol, NegativeTimestampAndZeroZoomFlagsAreSerializedExactly)
{
    RendererProtocolVideo(fixture, 'S', -2, -1, 0);
    Packet(0, {'V','S',13,'S',255,255,255,255,255,255,255,254,255,255,255,255});
    RendererProtocolVideo(fixture, 'U', 0, 0, 0);
    Packet(1, {'V','S',9,'U',0,0,0,0,0,0,0,0});
    RendererProtocolVideo(fixture, 'Z', 0, 0, 0);
    Packet(2, {'V','S',13,'Z',0,0,0,0,0,0,0,0,0,0,0,0});
    Command("set allow-4k-zoom 1", "0: allow-4k-zoom 1");
    RendererProtocolCaptureReset();
    RendererProtocolVideo(fixture, 'Z', 0, 4, 0);
    Packet(0, {'V','S',13,'Z',0,0,0,0,0,0,0,1,0,0,0,4});
    EXPECT_EQ(1, RendererProtocolPeer(fixture).zoomVersion);
    EXPECT_EQ(4, RendererProtocolPeer(fixture).zoom);
}

TEST_F(RendererProtocol, FailedSendPreservesPreviouslySuccessfulPeerState)
{
    for (char kind : {'R', 'Z', 'D'}) RendererProtocolVideo(fixture, kind, 0, 4, 0);
    Command("set global-zoom-active 1", "0: global-zoom-active 1");
    RendererProtocolCaptureReset();
    RendererProtocolTransport(1, -1);
    RendererProtocolVideo(fixture, 'Z', 0, 5, 0);
    ASSERT_EQ(2, RendererProtocolPacketCount());
    Packet(0, {'V','S',13,'Z',0,0,0,1,0,0,0,0,0,0,0,5});
    Packet(1, {'V','S',13,'Z',0,0,0,1,0,0,0,0,0,0,0,5});
    const auto state = RendererProtocolPeer(fixture);
    EXPECT_EQ(4, state.refreshRate); EXPECT_EQ(4, state.zoom);
    EXPECT_EQ(0, state.zoomVersion); EXPECT_EQ(4, state.debugLevel);
}

TEST_F(RendererProtocol, OffloadPushWithoutContextOrRunningWorkerIsANoOp)
{
    RendererProtocolQueuePush(fixture, 2, 0, 99, 4, false, false);
    RendererProtocolQueuePush(fixture, 2, 0, 99, 4, true, true);
    EXPECT_EQ(0, RendererProtocolQueue(fixture).count);
    EXPECT_EQ(0, RendererProtocolPacketCount());
}

TEST_F(RendererProtocol, OffloadExecuteDispatchesAllMessageKindsWithoutAWorker)
{
    RendererProtocolExecute(fixture, 0, 0, 0);
    RendererProtocolExecute(fixture, 999, 0, 0);
    RendererProtocolExecute(fixture, 5, 0, 0); // malloc-owned payload is freed.
    EXPECT_EQ(0, RendererProtocolPacketCount());
    RendererProtocolExecute(fixture, 1, 0, 7);
    RendererProtocolExecute(fixture, 2, 8, 9);
    RendererProtocolExecute(fixture, 3, 10, 0);
    RendererProtocolExecute(fixture, 4, 0, 211);
    ASSERT_EQ(3, RendererProtocolPacketCount());
    Packet(0, {'V','S',5,'B',0,0,0,7});
    Packet(1, {'V','S',13,'S',0,0,0,0,0,0,0,8,0,0,0,9});
    Packet(2, {'V','S',9,'U',0,0,0,0,0,0,0,10});
    EXPECT_EQ(1u, RendererCalls()->closeCalls);
}

TEST_F(RendererProtocol, OffloadQueueWrapsAndSelectiveFlushDoesNotDuplicateMessages)
{
    RendererProtocolQueueStartAt(fixture, 62);
    RendererProtocolQueuePush(fixture, 1, 0, 0, 7, true, false);
    RendererProtocolQueuePush(fixture, 2, 1, 8, 9, true, false);
    RendererProtocolQueuePush(fixture, 3, 0, 10, 0, true, false);
    RendererProtocolQueuePush(fixture, 0, 0, 0, 0, true, false);
    auto q = RendererProtocolQueue(fixture);
    ASSERT_EQ(4, q.count);
    EXPECT_EQ(62, q.readIndex); EXPECT_EQ(2, q.writeIndex);
    EXPECT_EQ(1, q.types[0]); EXPECT_EQ(7, q.values[0]);
    EXPECT_EQ(2, q.types[1]); EXPECT_EQ(8, q.times[1]); EXPECT_EQ(9, q.values[1]);
    EXPECT_EQ(3, q.types[2]); EXPECT_EQ(10, q.times[2]);
    EXPECT_EQ(0, RendererProtocolPacketCount());
    RendererProtocolQueueFlush(fixture, -1);
    EXPECT_EQ(0, RendererProtocolPacketCount());
    RendererProtocolQueueFlush(fixture, 0);
    ASSERT_EQ(2, RendererProtocolPacketCount());
    Packet(0, {'V','S',5,'B',0,0,0,7});
    Packet(1, {'V','S',9,'U',0,0,0,0,0,0,0,10});
    q = RendererProtocolQueue(fixture);
    EXPECT_EQ(4, q.count); // Flush tombstones entries; the consumer advances readIdx.
    EXPECT_EQ(0, q.types[0]); EXPECT_EQ(2, q.types[1]); EXPECT_EQ(0, q.types[2]);
    RendererProtocolQueueFlush(fixture, 0);
    EXPECT_EQ(2, RendererProtocolPacketCount());
    RendererProtocolQueueDrain(fixture);
    ASSERT_EQ(3, RendererProtocolPacketCount());
    Packet(2, {'V','S',13,'S',0,0,0,0,0,0,0,8,0,0,0,9});
    EXPECT_EQ(0, RendererProtocolQueue(fixture).count);
    EXPECT_EQ(2, RendererProtocolQueue(fixture).readIndex);
}

TEST_F(RendererProtocol, FullOffloadQueueExecutesOverflowImmediatelyThenDrainsInOrder)
{
    RendererProtocolQueueStartAt(fixture, 63);
    for (int i = 0; i < 63; ++i)
        RendererProtocolQueuePush(fixture, 1, 0, 0, i, true, false);
    auto q = RendererProtocolQueue(fixture);
    EXPECT_EQ(63, q.count); EXPECT_EQ(63, q.readIndex); EXPECT_EQ(62, q.writeIndex);
    EXPECT_EQ(0, RendererProtocolPacketCount());
    RendererProtocolQueuePush(fixture, 1, 0, 0, 99, true, false);
    ASSERT_EQ(1, RendererProtocolPacketCount());
    Packet(0, {'V','S',5,'B',0,0,0,99});
    q = RendererProtocolQueue(fixture);
    EXPECT_EQ(63, q.count); EXPECT_EQ(62, q.writeIndex);
    RendererProtocolQueueDrain(fixture);
    ASSERT_EQ(64, RendererProtocolPacketCount());
    for (int i = 0; i < 63; ++i)
        Packet(i + 1, {'V','S',5,'B',0,0,0,static_cast<unsigned char>(i)});
    EXPECT_EQ(0, RendererProtocolQueue(fixture).count);
}

TEST_F(RendererProtocol, OffloadStatusReportsDropCountAtEnqueueAndSerializesWhenDrained)
{
    RendererProtocolOffloadStatus(fixture, 0x0102030405060708LL, 0x12345678);
    RendererProtocolOffloadUnderflow(fixture, 0x0102030405060708LL);
    auto q = RendererProtocolQueue(fixture);
    ASSERT_EQ(2, q.count);
    EXPECT_EQ(2, q.types[0]); EXPECT_EQ(3, q.types[1]);
    EXPECT_EQ(0x12345678, q.reportedDrops);
    EXPECT_EQ(0x0102030405060708LL, q.times[0]);
    EXPECT_EQ(0x12345678, q.values[0]);
    EXPECT_EQ(0, RendererProtocolPacketCount());
    RendererProtocolQueueDrain(fixture);
    ASSERT_EQ(2, RendererProtocolPacketCount());
    Packet(0, VideoBytes('S')); Packet(1, VideoBytes('U'));
}

TEST_F(RendererProtocol, OffloadReleaseTransfersOwnershipClearsFrameAndClosesMockDescriptors)
{
    const auto state = RendererProtocolOffloadRelease(fixture, 42, false, false, true);
    EXPECT_EQ(42, state.bufferId);
    EXPECT_EQ(-1, state.fd0); EXPECT_EQ(-1, state.fd1); EXPECT_EQ(-1, state.fd2);
    EXPECT_FALSE(state.hasAllocation); EXPECT_FALSE(state.dropped); EXPECT_FALSE(state.kept);
    EXPECT_EQ(0u, state.fb); EXPECT_EQ(0u, state.handle0); EXPECT_EQ(0u, state.handle1);
    EXPECT_EQ(1, RendererProtocolQueue(fixture).count);
    EXPECT_EQ(0, RendererProtocolPacketCount());
    EXPECT_EQ(0u, RendererCalls()->closeCalls);
    EXPECT_EQ(1, RendererProtocolVideoDescriptorCount());
    RendererProtocolQueueFlush(fixture, 0);
    EXPECT_EQ(3u, RendererCalls()->closeCalls);
    EXPECT_EQ(0, RendererProtocolVideoDescriptorCount());
    ASSERT_EQ(1, RendererProtocolPacketCount());
    Packet(0, {'V','S',5,'B',0,0,0,42});
    RendererProtocolQueueDrain(fixture);
    EXPECT_EQ(3u, RendererCalls()->closeCalls);
    EXPECT_EQ(1, RendererProtocolPacketCount());
}

TEST_F(RendererProtocol, OffloadReleaseWithoutDescriptorsSendsWithoutClosing)
{
    const auto state = RendererProtocolOffloadRelease(fixture, 0, false, false, false);
    EXPECT_FALSE(state.hasAllocation);
    RendererProtocolQueueDrain(fixture);
    ASSERT_EQ(1, RendererProtocolPacketCount());
    Packet(0, {'V','S',5,'B',0,0,0,0});
    EXPECT_EQ(0u, RendererCalls()->closeCalls);
}

TEST_F(RendererProtocol, OffloadReleaseRejectsMissingInputsAndNegativeBufferWithoutMutation)
{
    for (int scenario = 0; scenario < 3; ++scenario)
    {
        const int id = scenario == 2 ? -1 : 42;
        const auto state = RendererProtocolOffloadRelease(fixture, id,
            scenario == 0, scenario == 1, true);
        EXPECT_EQ(id, state.bufferId);
        EXPECT_EQ(201, state.fd0); EXPECT_EQ(202, state.fd1); EXPECT_EQ(203, state.fd2);
        EXPECT_TRUE(state.hasAllocation); EXPECT_TRUE(state.dropped); EXPECT_TRUE(state.kept);
    }
    EXPECT_EQ(0, RendererProtocolQueue(fixture).count);
    EXPECT_EQ(0, RendererProtocolPacketCount());
    EXPECT_EQ(0u, RendererCalls()->closeCalls);
}

TEST_F(RendererProtocol, OffloadCloseAndFreeWrappersQueueNonConnectionWork)
{
    RendererProtocolOffloadClose(219);
    RendererProtocolOffloadFree();
    const auto q = RendererProtocolQueue(fixture);
    ASSERT_EQ(2, q.count);
    EXPECT_EQ(4, q.types[0]); EXPECT_EQ(219, q.values[0]); EXPECT_EQ(5, q.types[1]);
    RendererProtocolQueueFlush(fixture, 0);
    EXPECT_EQ(0u, RendererCalls()->closeCalls);
    EXPECT_EQ(2, RendererProtocolQueue(fixture).count);
    RendererProtocolQueueDrain(fixture);
    EXPECT_EQ(1u, RendererCalls()->closeCalls);
    EXPECT_EQ(0, RendererProtocolQueue(fixture).count);
    EXPECT_EQ(0, RendererProtocolPacketCount());
}

TEST_F(RendererProtocol, CaptureDispatcherDelegatesToFrameMockAfterProtocolFixtureEnds)
{
    RendererProtocolDestroy(fixture);
    fixture = nullptr;
    RendererFrameReleaseReset();
    unsigned char bytes[] = {'V','S',5,'B',0,0,0,42};
    struct iovec iov = {bytes, sizeof(bytes)};
    struct msghdr message{};
    message.msg_iov = &iov; message.msg_iovlen = 1;
    EXPECT_EQ(static_cast<ssize_t>(sizeof(bytes)), RendererProtocolSendmsg(-1, &message, MSG_NOSIGNAL));
    EXPECT_EQ(1, RendererFrameReleaseCount());
    EXPECT_EQ(42, RendererFrameReleasedId(0));
    EXPECT_TRUE(RendererFrameReleaseWireValid());
}
} // namespace