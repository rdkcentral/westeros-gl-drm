/* Test-only inclusion after the unchanged production source and shared mocks.
 * Wrappers construct inputs and expose state; they do not reproduce parsing,
 * message serialization, queue dispatch, or resource-release algorithms. */
#include "renderer_protocol_support.h"
#include "renderer_frame_support.h"
#include <assert.h>

struct RendererProtocolFixture {
    DisplayServerConnection display;
    VideoServerConnection video[2];
    VideoFrameManager manager;
};

static bool protocolCapturing;
static int protocolPacketCount, protocolInterrupts, protocolFinalResult;
static RendererProtocolPacket protocolPackets[256];

void RendererProtocolCaptureReset(void)
{
    protocolPacketCount = protocolInterrupts = protocolFinalResult = 0;
    memset(protocolPackets, 0, sizeof(protocolPackets));
}

void RendererProtocolTransport(int interrupts, int finalResult)
{
    assert(interrupts >= 0);
    protocolInterrupts = interrupts;
    protocolFinalResult = finalResult;
}

ssize_t RendererProtocolSendmsg(int fd, const struct msghdr *message, int flags)
{
    RendererProtocolPacket *packet;
    size_t i, total = 0;
    if (!protocolCapturing) return RendererFrameSendmsg(fd, message, flags);
    assert(protocolPacketCount < 256);
    packet = &protocolPackets[protocolPacketCount++];
    packet->fd = fd;
    packet->flags = flags;
    packet->envelopeValid = message->msg_name == NULL &&
        message->msg_namelen == 0 && message->msg_iovlen == 1 &&
        message->msg_control == NULL && message->msg_controllen == 0 &&
        message->msg_flags == 0;
    for (i = 0; i < message->msg_iovlen; ++i)
    {
        size_t length = message->msg_iov[i].iov_len;
        assert(total + length <= sizeof(packet->bytes));
        memcpy(packet->bytes + total, message->msg_iov[i].iov_base, length);
        total += length;
    }
    packet->length = total;
    if (protocolInterrupts > 0)
    {
        --protocolInterrupts;
        errno = EINTR;
        return -1;
    }
    if (protocolFinalResult < 0) { errno = EPIPE; return -1; }
    if (protocolFinalResult > 0) return total ? (ssize_t)total - 1 : 0;
    return (ssize_t)total;
}

int RendererProtocolPacketCount(void) { return protocolPacketCount; }
RendererProtocolPacket RendererProtocolPacketAt(int index)
{
    assert(index >= 0 && index < protocolPacketCount);
    return protocolPackets[index];
}

RendererProtocolFixture *RendererProtocolCreate(void)
{
    RendererProtocolFixture *f;
    assert(gCtx && !protocolCapturing && !gCtx->offloadThreadStarted);
    f = calloc(1, sizeof(*f));
    if (!f) return NULL;
    pthread_mutex_init(&f->display.mutex, NULL);
    pthread_mutex_init(&f->video[0].mutex, NULL);
    pthread_mutex_init(&f->video[1].mutex, NULL);
    f->display.socketFd = f->video[0].socketFd = f->video[1].socketFd = -1;
    f->video[0].refreshRate = f->video[0].zoomMode = -7;
    f->video[0].zoomPolicyVersion = f->video[0].videoDebugLevel = -7;
    memset(&gCtx->offloadMsgQ, 0, sizeof(gCtx->offloadMsgQ));
    pthread_mutex_init(&gCtx->offloadMsgQ.mutex, NULL);
    protocolCapturing = true;
    RendererProtocolCaptureReset();
    return f;
}

void RendererProtocolQueueDrain(RendererProtocolFixture *f)
{
    WstOffloadMsgQ *q = &gCtx->offloadMsgQ;
    (void)f;
    assert(!gCtx->offloadThreadStarted && !q->using_connection);
    while (q->readIdx != q->writeIdx)
    {
        WstOffloadMsg *msg = &q->msg[q->readIdx];
        wstOffloadMsgExecute(msg->msgType, msg->param_pvoid,
            msg->param_long_long, msg->param_int, msg->param_pvoid2);
        memset(msg, 0, sizeof(*msg));
        q->readIdx = (q->readIdx + 1) % OFFLOAD_QUEUE_CAPACITY;
    }
}

void RendererProtocolDestroy(RendererProtocolFixture *f)
{
    if (!f) return;
    /* A failing assertion in C++ still reaches fixture teardown. */
    RendererProtocolCaptureReset();
    RendererProtocolQueueDrain(f);
    pthread_mutex_destroy(&gCtx->offloadMsgQ.mutex);
    memset(&gCtx->offloadMsgQ, 0, sizeof(gCtx->offloadMsgQ));
    pthread_mutex_destroy(&f->display.mutex);
    pthread_mutex_destroy(&f->video[0].mutex);
    pthread_mutex_destroy(&f->video[1].mutex);
    free(f);
    protocolCapturing = false;
}

const char *RendererProtocolCommand(RendererProtocolFixture *f,
    const char *command, int length, bool hideContext)
{
    char text[256];
    WstGLCtx *saved = gCtx;
    if (command)
    {
        assert(strlen(command) < sizeof(text));
        strcpy(text, command);
    }
    if (hideContext) gCtx = NULL;
    wstDisplayServerProcessMessage(&f->display, length, command ? text : NULL);
    gCtx = saved;
    return f->display.response;
}

void RendererProtocolResponse(RendererProtocolFixture *f, const char *text)
{
    /* 254 text bytes + NUL fit the one-byte length field. */
    assert(strlen(text) <= 254 && strlen(text) < sizeof(f->display.response));
    strcpy(f->display.response, text);
    f->display.responseLen = strlen(text);
    wstDisplayServerSendResponse(&f->display);
}
int RendererProtocolResponseLength(RendererProtocolFixture *f)
{ return f->display.responseLen; }

void RendererProtocolMode(int width, int height, int rate, bool interlaced, bool available)
{
    gCtx->modeCurrent.hdisplay = width;
    gCtx->modeCurrent.vdisplay = height;
    gCtx->modeCurrent.vrefresh = rate;
    gCtx->modeCurrent.flags = interlaced ? DRM_MODE_FLAG_INTERLACE : 0;
    gCtx->modeInfo = available ? &gCtx->modeCurrent : NULL;
}
void RendererProtocolClearDirty(void) { gCtx->dirty = gCtx->forceDirty = false; }
RendererProtocolSettings RendererProtocolGetSettings(void)
{
    RendererProtocolSettings s = {0};
    s.display = gCtx->outputEnable; s.graphics = gCtx->graphicsEnable;
    s.video = gCtx->videoEnable; s.actualSize = gGraphicsActualSize;
    s.autoFRM = gCtx->autoFRMModeEnabled; s.globalZoom = gCtx->globalZoomActive;
    s.allow4k = gCtx->allow4kZoom; s.dirty = gCtx->dirty;
    s.forceDirty = gCtx->forceDirty; s.zoom = gCtx->zoomMode;
    s.zoomVersion = gCtx->zoomPolicyVersion; s.videoDebug = gCtx->videoDebugLevel;
    s.logLevel = g_activeLevel;
    return s;
}

void RendererProtocolVideo(RendererProtocolFixture *f, char kind,
    long long time, int value, int secondValue)
{
    VideoServerConnection *c = &f->video[0];
    switch (kind)
    {
        case 'R': wstVideoServerSendRefreshRate(c, value); break;
        case 'B': wstVideoServerSendBufferRelease(c, value); break;
        case 'S': wstVideoServerSendStatus(c, time, value); break;
        case 'U': wstVideoServerSendUnderflow(c, time); break;
        case 'Z': wstVideoServerSendZoomMode(c, gCtx, value); break;
        case 'D': wstVideoServerSendDebugLevel(c, value); break;
        case 'Q': wstVideoServerSendAuthResponse(c, value, secondValue); break;
        default: assert(!"invalid video message discriminator");
    }
}
RendererProtocolPeerState RendererProtocolPeer(RendererProtocolFixture *f)
{
    RendererProtocolPeerState s = { f->video[0].refreshRate,
        f->video[0].zoomMode, f->video[0].zoomPolicyVersion,
        f->video[0].videoDebugLevel };
    return s;
}
bool RendererProtocolMutexesUnlocked(RendererProtocolFixture *f)
{
    pthread_mutex_t *mutexes[] = { &f->display.mutex, &f->video[0].mutex,
        &f->video[1].mutex, &gCtx->mutex, &gMutex, &gCtx->offloadMsgQ.mutex };
    unsigned i;
    for (i = 0; i < sizeof(mutexes)/sizeof(mutexes[0]); ++i)
    {
        if (pthread_mutex_trylock(mutexes[i])) return false;
        pthread_mutex_unlock(mutexes[i]);
    }
    return true;
}

void RendererProtocolQueueStartAt(RendererProtocolFixture *f, int index)
{
    (void)f;
    assert(index >= 0 && index < OFFLOAD_QUEUE_CAPACITY);
    assert(gCtx->offloadMsgQ.readIdx == gCtx->offloadMsgQ.writeIdx);
    gCtx->offloadMsgQ.readIdx = gCtx->offloadMsgQ.writeIdx = index;
}
void RendererProtocolQueuePush(RendererProtocolFixture *f, unsigned type,
    int peer, long long time, int value, bool running, bool hideContext)
{
    WstGLCtx *saved = gCtx;
    assert(peer >= 0 && peer < 2);
    saved->offloadThreadStarted = running;
    if (hideContext) gCtx = NULL;
    wstOffloadMsgPush(type, &f->video[peer], time, value, NULL);
    gCtx = saved;
    saved->offloadThreadStarted = false;
}
void RendererProtocolQueueFlush(RendererProtocolFixture *f, int peer)
{
    assert(!gCtx->offloadMsgQ.using_connection);
    wstOffloadFlushConn(peer < 0 ? NULL : &f->video[peer]);
}
RendererProtocolQueueState RendererProtocolQueue(RendererProtocolFixture *f)
{
    RendererProtocolQueueState s = {0};
    WstOffloadMsgQ *q = &gCtx->offloadMsgQ;
    int i;
    s.readIndex = q->readIdx; s.writeIndex = q->writeIdx;
    s.count = (q->writeIdx - q->readIdx + OFFLOAD_QUEUE_CAPACITY) % OFFLOAD_QUEUE_CAPACITY;
    for (i = 0; i < s.count; ++i)
    {
        WstOffloadMsg *msg = &q->msg[(q->readIdx + i) % OFFLOAD_QUEUE_CAPACITY];
        s.types[i] = msg->msgType; s.values[i] = msg->param_int;
        s.times[i] = msg->param_long_long;
    }
    s.reportedDrops = f->manager.dropFrameCountReported;
    return s;
}
void RendererProtocolExecute(RendererProtocolFixture *f, unsigned type,
    long long time, int value)
{
    /* FREE_VF must receive an allocation, never the fixture's connection. */
    void *arg = type == WST_OLM_FREE_VF_BUFF ? malloc(16) : &f->video[0];
    wstOffloadMsgExecute(type, arg, time, value, NULL);
}
void RendererProtocolOffloadStatus(RendererProtocolFixture *f, long long time, int drops)
{
    gCtx->offloadThreadStarted = true;
    wstOffloadSendStatus(&f->video[0], &f->manager, time, drops);
    gCtx->offloadThreadStarted = false;
}
void RendererProtocolOffloadUnderflow(RendererProtocolFixture *f, long long time)
{
    gCtx->offloadThreadStarted = true;
    wstOffloadSendUnderflow(&f->video[0], time);
    gCtx->offloadThreadStarted = false;
}
RendererProtocolReleasedFrame RendererProtocolOffloadRelease(RendererProtocolFixture *f,
    int bufferId, bool nullConnection, bool nullFrame, bool withDescriptors)
{
    VideoFrame frame = {0};
    RendererProtocolReleasedFrame s = {0};
    frame.bufferId = bufferId;
    frame.fd0 = withDescriptors ? 201 : -1;
    frame.fd1 = withDescriptors ? 202 : -1;
    frame.fd2 = withDescriptors ? 203 : -1;
    frame.vf = malloc(8);
    /* dropped suppresses video-fence ioctl even if that feature is enabled. */
    frame.dropped = frame.kept = true;
    /* Model a tracked import before its corresponding release. The production
     * counter tracks one frame's fd group, not each individual plane fd. */
    if (withDescriptors && bufferId >= 0 && !nullConnection && !nullFrame)
        wstUpdateResources(WSTRES_FD_VIDEO, true, frame.fd0, __LINE__);
    gCtx->offloadThreadStarted = true;
    wstOffloadSendBufferRelease(nullConnection ? NULL : &f->video[0], nullFrame ? NULL : &frame);
    gCtx->offloadThreadStarted = false;
    s.bufferId = frame.bufferId; s.fd0 = frame.fd0; s.fd1 = frame.fd1; s.fd2 = frame.fd2;
    s.dropped = frame.dropped; s.kept = frame.kept; s.hasAllocation = frame.vf != NULL;
    s.fb = frame.fbId; s.handle0 = frame.handle0; s.handle1 = frame.handle1;
    /* Early-return inputs retain ownership; successful calls transfer it. */
    free(frame.vf);
    return s;
}
int RendererProtocolVideoDescriptorCount(void)
{ return gResources ? gResources->fdVideoCount : 0; }
void RendererProtocolOffloadClose(int descriptor)
{
    gCtx->offloadThreadStarted = true;
    wstOffloadCloseFileHande(descriptor);
    gCtx->offloadThreadStarted = false;
}
void RendererProtocolOffloadFree(void)
{
    gCtx->offloadThreadStarted = true;
    wstOffloadFreeVf(malloc(8));
    gCtx->offloadThreadStarted = false;
}