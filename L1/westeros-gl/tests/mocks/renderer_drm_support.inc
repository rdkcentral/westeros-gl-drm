/* Append to renderer_test_support.c after the production include and undefs.
 * No worker threads, descriptors, DRM ioctls or GBM objects are real. The CMake
 * target must retain DRM_NO_OUT_FENCE/DRM_NO_NATIVE_FENCE/DRM_NO_REFRESH_LOCK.
 * The shared harness owns open/close/resources/connector/surface/sendmsg seams.
 */
#include "renderer_drm_support.h"
#include <assert.h>

#if defined(DRM_USE_OUT_FENCE) || defined(DRM_USE_NATIVE_FENCE) || defined(DRM_USE_VIDEO_FENCE)
#error "Renderer DRM fixtures require disabled fence features; no real poll/EGL fence calls are permitted"
#endif

static RendererDRMFaults rd_fault;
static RendererDRMTrace rd_trace;
static WstOverlayPlane *rd_graphics, *rd_video;
static VideoServerConnection rd_connection;
static VideoFrameManager rd_manager;
static void *rd_event_data;
static bool rd_created;
static struct { unsigned id; bool busy; } rd_bos[64];
static unsigned rd_fb_live[128];
static const char *const rd_connector_names[] = { "CRTC_ID", "link-status" };
static const char *const rd_crtc_names[] = { "MODE_ID", "ACTIVE", "OUT_FENCE_PTR" };
static const char *const rd_plane_names[] = {
    "FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W", "SRC_H",
    "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H", "IN_FENCE_FD", "zpos"
};

static const char *rd_name(unsigned id)
{
    unsigned group = id / 100, index = id % 100;
    if (!index) return "";
    --index;
    if (group == RD_CONNECTOR && index < 2) return rd_connector_names[index];
    if (group == RD_CRTC && index < 3) return rd_crtc_names[index];
    if ((group == RD_GRAPHICS || group == RD_VIDEO) && index < 12)
        return rd_plane_names[index];
    return "";
}

static drmModeObjectPropertiesPtr rd_object_properties(int fd, uint32_t object, uint32_t type)
{
    drmModeObjectPropertiesPtr p;
    unsigned i, group = type == DRM_MODE_OBJECT_CONNECTOR ? RD_CONNECTOR :
        type == DRM_MODE_OBJECT_CRTC ? RD_CRTC :
        object == RD_VIDEO_ID ? RD_VIDEO : RD_GRAPHICS;
    (void)fd;
    ++rd_trace.objectQueries;
    rd_trace.queryObject = object; rd_trace.queryType = type;
    if (rd_fault.failObject) return NULL;
    p = calloc(1, sizeof(*p)); assert(p);
    p->count_props = group == RD_CONNECTOR ? 2 : group == RD_CRTC ? 3 : 12;
    p->props = calloc(p->count_props, sizeof(*p->props));
    p->prop_values = calloc(p->count_props, sizeof(*p->prop_values));
    assert(p->props && p->prop_values);
    for (i = 0; i < p->count_props; ++i)
    {
        p->props[i] = group * 100 + i + 1;
        p->prop_values[i] = 700 + i;
    }
    ++rd_trace.liveObjects;
    return p;
}
static drmModePropertyPtr rd_property(int fd, uint32_t id)
{
    drmModePropertyPtr p;
    (void)fd; ++rd_trace.propertyQueries;
    if (rd_fault.failPropertyId == id) return NULL;
    p = calloc(1, sizeof(*p)); assert(p);
    p->prop_id = id;
    snprintf(p->name, sizeof(p->name), "%s", rd_name(id));
    /* Exercise the enum traversal in plane acquisition as well. */
    p->flags = DRM_MODE_PROP_ENUM;
    p->count_enums = 1;
    p->enums = calloc(1, sizeof(*p->enums)); assert(p->enums);
    p->enums[0].value = 9;
    snprintf(p->enums[0].name, sizeof(p->enums[0].name), "synthetic");
    ++rd_trace.liveProperties;
    return p;
}
static void rd_free_object_properties(drmModeObjectPropertiesPtr p)
{
    if (!p) return;
    ++rd_trace.objectFrees; --rd_trace.liveObjects;
    free(p->props); free(p->prop_values); free(p);
}
static void rd_free_property(drmModePropertyPtr p)
{
    if (!p) return;
    ++rd_trace.propertyFrees; --rd_trace.liveProperties;
    free(p->values); free(p->enums); free(p->blob_ids); free(p);
}
static drmModeAtomicReqPtr rd_atomic_alloc(void)
{
    void *p;
    ++rd_trace.atomicAllocs;
    if (rd_fault.failAtomicAlloc) return NULL;
    p = malloc(1); assert(p); ++rd_trace.liveRequests;
    return p;
}
static void rd_atomic_free(drmModeAtomicReqPtr p)
{
    assert(p); ++rd_trace.atomicFrees; --rd_trace.liveRequests; free(p);
}
static int rd_atomic_add(drmModeAtomicReqPtr req, uint32_t object, uint32_t property, uint64_t value)
{
    RendererDRMPropertyCall *p;
    assert(req && rd_trace.addCount < 256);
    p = &rd_trace.adds[rd_trace.addCount++];
    p->object = object; p->property = property; p->value = value;
    snprintf(p->name, sizeof(p->name), "%s", rd_name(property));
    return rd_fault.atomicAddResult;
}
static int rd_atomic_commit(int fd, drmModeAtomicReqPtr req, uint32_t flags, void *data)
{
    (void)fd; (void)data; assert(req);
    ++rd_trace.commitCount; rd_trace.commitFlags = flags;
    return rd_fault.commitResult;
}
static int rd_create_blob(int fd, const void *data, size_t length, uint32_t *id)
{
    const drmModeModeInfo *mode = data;
    (void)fd;
    assert(data && length == sizeof(*mode));
    ++rd_trace.blobCreates;
    rd_trace.blobWidth = mode->hdisplay; rd_trace.blobHeight = mode->vdisplay;
    rd_trace.blobRate = mode->vrefresh;
    if (rd_fault.blobResult) return rd_fault.blobResult;
    *id = RD_BLOB_ID; ++rd_trace.liveBlobs;
    return 0;
}
static int rd_destroy_blob(int fd, uint32_t id)
{
    (void)fd; assert(id == RD_BLOB_ID);
    ++rd_trace.blobDestroys;
    if (!rd_fault.blobDestroyResult) --rd_trace.liveBlobs;
    return rd_fault.blobDestroyResult;
}
static void rd_track_fb(unsigned id)
{
    unsigned i;
    if (!id) return;
    for (i = 0; i < 128; ++i) if (rd_fb_live[i] == id) return;
    for (i = 0; i < 128; ++i) if (!rd_fb_live[i])
    { rd_fb_live[i] = id; ++rd_trace.outstandingFB; return; }
    abort();
}
static bool rd_has_fb(unsigned id)
{
    unsigned i;
    if (!id) return false;
    for (i = 0; i < 128; ++i) if (rd_fb_live[i] == id) return true;
    return false;
}
static int rd_add_fb(int fd, uint32_t width, uint32_t height, uint8_t depth,
                     uint8_t bpp, uint32_t stride, uint32_t handle, uint32_t *id)
{
    (void)fd; ++rd_trace.addFBCount;
    rd_trace.fbWidth = width; rd_trace.fbHeight = height;
    rd_trace.fbDepth = depth; rd_trace.fbBpp = bpp;
    rd_trace.fbStride = stride; rd_trace.fbHandle = handle;
    if (rd_fault.addFBResult) return rd_fault.addFBResult;
    *id = 900 + rd_trace.addFBCount; rd_track_fb(*id);
    return 0;
}
static int rd_rm_fb(int fd, uint32_t id)
{
    unsigned i;
    (void)fd; assert(rd_trace.rmCount < 128);
    rd_trace.removedFB[rd_trace.rmCount++] = id;
    if (!rd_fault.rmFBResult)
        for (i = 0; i < 128; ++i) if (id && rd_fb_live[i] == id)
        { rd_fb_live[i] = 0; --rd_trace.outstandingFB; }
    return rd_fault.rmFBResult;
}
static int rd_set_crtc(int fd, uint32_t crtc, uint32_t fb, uint32_t x, uint32_t y,
                       uint32_t *connectors, int count, drmModeModeInfoPtr mode)
{
    (void)fd; assert(x == 0 && y == 0 && count == 1 && connectors && mode);
    ++rd_trace.setCrtcCount;
    rd_trace.crtcId = crtc; rd_trace.crtcFB = fb;
    rd_trace.crtcConnector = connectors[0];
    rd_trace.crtcWidth = mode->hdisplay; rd_trace.crtcHeight = mode->vdisplay;
    return rd_fault.setCrtcResult;
}
static int rd_set_plane(int fd, uint32_t plane, uint32_t crtc, uint32_t fb,
    uint32_t flags, int32_t x, int32_t y, uint32_t w, uint32_t h,
    uint32_t sx, uint32_t sy, uint32_t sw, uint32_t sh)
{
    RendererDRMPlaneCall *p;
    (void)fd; assert(rd_trace.planeCount < 64);
    p = &rd_trace.planes[rd_trace.planeCount++];
    p->plane = plane; p->crtc = crtc; p->fb = fb; p->flags = flags;
    p->x = x; p->y = y; p->width = w; p->height = h;
    p->sx = sx; p->sy = sy; p->sw = sw; p->sh = sh;
    return rd_fault.setPlaneResult;
}
static int rd_page_flip(int fd, uint32_t crtc, uint32_t fb, uint32_t flags, void *data)
{
    (void)fd; ++rd_trace.pageFlips;
    rd_trace.pageFlipCrtc = crtc; rd_trace.pageFlipFB = fb; rd_trace.pageFlipFlags = flags;
    if (!rd_fault.pageFlipResult) rd_event_data = data;
    return rd_fault.pageFlipResult;
}
static int rd_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    ++rd_trace.selects; rd_trace.selectNfds = nfds;
    rd_trace.selectSawDRM = readfds && FD_ISSET(nfds - 1, readfds);
    rd_trace.selectSawStdin = readfds && FD_ISSET(0, readfds);
    rd_trace.selectNoTimeout = !timeout && !writefds && !exceptfds;
    /* Never wait. Only the synthetic DRM descriptor may be marked readable. */
    if (readfds)
    {
        FD_ZERO(readfds);
        if (rd_fault.selectResult > 0 && rd_fault.selectReady) FD_SET(nfds - 1, readfds);
    }
    if (rd_fault.selectResult < 0) errno = EINTR;
    return rd_fault.selectResult;
}
static int rd_handle_event(int fd, drmEventContextPtr event)
{
    ++rd_trace.events; rd_trace.eventVersion = event->version;
    assert(event->page_flip_handler && rd_event_data);
    event->page_flip_handler(fd, 88, 7, 1234, rd_event_data);
    rd_event_data = NULL;
    return 0;
}
static int rd_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    (void)fd;
    if (RendererInitMocksActive() && request == DRM_IOCTL_SET_CLIENT_CAP)
    {
        int result;
        va_start(ap, request);
        result = ri_client_cap(fd, va_arg(ap, struct drm_set_client_cap *));
        va_end(ap);
        return result;
    }
    if (request != DRM_IOCTL_GEM_CLOSE)
    {
        ++rd_trace.unexpectedIoctl; errno = ENOTTY; return -1;
    }
    va_start(ap, request);
    {
        /* Match the actual variadic argument type (&close in production).
         * Ellipsis does not convert T* to const T* as a typed parameter does. */
        const struct drm_gem_close *arg = va_arg(ap, struct drm_gem_close *);
        assert(arg && rd_trace.ioctlCount < 64);
        rd_trace.closedHandles[rd_trace.ioctlCount++] = arg->handle;
    }
    va_end(ap);
    return rd_fault.ioctlResult;
}
static struct gbm_bo *rd_lock_front(struct gbm_surface *surface)
{
    unsigned i;
    assert(surface); ++rd_trace.locks;
    if (rd_fault.failLock) return NULL;
    for (i = 0; i < 64; ++i) if (!rd_bos[i].busy)
    {
        rd_bos[i].id = i + 1; rd_bos[i].busy = true; ++rd_trace.outstandingBO;
        return (struct gbm_bo *)&rd_bos[i];
    }
    abort();
}
static unsigned rd_bo_id(struct gbm_bo *bo)
{
    unsigned i;
    if (!bo) return 0;
    for (i = 0; i < 64; ++i) if (bo == (struct gbm_bo *)&rd_bos[i]) return rd_bos[i].id;
    abort(); /* Catch any synthetic object accidentally crossing fixture types. */
}
static void rd_release_buffer(struct gbm_surface *surface, struct gbm_bo *bo)
{
    unsigned id = rd_bo_id(bo);
    (void)surface; assert(id && rd_bos[id - 1].busy && rd_trace.releases < 64);
    rd_trace.releasedBO[rd_trace.releases++] = id;
    rd_bos[id - 1].busy = false; --rd_trace.outstandingBO;
}
static union gbm_bo_handle rd_bo_handle(struct gbm_bo *bo)
{
    union gbm_bo_handle handle;
    memset(&handle, 0, sizeof(handle)); handle.u32 = 500 + rd_bo_id(bo);
    return handle;
}
static uint32_t rd_bo_stride(struct gbm_bo *bo) { assert(rd_bo_id(bo)); return 4096; }
static void rd_free_plane(drmModePlanePtr p) { ++rd_trace.planeFrees; free(p->formats); free(p); }
static void rd_free_crtc(drmModeCrtcPtr p) { ++rd_trace.crtcFrees; free(p); }
static void rd_free_encoder(drmModeEncoderPtr p) { ++rd_trace.encoderFrees; free(p); }
static void rd_device_destroy(struct gbm_device *p) { ++rd_trace.deviceDestroys; free(p); }

static WstOverlayPlane *rd_new_plane(unsigned id)
{
    int i;
    WstOverlayPlane *p = calloc(1, sizeof(*p)); assert(p);
    p->plane = calloc(1, sizeof(*p->plane)); assert(p->plane);
    p->plane->plane_id = id; p->crtc_id = RD_CRTC_ID;
    p->plane->crtc_id = RD_CRTC_ID; p->zOrder = id == RD_VIDEO_ID ? 7 : 3;
    /* Teardown owns a realistic format/modifier tree, including an empty
     * modifier list and a multi-modifier list whose storage is freed once. */
    p->formatCount = 2;
    p->formats = calloc(2, sizeof(*p->formats)); assert(p->formats);
    p->formats[0].format = DRM_FORMAT_ARGB8888;
    p->formats[0].modifierCount = 2;
    p->formats[0].modifiers = calloc(2, sizeof(uint64_t)); assert(p->formats[0].modifiers);
    p->formats[0].modifiers[1] = 1;
    p->formats[1].format = DRM_FORMAT_NV12;
    for (i = 0; i < ACTIVE_FRAMES; ++i)
    {
        p->videoFrame[i].bufferId = -1;
        p->videoFrame[i].fd0 = p->videoFrame[i].fd1 = p->videoFrame[i].fd2 = -1;
    }
    return p;
}

void RendererDRMResetMocks(void)
{
    assert(!rd_created && !gCtx);
    /* Do not erase evidence of leaked ownership from the previous fixture. */
    assert(!rd_trace.liveObjects && !rd_trace.liveProperties &&
           !rd_trace.liveRequests && !rd_trace.liveBlobs &&
           !rd_trace.outstandingBO && !rd_trace.outstandingFB);
    memset(&rd_trace, 0, sizeof(rd_trace));
    memset(&rd_fault, 0, sizeof(rd_fault));
    memset(rd_bos, 0, sizeof(rd_bos)); memset(rd_fb_live, 0, sizeof(rd_fb_live));
    memset(&rd_connection, 0, sizeof(rd_connection)); memset(&rd_manager, 0, sizeof(rd_manager));
    rd_graphics = rd_video = NULL;
    rd_event_data = NULL;
    rd_fault.selectResult = 1; rd_fault.selectReady = true;
}

bool RendererDRMCreate(bool window, bool video)
{
    NativeWindowItem *nw;
    assert(!rd_created);
    RendererReset();
    if (!RendererCreateContext(1920, 1080)) return false;
    rd_created = true;
    RendererAddMode(1920, 1080, 60, false);
    gCtx->conn = l1_connector(gCtx->drmFd, RD_CONNECTOR_ID);
    gCtx->enc = calloc(1, sizeof(*gCtx->enc)); assert(gCtx->enc);
    gCtx->crtc = calloc(1, sizeof(*gCtx->crtc)); assert(gCtx->crtc);
    gCtx->gbm = malloc(1); assert(gCtx->gbm);
    gCtx->enc->crtc_id = gCtx->crtc->crtc_id = RD_CRTC_ID;
    gCtx->modeCurrent.vrefresh = 60; gCtx->modeNext = gCtx->modeCurrent;
    gCtx->outputEnable = gCtx->graphicsEnable = gCtx->videoEnable = true;
    gCtx->haveAtomic = true; gCtx->useZPos = true;
    pthread_mutex_init(&gCtx->offloadMsgQ.mutex, NULL);
    sem_init(&gCtx->offloadMsgQ.sem, 0, 0);
    pthread_mutex_init(&rd_connection.mutex, NULL);
    rd_connection.socketFd = -1;
    rd_graphics = rd_new_plane(RD_GRAPHICS_ID);
    rd_video = rd_new_plane(RD_VIDEO_ID);
    rd_video->conn = video ? &rd_connection : NULL;
    rd_video->vfm = &rd_manager;
    rd_connection.videoPlane = rd_video;
    gCtx->overlayPlanes.totalCount = 2;
    gCtx->overlayPlanes.primary = rd_graphics;
    /* Build valid used/available lists with production allocation helpers. */
    wstOverlayAppendUnused(&gCtx->overlayPlanes, rd_graphics);
    wstOverlayAppendUnused(&gCtx->overlayPlanes, rd_video);
    rd_graphics->supportsGraphics = true; rd_video->supportsVideo = true;
    if (window)
    {
        nw = calloc(1, sizeof(*nw)); assert(nw);
        nw->nativeWindow = l1_surface_create(gCtx->gbm, 1280, 720, 0, 0);
        nw->width = 1280; nw->height = 720; nw->dirty = true;
        nw->windowPlane = wstOverlayAllocPrimary(&gCtx->overlayPlanes);
        assert(nw->windowPlane == rd_graphics);
        #ifdef USE_REFRESH_LOCK
        pthread_mutex_init(&nw->mutexRefresh, NULL); pthread_cond_init(&nw->condRefresh, NULL);
        #endif
        gCtx->nwFirst = gCtx->nwLast = nw;
    }
    if (video)
    {
        /* The graphics-selection policy takes the tail, which is our video
         * node even when the primary has not been assigned to a window. */
        WstOverlayPlane *p = wstOverlayAlloc(&gCtx->overlayPlanes, true, false);
        assert(p == rd_video);
        rd_video->dirty = rd_video->readyToFlip = true;
    }
    return true;
}

RendererDRMFaults *RendererDRMFailures(void) { return &rd_fault; }
const RendererDRMTrace *RendererDRMCalls(void) { return &rd_trace; }
static WstOverlayPlane *rd_plane(enum RendererDRMObject object)
{ return object == RD_VIDEO ? rd_video : rd_graphics; }
bool RendererDRMAcquire(enum RendererDRMObject object)
{
    if (object == RD_CONNECTOR) return wstAcquireConnectorProperties(gCtx);
    if (object == RD_CRTC) return wstAcquireCrtcProperties(gCtx);
    return wstAcquirePlaneProperties(gCtx, rd_plane(object));
}
void RendererDRMRelease(enum RendererDRMObject object)
{
    if (object == RD_CONNECTOR) wstReleaseConnectorProperties(gCtx);
    else if (object == RD_CRTC) wstReleaseCrtcProperties(gCtx);
    else wstReleasePlaneProperties(gCtx, rd_plane(object));
}
bool RendererDRMPropertiesPresent(enum RendererDRMObject object)
{
    if (object == RD_CONNECTOR) return gCtx->connectorProps && gCtx->connectorPropRes;
    if (object == RD_CRTC) return gCtx->crtcProps && gCtx->crtcPropRes;
    return rd_plane(object)->planeProps && rd_plane(object)->planePropRes;
}
unsigned RendererDRMPropertyId(enum RendererDRMObject object, const char *name)
{
    unsigned i;
    for (i = 1; i <= 12; ++i) if (!strcmp(rd_name(object * 100 + i), name)) return object * 100 + i;
    return 0;
}
void RendererDRMAddProperty(enum RendererDRMObject object, const char *name, uint64_t value)
{
    drmModeAtomicReqPtr req = rd_atomic_alloc();
    drmModeObjectPropertiesPtr p;
    drmModePropertyRes **r;
    unsigned id;
    assert(req);
    if (object == RD_CONNECTOR) { p = gCtx->connectorProps; r = gCtx->connectorPropRes; id = RD_CONNECTOR_ID; }
    else if (object == RD_CRTC) { p = gCtx->crtcProps; r = gCtx->crtcPropRes; id = RD_CRTC_ID; }
    else { p = rd_plane(object)->planeProps; r = rd_plane(object)->planePropRes; id = rd_plane(object)->plane->plane_id; }
    assert(p && r);
    wstAtomicAddProperty(gCtx, req, id, p->count_props, r, name, value);
    rd_atomic_free(req);
}
RendererDRMOptions RendererDRMGetOptions(void)
{
    RendererDRMOptions o = {0};
    o.atomic = gCtx->haveAtomic; o.modeSet = gCtx->modeSet;
    o.output = gCtx->outputEnable; o.graphics = gCtx->graphicsEnable; o.video = gCtx->videoEnable;
    o.actualSize = gGraphicsActualSize; o.zpos = gCtx->useZPos;
    o.modePending = gCtx->modeSetPending;
    o.videoDirty = rd_video->dirty; o.ready = rd_video->readyToFlip; o.inUse = rd_video->inUse;
    o.hide = rd_video->hide; o.nextHide = rd_video->videoFrame[FRAME_NEXT].hide;
    o.nextHidden = rd_video->videoFrame[FRAME_NEXT].hidden;
    if (gCtx->nwFirst)
    {
        o.windowDirty = gCtx->nwFirst->dirty;
        o.windowWidth = gCtx->nwFirst->width; o.windowHeight = gCtx->nwFirst->height;
    }
    return o;
}
void RendererDRMConfigure(RendererDRMOptions o)
{
    gCtx->haveAtomic = o.atomic; gCtx->modeSet = o.modeSet;
    gCtx->outputEnable = o.output; gCtx->graphicsEnable = o.graphics; gCtx->videoEnable = o.video;
    gGraphicsActualSize = o.actualSize; gCtx->useZPos = o.zpos;
    gCtx->modeSetPending = o.modePending;
    rd_video->dirty = o.videoDirty; rd_video->readyToFlip = o.ready; rd_video->inUse = o.inUse;
    rd_video->hide = o.hide; rd_video->videoFrame[FRAME_NEXT].hide = o.nextHide;
    rd_video->videoFrame[FRAME_NEXT].hidden = o.nextHidden;
    if (gCtx->nwFirst)
    {
        assert(o.windowWidth > 0 && o.windowHeight > 0);
        gCtx->nwFirst->dirty = o.windowDirty;
        gCtx->nwFirst->width = o.windowWidth; gCtx->nwFirst->height = o.windowHeight;
    }
}
static RendererDRMFrame rd_frame_view(const VideoFrame *f)
{
    RendererDRMFrame v = {0};
    v.fb = f->fbId; v.handle0 = f->handle0; v.handle1 = f->handle1;
    v.id = f->bufferId; v.fd0 = f->fd0; v.fd1 = f->fd1; v.fd2 = f->fd2;
    v.visibleWidth = f->frameWidthVisible; v.visibleHeight = f->frameHeightVisible;
    v.x = f->rectX; v.y = f->rectY; v.width = f->rectW; v.height = f->rectH;
    v.hide = f->hide; v.hidden = f->hidden;
    return v;
}
void RendererDRMSetFrame(enum RendererDRMSlot slot, RendererDRMFrame v)
{
    VideoFrame *f = &rd_video->videoFrame[slot];
    memset(f, 0, sizeof(*f));
    f->fbId = v.fb; f->handle0 = v.handle0; f->handle1 = v.handle1;
    f->bufferId = v.id; f->fd0 = v.fd0; f->fd1 = v.fd1; f->fd2 = v.fd2;
    f->frameWidthVisible = f->frameWidth = v.visibleWidth;
    f->frameHeightVisible = f->frameHeight = v.visibleHeight;
    f->rectX = v.x; f->rectY = v.y; f->rectW = v.width; f->rectH = v.height;
    f->hide = v.hide; f->hidden = v.hidden;
    rd_track_fb(v.fb);
}
RendererDRMState RendererDRMGetState(void)
{
    RendererDRMState s = {0}; int i;
    s.modeSet = gCtx->modeSet; s.modePending = gCtx->modeSetPending;
    s.notifySize = gCtx->notifySizeChange; s.defaultRate = gCtx->defaultRate;
    s.flipPending = gCtx->flipPending;
    s.queuedReleases = (gCtx->offloadMsgQ.writeIdx - gCtx->offloadMsgQ.readIdx + OFFLOAD_QUEUE_CAPACITY) % OFFLOAD_QUEUE_CAPACITY;
    s.videoDirty = rd_video->dirty; s.ready = rd_video->readyToFlip;
    s.nextHide = rd_video->videoFrame[FRAME_NEXT].hide; s.nextHidden = rd_video->videoFrame[FRAME_NEXT].hidden;
    for (i = 0; i < 4; ++i) s.frames[i] = rd_frame_view(&rd_video->videoFrame[i]);
    if (gCtx->nwFirst)
    {
        s.windowDirty = gCtx->nwFirst->dirty;
        s.fb = gCtx->nwFirst->fbId; s.previousFB = gCtx->nwFirst->prevFbId;
        s.bo = rd_bo_id(gCtx->nwFirst->bo); s.previousBO = rd_bo_id(gCtx->nwFirst->prevBo);
    }
    return s;
}
void RendererDRMSwap(bool directAtomic)
{
    pthread_mutex_lock(&gMutex);
    if (directAtomic) wstSwapDRMBuffersAtomic(gCtx);
    else wstSwapDRMBuffers(gCtx);
    pthread_mutex_unlock(&gMutex);
}
void RendererDRMReleasePrevious(void)
{
    /* Enable enqueueing only, never start/join a worker on synthetic state. */
    gCtx->offloadThreadStarted = true;
    wstReleasePreviousBuffers(gCtx);
    gCtx->offloadThreadStarted = false;
}
void RendererDRMDrainReleases(void)
{
    WstOffloadMsgQ *q = &gCtx->offloadMsgQ;
    while (q->readIdx != q->writeIdx)
    {
        WstOffloadMsg *m = &q->msg[q->readIdx];
        assert(m->msgType == WST_OLM_BUFF_RELEASE && rd_trace.releasedFrames < 64);
        rd_trace.releasedFrameIds[rd_trace.releasedFrames++] = m->param_int;
        wstOffloadMsgExecute(m->msgType, m->param_pvoid, m->param_long_long, m->param_int, m->param_pvoid2);
        memset(m, 0, sizeof(*m));
        q->readIdx = (q->readIdx + 1) % OFFLOAD_QUEUE_CAPACITY;
    }
}
void RendererDRMPageEvent(int pending, unsigned events)
{
    gCtx->flipPending = pending;
    while (events--) pageFlipEventHandler(gCtx->drmFd, 1, 2, 3, gCtx);
}
void RendererDRMDetachConnector(void)
{
    gCtx->conn = NULL;
    gCtx->res = l1_resources(gCtx->drmFd);
    RendererSetConnectorFailure(true);
}
void RendererDRMDetachWindowPlane(void)
{
    if (gCtx->nwFirst) gCtx->nwFirst->windowPlane = NULL;
}
void RendererDRMDestroyWindow(void)
{
    if (gCtx->nwFirst) WstGLDestroyNativeWindow(gCtx, gCtx->nwFirst->nativeWindow);
}
void RendererDRMDestroy(void)
{
    unsigned i;
    if (!rd_created) return;
    /* Faults describe the operation under test, not fixture reclamation. */
    rd_fault.rmFBResult = rd_fault.blobDestroyResult = rd_fault.ioctlResult = 0;
    RendererDRMReleasePrevious(); RendererDRMDrainReleases();
    for (i = 0; i < ACTIVE_FRAMES; ++i)
    {
        VideoFrame *f = &rd_video->videoFrame[i];
        if (rd_has_fb(f->fbId))
        {
            WstOffloadVideoFrameResources r = {0};
            r.fbId = f->fbId; r.handle0 = f->handle0; r.handle1 = f->handle1;
            r.fd0 = f->fd0; r.fd1 = f->fd1; r.fd2 = f->fd2;
            wstOffloadFreeVideoFrameResources(&r);
        }
        /* Already-failed legacy frames retain stale metadata, but their
         * resources were released by production and must not be freed twice. */
        memset(f, 0, sizeof(*f));
        f->fd0 = f->fd1 = f->fd2 = f->bufferId = -1;
    }
    /* Production DestroyNativeWindow only releases prevBo, not current bo/fb.
     * Reclaim these fixture-owned mock resources explicitly; tests inspect the
     * unmodified post-call state BEFORE teardown, including failure-path leaks.
     * Also reclaim legacy AddFB-failure BOs not retained by production. */
    for (i = 0; i < 64; ++i) if (rd_bos[i].busy)
        rd_release_buffer(NULL, (struct gbm_bo *)&rd_bos[i]);
    for (i = 0; i < 128; ++i) if (rd_fb_live[i]) rd_rm_fb(gCtx->drmFd, rd_fb_live[i]);
    if (rd_trace.liveBlobs) rd_destroy_blob(gCtx->drmFd, RD_BLOB_ID);
    /* Window destruction returns its graphics plane. Return any remaining
     * used planes (including a detached window plane) before context cleanup:
     * production wstTermCtx does not clear usedHead after freeing that list,
     * so a mixed used/available pool would revisit freed nodes. Resources
     * and post-operation assertions have already been handled above. */
    RendererDRMDestroyWindow();
    while (gCtx->overlayPlanes.usedHead)
        wstOverlayFree(&gCtx->overlayPlanes, gCtx->overlayPlanes.usedHead);
    pthread_mutex_destroy(&gCtx->offloadMsgQ.mutex);
    sem_destroy(&gCtx->offloadMsgQ.sem);
    pthread_mutex_destroy(&rd_connection.mutex);
    RendererCleanup();
    rd_graphics = rd_video = NULL; rd_created = false; rd_event_data = NULL;
    memset(&rd_fault, 0, sizeof(rd_fault));
    rd_fault.selectResult = 1; rd_fault.selectReady = true;
}