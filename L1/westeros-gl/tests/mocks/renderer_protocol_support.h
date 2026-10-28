#ifndef WESTEROS_RENDERER_PROTOCOL_SUPPORT_H
#define WESTEROS_RENDERER_PROTOCOL_SUPPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Main harness integration (no production edits):
 * - Include this header before including westeros-gl.c.
 * - Redirect sendmsg to RendererProtocolSendmsg around that source inclusion,
 *   instead of redirecting it directly to RendererFrameSendmsg.
 * - Include renderer_frame_support.inc and then renderer_protocol_support.inc
 *   at the END of renderer_test_support.c; add renderer_protocol_test.cpp to
 *   the existing renderer test executable.
 * When no protocol fixture exists the dispatcher delegates to the frame mock.
 * No socket, device, thread, clock or sleep redirects are needed by this suite.
 */
struct msghdr;
ssize_t RendererProtocolSendmsg(int fd, const struct msghdr *message, int flags);

typedef struct RendererProtocolFixture RendererProtocolFixture;
typedef struct {
    unsigned char bytes[260];
    size_t length;
    int fd, flags;
    bool envelopeValid;
} RendererProtocolPacket;
typedef struct {
    bool display, graphics, video, actualSize, autoFRM, globalZoom, allow4k;
    bool dirty, forceDirty;
    int zoom, zoomVersion, videoDebug, logLevel;
} RendererProtocolSettings;
typedef struct {
    int refreshRate, zoom, zoomVersion, debugLevel;
} RendererProtocolPeerState;
typedef struct {
    int readIndex, writeIndex, count, types[64], values[64];
    long long times[64];
    int reportedDrops;
} RendererProtocolQueueState;
typedef struct {
    int bufferId, fd0, fd1, fd2;
    bool dropped, kept, hasAllocation;
    unsigned fb, handle0, handle1;
} RendererProtocolReleasedFrame;

RendererProtocolFixture *RendererProtocolCreate(void);
void RendererProtocolDestroy(RendererProtocolFixture *fixture);
void RendererProtocolCaptureReset(void);
/* finalResult: 0=full send, 1=short send, -1=EPIPE; interrupts are EINTR. */
void RendererProtocolTransport(int interrupts, int finalResult);
int RendererProtocolPacketCount(void);
RendererProtocolPacket RendererProtocolPacketAt(int index);
const char *RendererProtocolCommand(RendererProtocolFixture *fixture,
    const char *command, int length, bool hideContext);
void RendererProtocolResponse(RendererProtocolFixture *fixture, const char *text);
int RendererProtocolResponseLength(RendererProtocolFixture *fixture);
void RendererProtocolMode(int width, int height, int rate, bool interlaced, bool available);
void RendererProtocolClearDirty(void);
RendererProtocolSettings RendererProtocolGetSettings(void);
/* kind is the wire discriminator: R/B/S/U/Z/D/Q. */
void RendererProtocolVideo(RendererProtocolFixture *fixture, char kind,
    long long time, int value, int secondValue);
RendererProtocolPeerState RendererProtocolPeer(RendererProtocolFixture *fixture);
bool RendererProtocolMutexesUnlocked(RendererProtocolFixture *fixture);

/* Offload wrappers initialize the real queue mutex but never start a worker.
 * The running flag is set only during a push. Explicit drain/flush executes
 * original production functions synchronously; using_connection stays NULL.
 */
void RendererProtocolQueueStartAt(RendererProtocolFixture *fixture, int index);
void RendererProtocolQueuePush(RendererProtocolFixture *fixture, unsigned type,
    int peer, long long time, int value, bool running, bool hideContext);
void RendererProtocolQueueFlush(RendererProtocolFixture *fixture, int peer);
void RendererProtocolQueueDrain(RendererProtocolFixture *fixture);
RendererProtocolQueueState RendererProtocolQueue(RendererProtocolFixture *fixture);
void RendererProtocolExecute(RendererProtocolFixture *fixture, unsigned type,
    long long time, int value);
void RendererProtocolOffloadStatus(RendererProtocolFixture *fixture, long long time, int drops);
void RendererProtocolOffloadUnderflow(RendererProtocolFixture *fixture, long long time);
RendererProtocolReleasedFrame RendererProtocolOffloadRelease(RendererProtocolFixture *fixture,
    int bufferId, bool nullConnection, bool nullFrame, bool withDescriptors);
int RendererProtocolVideoDescriptorCount(void);
void RendererProtocolOffloadClose(int descriptor);
void RendererProtocolOffloadFree(void);

#ifdef __cplusplus
}
#endif
#endif