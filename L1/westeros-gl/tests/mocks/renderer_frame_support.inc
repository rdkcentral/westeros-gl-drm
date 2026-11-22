/* Included ONLY at the end of renderer_test_support.c. Wrappers invoke the
 * original static functions; observations and input construction live here.
 * The suite targets NO_GENERIC_AVSYNC, without platform AV-sync enabled. */
#include "renderer_frame_support.h"
#include <assert.h>

struct RendererFrameFixture {
    VideoServerConnection conn;
    WstOverlayPlane plane;
    VideoFrameManager *manager;
};
struct RendererOverlayFixture {
    WstOverlayPlanes planes;
    WstOverlayPlane nodes[8];
};

static int frameReleasedIds[256], frameReleaseCount;
static bool frameReleaseWireValid = true;

void RendererFrameReleaseReset(void)
{
    frameReleaseCount = 0;
    frameReleaseWireValid = true;
}
int RendererFrameReleaseCount(void) { return frameReleaseCount; }
int RendererFrameReleasedId(int index)
{
    return (index >= 0 && index < frameReleaseCount && index < 256)
        ? frameReleasedIds[index] : -1;
}
bool RendererFrameReleaseWireValid(void) { return frameReleaseWireValid; }

ssize_t RendererFrameSendmsg(int fd, const struct msghdr *message, int flags)
{
    size_t i;
    ssize_t length = 0;
    (void)fd;
    for (i = 0; i < message->msg_iovlen; ++i)
        length += message->msg_iov[i].iov_len;
    if (message->msg_iovlen == 1 && message->msg_iov[0].iov_len == 8)
    {
        const unsigned char *bytes = message->msg_iov[0].iov_base;
        if (bytes[3] == 'B')
        {
            frameReleaseWireValid = frameReleaseWireValid &&
                bytes[0] == 'V' && bytes[1] == 'S' && bytes[2] == 5 &&
                flags == MSG_NOSIGNAL && message->msg_controllen == 0;
            if (frameReleaseCount < 256)
                frameReleasedIds[frameReleaseCount] =
                    ((unsigned)bytes[4] << 24) | ((unsigned)bytes[5] << 16) |
                    ((unsigned)bytes[6] << 8) | bytes[7];
            ++frameReleaseCount;
        }
    }
    return length;
}

static RendererFrameView rendererFrameView(const VideoFrame *frame)
{
    RendererFrameView view = {0};
    view.id = frame->bufferId;
    view.number = frame->frameNumber;
    view.fd0 = frame->fd0; view.fd1 = frame->fd1; view.fd2 = frame->fd2;
    view.time = frame->frameTime;
    view.canExpire = frame->canExpire; view.advanced = frame->advanced;
    view.x = frame->rectX; view.y = frame->rectY;
    view.width = frame->rectW; view.height = frame->rectH;
    view.visibleWidth = frame->frameWidthVisible;
    view.visibleHeight = frame->frameHeightVisible;
    return view;
}

RendererFrameFixture *RendererFramesCreate(bool immediate)
{
    int i;
    RendererFrameFixture *fixture = calloc(1, sizeof(*fixture));
    if (!fixture) return NULL;
    pthread_mutex_init(&fixture->conn.mutex, NULL);
    fixture->conn.socketFd = -1; /* Even missing sendmsg wiring cannot touch a device. */
    fixture->conn.syncType = immediate ? SYNC_IMMEDIATE : SYNC_VMASTER;
    fixture->conn.videoPlane = &fixture->plane;
    for (i = 0; i < ACTIVE_FRAMES; ++i)
    {
        fixture->plane.videoFrame[i].bufferId = -1;
        fixture->plane.videoFrame[i].fd0 = -1;
        fixture->plane.videoFrame[i].fd1 = -1;
        fixture->plane.videoFrame[i].fd2 = -1;
        fixture->plane.videoFrame[i].frameWidth = 1920;
        fixture->plane.videoFrame[i].frameHeight = 1080;
    }
    fixture->manager = wstCreateVideoFrameManager(&fixture->conn);
    if (!fixture->manager)
    {
        pthread_mutex_destroy(&fixture->conn.mutex);
        free(fixture);
        return NULL;
    }
    fixture->plane.vfm = fixture->manager;
    return fixture;
}

void RendererFramesDestroy(RendererFrameFixture *fixture)
{
    if (!fixture) { wstDestroyVideoFrameManager(NULL); return; }
    wstDestroyVideoFrameManager(fixture->manager);
    pthread_mutex_destroy(&fixture->conn.mutex);
    free(fixture);
}

void RendererFramesPush(RendererFrameFixture *fixture, int id, long long time)
{
    VideoFrame frame = {0};
    frame.bufferId = id; frame.frameNumber = id + 1000;
    frame.frameTime = time;
    frame.fd0 = frame.fd1 = frame.fd2 = -1;
    frame.frameWidth = 1920; frame.frameHeight = 1080;
    frame.plane = &fixture->plane;
    wstVideoFrameManagerPushFrame(fixture->manager, &frame);
}

bool RendererFramesPop(RendererFrameFixture *fixture, long long blank,
                       long long interval, RendererFrameView *out)
{
    VideoFrame *result;
    WstOffloadMsgQ *queue = &gCtx->offloadMsgQ;
    /* Model a running offload consumer without ever creating a thread. Pop
     * only enqueues work. Drain that work synchronously before returning.
     * Never leave threadStarted true for RendererCleanup's join path. */
    assert(!gCtx->offloadThreadStarted);
    memset(queue, 0, sizeof(*queue));
    pthread_mutex_init(&queue->mutex, NULL);
    gCtx->offloadThreadStarted = true;
    fixture->manager->vblankTime = blank;
    fixture->manager->vblankInterval = interval;
    result = wstVideoFrameManagerPopFrame(fixture->manager);
    if (result && out) *out = rendererFrameView(result);
    gCtx->offloadThreadStarted = false;
    while (queue->readIdx != queue->writeIdx)
    {
        WstOffloadMsg *msg = &queue->msg[queue->readIdx];
        wstOffloadMsgExecute(msg->msgType, msg->param_pvoid,
            msg->param_long_long, msg->param_int, msg->param_pvoid2);
        queue->readIdx = (queue->readIdx + 1) % OFFLOAD_QUEUE_CAPACITY;
    }
    pthread_mutex_destroy(&queue->mutex);
    memset(queue, 0, sizeof(*queue));
    return result != NULL;
}

RendererFrameView RendererFramesAt(RendererFrameFixture *fixture, int index)
{
    assert(index >= 0 && index < fixture->manager->queueCapacity);
    return rendererFrameView(&fixture->manager->queue[index]);
}
RendererFrameView RendererFramesCurrent(RendererFrameFixture *fixture)
{ return rendererFrameView(&fixture->plane.videoFrame[FRAME_CURR]); }
RendererFrameState RendererFramesState(RendererFrameFixture *fixture)
{
    VideoFrameManager *v = fixture->manager;
    RendererFrameState state = {0};
    state.size = v->queueSize; state.capacity = v->queueCapacity;
    state.currentId = v->bufferIdCurrent; state.syncSession = v->syncSession;
    state.drops = v->dropFrameCount; state.rate = v->rate;
    state.paused = v->paused; state.resetBaseTime = v->resetBaseTime;
    state.advance = v->frameAdvance; state.underflow = v->underflowDetected;
    state.underflowReported = v->underflowReported;
    state.flipBase = v->flipTimeBase; state.frameBase = v->frameTimeBase;
    state.flipCurrent = v->flipTimeCurrent; state.frameCurrent = v->frameTimeCurrent;
    state.adjust = v->adjust;
    state.dirty = fixture->plane.dirty; state.ready = fixture->plane.readyToFlip;
    state.forceDirty = gCtx->forceDirty;
    return state;
}
void RendererFramesPause(RendererFrameFixture *f, bool pause)
{ wstVideoFrameManagerPause(f->manager, pause); }
void RendererFramesAdvance(RendererFrameFixture *f)
{ wstVideoFrameManagerFrameAdvance(f->manager); }
void RendererFramesEos(RendererFrameFixture *f)
{ wstVideoFrameManagerEos(f->manager); }
void RendererFramesSyncType(RendererFrameFixture *f, int type)
{ wstVideoFrameManagerSetSyncType(f->manager, type); }
void RendererFramesRect(RendererFrameFixture *f, int x, int y, int w, int h)
{ wstVideoFrameManagerUpdateRect(f->manager, x, y, w, h); }
void RendererFramesTiming(RendererFrameFixture *f, long long flipBase,
                          long long frameBase, long long expireLimit, float rate)
{
    f->manager->flipTimeBase = flipBase; f->manager->frameTimeBase = frameBase;
    f->manager->expireLimit = expireLimit; f->manager->rate = rate;
}
void RendererFramesDisplayed(RendererFrameFixture *f, int id, long long time)
{
    f->manager->bufferIdCurrent = id;
    f->plane.videoFrame[FRAME_CURR].bufferId = id;
    f->plane.videoFrame[FRAME_CURR].frameTime = time;
}
void RendererFramesUnderflowReported(RendererFrameFixture *f, bool reported)
{ f->manager->underflowReported = reported; }

RendererOverlayFixture *RendererOverlaysCreate(void)
{ return calloc(1, sizeof(RendererOverlayFixture)); }
void RendererOverlaysDestroy(RendererOverlayFixture *fixture) { free(fixture); }
int RendererOverlaysAdd(RendererOverlayFixture *f, int z, bool video,
                       bool matching, bool primary)
{
    int index = f->planes.totalCount++;
    WstOverlayPlane *plane;
    assert(index < 8);
    plane = &f->nodes[index];
    plane->zOrder = z; plane->supportsVideo = video;
    plane->supportsGraphics = true; plane->frameRateMatchingPlane = matching;
    if (primary) f->planes.primary = plane;
    wstOverlayAppendUnused(&f->planes, plane);
    return index;
}
static int rendererOverlayIndex(RendererOverlayFixture *f, WstOverlayPlane *p)
{ return p ? (int)(p - f->nodes) : -1; }
int RendererOverlaysAlloc(RendererOverlayFixture *f, bool graphics, bool primaryVideo)
{ return rendererOverlayIndex(f, wstOverlayAlloc(&f->planes, graphics, primaryVideo)); }
int RendererOverlaysPrimary(RendererOverlayFixture *f)
{ return rendererOverlayIndex(f, wstOverlayAllocPrimary(&f->planes)); }
void RendererOverlaysFree(RendererOverlayFixture *f, int index)
{ wstOverlayFree(&f->planes, index < 0 ? NULL : &f->nodes[index]); }
void RendererOverlaysSeed(RendererOverlayFixture *f, int index, bool keep)
{
    int i;
    WstOverlayPlane *p = &f->nodes[index];
    p->frameCount = 15; p->frameTimeBase = 1000; p->flipTimeBase = 2000;
    p->hide = p->hidden = true; p->keepLastFrame = keep;
    p->conn = (VideoServerConnection *)f; /* Identity only; never dereferenced. */
    p->videoResourceId = 42;
    for (i = 0; i < ACTIVE_FRAMES; ++i)
    {
        p->videoFrame[i].bufferId = 100 + i;
        p->videoFrame[i].plane = p;
    }
}
RendererOverlayLists RendererOverlaysLists(RendererOverlayFixture *f)
{
    RendererOverlayLists view = {0};
    WstOverlayPlane *p, *prev = NULL;
    view.count = f->planes.totalCount; view.usedCount = f->planes.usedCount;
    view.linksValid = true;
    for (p = f->planes.availHead; p && view.availableCount < 8; p = p->next)
    {
        view.linksValid &= p->prev == prev && !p->inUse;
        view.available[view.availableCount++] = rendererOverlayIndex(f, p);
        prev = p;
    }
    view.linksValid &= !p && prev == f->planes.availTail;
    prev = NULL;
    for (p = f->planes.usedHead; p && view.usedListCount < 8; p = p->next)
    {
        view.linksValid &= p->prev == prev && p->inUse;
        view.used[view.usedListCount++] = rendererOverlayIndex(f, p);
        prev = p;
    }
    view.linksValid &= !p && prev == f->planes.usedTail &&
        view.usedListCount == view.usedCount &&
        view.usedListCount + view.availableCount == view.count;
    return view;
}
RendererOverlayState RendererOverlaysState(RendererOverlayFixture *f, int index)
{
    int i;
    WstOverlayPlane *p = &f->nodes[index];
    RendererOverlayState state = {0};
    state.inUse = p->inUse; state.hide = p->hide; state.hidden = p->hidden;
    state.keepLast = p->keepLastFrame; state.hasConnection = p->conn != NULL;
    state.frameCount = p->frameCount; state.resourceId = p->videoResourceId;
    state.frameBase = p->frameTimeBase; state.flipBase = p->flipTimeBase;
    for (i = 0; i < ACTIVE_FRAMES; ++i)
    {
        state.buffers[i] = p->videoFrame[i].bufferId;
        state.kept[i] = p->videoFrame[i].kept;
        state.hasPlane[i] = p->videoFrame[i].plane != NULL;
    }
    return state;
}

void RendererFrameModeType(int index, bool driver, bool preferred)
{
    assert(index >= 0 && index < connector.count_modes);
    modes[index].type = (driver ? DRM_MODE_TYPE_DRIVER : 0) |
                       (preferred ? DRM_MODE_TYPE_PREFERRED : 0);
}
void RendererFrameSelectMode(int width, int height, bool atomic)
{
    drmModeConnector *saved = gCtx->conn;
    bool savedAtomic = gCtx->haveAtomic;
    gCtx->conn = &connector; gCtx->haveAtomic = atomic;
    wstSelectMode(gCtx, width, height);
    gCtx->conn = saved; gCtx->haveAtomic = savedAtomic;
}
void RendererFrameSelectRate(int numerator, int denominator)
{
    drmModeConnector *saved = gCtx->conn;
    gCtx->conn = &connector;
    wstSelectRate(gCtx, numerator, denominator);
    gCtx->conn = saved;
}
void RendererFrameCurrentRate(int rate) { gCtx->modeCurrent.vrefresh = rate; }
int RendererFrameCurrentModeWidth(void) { return gCtx->modeCurrent.hdisplay; }
int RendererFrameCurrentModeHeight(void) { return gCtx->modeCurrent.vdisplay; }
int RendererFrameCurrentModeRate(void) { return gCtx->modeCurrent.vrefresh; }