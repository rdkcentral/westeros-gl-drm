/* Include LAST in the shared C harness; all source redirects must be undefined.
 * See renderer_init_support.h for the exact shared-owner integration changes.
 * These wrappers call the actual static production functions, not copies.
 */
#include "renderer_init_support.h"
#include "renderer_drm_support.h"
#include <assert.h>

#if defined(USE_UEVENT_HOTPLUG) || defined(USE_REFRESH_LOCK) || defined(USE_GENERIC_AVSYNC)
#error "Init fixture requires disabled uevent, refresh-lock and generic AV sync"
#endif

static RendererInitConfig ri_config;
static RendererInitTrace ri_trace;
static bool ri_active, ri_watch_service, ri_offload_initialized;
static WstServerCtx *ri_unpublished;
static NativeWindowItem *ri_update_window;

static void *ri_alloc(size_t count, size_t size)
{
    void *p = calloc(count, size);
    assert(p);
    return p;
}
static void *ri_calloc(size_t count, size_t size)
{
    void *p = calloc(count, size);
    if (ri_active && ri_watch_service && count == 1 && size == sizeof(WstServerCtx))
    {
        assert(!ri_unpublished);
        ri_unpublished = p;
    }
    return p;
}
bool RendererInitMocksActive(void) { return ri_active; }
RendererInitConfig *RendererInitSettings(void) { return &ri_config; }
const RendererInitTrace *RendererInitCalls(void) { return &ri_trace; }

static int ri_open(const char *path, int flags)
{
    assert(ri_active);
    if (flags & O_CREAT)
    {
        ++ri_trace.lockOpens;
        snprintf(ri_trace.lockPath, sizeof(ri_trace.lockPath), "%s", path);
        errno = EACCES;
        return ri_config.lockOpenResult;
    }
    ++ri_trace.opens;
    snprintf(ri_trace.cardPath, sizeof(ri_trace.cardPath), "%s", path);
    assert((flags & (O_RDWR | O_CLOEXEC)) == (O_RDWR | O_CLOEXEC));
    errno = ENOENT;
    return ri_config.openResult;
}
static drmModeResPtr ri_resources(int fd)
{
    drmModeResPtr p;
    unsigned i;
    (void)fd; ++ri_trace.resourceQueries;
    if (ri_config.failResources) return NULL;
    assert(ri_config.connectorCount <= RI_LIMIT && ri_config.encoderCount <= RI_LIMIT && ri_config.crtcCount <= RI_LIMIT);
    p = ri_alloc(1, sizeof(*p));
    p->count_connectors = ri_config.connectorCount;
    p->count_encoders = ri_config.encoderCount;
    p->count_crtcs = ri_config.crtcCount;
    p->connectors = ri_alloc(RI_LIMIT, sizeof(*p->connectors));
    p->encoders = ri_alloc(RI_LIMIT, sizeof(*p->encoders));
    p->crtcs = ri_alloc(RI_LIMIT, sizeof(*p->crtcs));
    for (i = 0; i < ri_config.connectorCount; ++i) p->connectors[i] = ri_config.connectors[i].id;
    for (i = 0; i < ri_config.encoderCount; ++i) p->encoders[i] = ri_config.encoders[i].id;
    for (i = 0; i < ri_config.crtcCount; ++i) p->crtcs[i] = ri_config.crtcs[i];
    ++ri_trace.liveResources;
    return p;
}
static void ri_free_resources(drmModeResPtr p)
{
    if (!p) return;
    ++ri_trace.resourceFrees; --ri_trace.liveResources;
    free(p->connectors); free(p->encoders); free(p->crtcs); free(p);
}
static drmModeConnectorPtr ri_connector(int fd, uint32_t id)
{
    unsigned i, j;
    (void)fd;
    assert(ri_trace.connectorQueries < 32);
    ri_trace.connectorIds[ri_trace.connectorQueries++] = id;
    for (i = 0; i < ri_config.connectorCount; ++i)
    {
        const RendererInitConnector *c = &ri_config.connectors[i];
        drmModeConnectorPtr p;
        if (c->id != id) continue;
        if (c->missing) return NULL;
        assert(c->modes <= RI_LIMIT);
        p = ri_alloc(1, sizeof(*p));
        p->connector_id = id; p->encoder_id = c->encoder;
        p->connector_type = DRM_MODE_CONNECTOR_HDMIA;
        p->connection = c->connected ? DRM_MODE_CONNECTED : DRM_MODE_DISCONNECTED;
        p->count_modes = c->modes;
        p->modes = ri_alloc(RI_LIMIT, sizeof(*p->modes));
        for (j = 0; j < c->modes; ++j)
        {
            p->modes[j].hdisplay = j ? 1280 : 1920;
            p->modes[j].vdisplay = j ? 720 : 1080;
            p->modes[j].vrefresh = 60;
            p->modes[j].type = DRM_MODE_TYPE_DRIVER;
            snprintf(p->modes[j].name, sizeof(p->modes[j].name), "%dx%d", p->modes[j].hdisplay, p->modes[j].vdisplay);
        }
        ++ri_trace.liveConnectors;
        return p;
    }
    return NULL;
}
static void ri_free_connector(drmModeConnectorPtr p)
{
    if (!p) return;
    ++ri_trace.connectorFrees; --ri_trace.liveConnectors;
    free(p->modes); free(p->props); free(p->prop_values); free(p->encoders); free(p);
}
static int ri_set_master(int fd)
{
    ++ri_trace.masterCalls; ri_trace.lastMasterFd = fd;
    return ri_active ? ri_config.masterResult : -1;
}
static drmVersionPtr ri_version(int fd)
{
    drmVersionPtr p;
    (void)fd; ++ri_trace.versionQueries;
    if (!ri_active || ri_config.failVersion) return NULL;
    p = ri_alloc(1, sizeof(*p));
    p->version_major = 1;
    p->name = strdup(ri_config.driver); p->date = strdup("20260101"); p->desc = strdup("L1 synthetic DRM");
    assert(p->name && p->date && p->desc);
    p->name_len = strlen(p->name); p->date_len = strlen(p->date); p->desc_len = strlen(p->desc);
    return p;
}
static void ri_free_version(drmVersionPtr p)
{
    assert(p); ++ri_trace.versionFrees;
    free(p->name); free(p->date); free(p->desc); free(p);
}
static int ri_client_cap(int fd, struct drm_set_client_cap *cap)
{
    unsigned i = ri_trace.capCount++;
    assert(ri_active && fd == RI_CARD_FD && cap && i < 8);
    ri_trace.caps[i] = cap->capability; ri_trace.capValues[i] = cap->value;
    if (cap->capability == DRM_CLIENT_CAP_UNIVERSAL_PLANES) return ri_config.universalResult;
    assert(cap->capability == DRM_CLIENT_CAP_ATOMIC);
    return ri_config.atomicResult;
}
static struct gbm_device *ri_gbm_create(int fd)
{
    ++ri_trace.gbmCreates; ri_trace.lastGBMFd = fd;
    return (!ri_active || ri_config.failGBM) ? NULL : ri_alloc(1, 1);
}
static drmModeEncoderPtr ri_encoder(int fd, uint32_t id)
{
    unsigned i;
    (void)fd;
    assert(ri_trace.encoderQueries < 32);
    ri_trace.encoderIds[ri_trace.encoderQueries++] = id;
    if (!ri_active) return NULL;
    for (i = 0; i < ri_config.encoderCount; ++i)
    {
        const RendererInitEncoder *e = &ri_config.encoders[i];
        drmModeEncoderPtr p;
        if (e->id != id) continue;
        if (e->missing) return NULL;
        p = ri_alloc(1, sizeof(*p));
        p->encoder_id = id; p->crtc_id = e->crtc; p->possible_crtcs = e->possibleCrtcs;
        return p;
    }
    return NULL;
}
static drmModeCrtcPtr ri_crtc(int fd, uint32_t id)
{
    drmModeCrtcPtr p;
    (void)fd;
    assert(ri_trace.crtcQueries < 32);
    ri_trace.crtcIds[ri_trace.crtcQueries++] = id;
    if (!ri_active || ri_config.failCrtc) return NULL;
    p = ri_alloc(1, sizeof(*p)); p->crtc_id = id;
    p->mode_valid = ri_config.validMode;
    p->mode.hdisplay = 1920; p->mode.vdisplay = 1080; p->mode.vrefresh = 60;
    return p;
}
static drmModePlaneResPtr ri_plane_resources(int fd)
{
    drmModePlaneResPtr p;
    unsigned i;
    (void)fd; ++ri_trace.planeResourceQueries;
    if (!ri_active || ri_config.failPlaneResources) return NULL;
    assert(ri_config.planeCount <= RI_LIMIT);
    p = ri_alloc(1, sizeof(*p)); p->count_planes = ri_config.planeCount;
    p->planes = ri_alloc(RI_LIMIT, sizeof(*p->planes));
    for (i = 0; i < p->count_planes; ++i) p->planes[i] = RD_GRAPHICS_ID + i;
    return p;
}
static void ri_free_plane_resources(drmModePlaneResPtr p)
{
    assert(p); ++ri_trace.planeResourceFrees; free(p->planes); free(p);
}
static drmModePlanePtr ri_plane(int fd, uint32_t id)
{
    drmModePlanePtr p;
    (void)fd; ++ri_trace.planeQueries;
    if (!ri_active || ri_config.failPlane) { errno = ENOENT; return NULL; }
    /* Only property-query FAILURE is exercised for a non-null plane. The
     * production discovery loop frees only its final property; querying all
     * 12 default rd properties would leak the first 11. Do not conceal it. */
    assert(RendererDRMFailures()->failObject);
    p = ri_alloc(1, sizeof(*p)); p->plane_id = id; p->possible_crtcs = 1;
    p->count_formats = 1; p->formats = ri_alloc(1, sizeof(*p->formats));
    p->formats[0] = DRM_FORMAT_ARGB8888;
    return p;
}
static int ri_thread_create(pthread_t *thread, const pthread_attr_t *attr,
                            void *(*start)(void *), void *arg)
{
    (void)thread; (void)attr; (void)arg;
    ++ri_trace.threads;
    if (start == wstOffloadThread)
    {
        ++ri_trace.offloadThreads;
        /* Reached only AFTER production successfully initializes the real
         * semaphore and queue mutex. Destroy these exact objects at teardown. */
        ri_offload_initialized = true;
    }
    else if (start == wstRefreshThread) ++ri_trace.refreshThreads;
    else if (start == wstVideoServerThread || start == wstDisplayServerThread) ++ri_trace.serviceThreads;
    else if (start == wstVideoServerConnectionThread || start == wstDisplayServerConnectionThread) ++ri_trace.connectionThreads;
    return EAGAIN; /* Never call start(), publish a thread id, or start a thread. */
}
static int ri_attr_init(pthread_attr_t *attr)
{
    ++ri_trace.attrInits; memset(attr, 0, sizeof(*attr));
    return ri_config.attrInitResult;
}
static int ri_attr_detach(pthread_attr_t *attr, int state)
{
    (void)attr; assert(state == PTHREAD_CREATE_DETACHED);
    ++ri_trace.attrDetaches; return ri_config.attrDetachResult;
}
static int ri_flock(int fd, int operation)
{
    assert(fd == RI_LOCK_FD && operation == (LOCK_NB | LOCK_EX));
    ++ri_trace.flocks; errno = EWOULDBLOCK; return ri_config.flockResult;
}
static int ri_socket(int domain, int type, int protocol)
{
    ++ri_trace.sockets;
    ri_trace.socketDomain = domain; ri_trace.socketType = type; ri_trace.socketProtocol = protocol;
    errno = EMFILE; return ri_active ? ri_config.socketResult : -1;
}
static int ri_bind(int fd, const struct sockaddr *address, socklen_t length)
{
    const struct sockaddr_un *a = (const struct sockaddr_un *)address;
    assert(fd == RI_SOCKET_FD && a->sun_family == AF_LOCAL);
    assert(length == offsetof(struct sockaddr_un, sun_path) + strlen(a->sun_path) + 1);
    ++ri_trace.binds;
    snprintf(ri_trace.boundPath, sizeof(ri_trace.boundPath), "%s", a->sun_path);
    errno = EADDRINUSE; return ri_config.bindResult;
}
static int ri_listen(int fd, int backlog)
{
    assert(fd == RI_SOCKET_FD); ++ri_trace.listens; ri_trace.backlog = backlog;
    errno = EINVAL; return ri_config.listenResult;
}
static int ri_shutdown(int fd, int how)
{
    ++ri_trace.shutdowns; ri_trace.shutdownFd = fd; ri_trace.shutdownHow = how;
    return 0;
}
static int ri_unlink(const char *path)
{
    assert(ri_trace.unlinks < 8);
    snprintf(ri_trace.unlinked[ri_trace.unlinks++], 160, "%s", path);
    return 0;
}

void RendererInitResetMocks(void)
{
    assert(!ri_active && !gCtx && !ri_unpublished && !ri_update_window &&
           !ri_offload_initialized);
    assert(!ri_trace.liveResources && !ri_trace.liveConnectors);
    memset(&ri_trace, 0, sizeof(ri_trace)); memset(&ri_config, 0, sizeof(ri_config));
    ri_watch_service = false;
}

void RendererInitReset(void)
{
    assert(!ri_active);
    RendererReset(); /* Includes both DRM and init traces, arrays and faults. */
    ri_config.openResult = RI_CARD_FD; ri_config.lockOpenResult = RI_LOCK_FD;
    ri_config.masterResult = -1; ri_config.atomicResult = -1;
    ri_config.socketResult = RI_SOCKET_FD; ri_config.validMode = true;
    snprintf(ri_config.driver, sizeof(ri_config.driver), "synthetic");
    ri_config.connectorCount = ri_config.encoderCount = ri_config.crtcCount = 1;
    ri_config.connectors[0] = (RendererInitConnector){RD_CONNECTOR_ID, 21, false, true, 2};
    ri_config.encoders[0] = (RendererInitEncoder){21, RD_CRTC_ID, 1, false};
    ri_config.crtcs[0] = RD_CRTC_ID;
    ri_unpublished = NULL; ri_update_window = NULL;
    ri_offload_initialized = ri_watch_service = false; ri_active = true;
}
static void ri_reclaim_unpublished(void)
{
    if (ri_unpublished)
    {
        ++ri_trace.reclaimedServices;
        wstTermServiceServer(ri_unpublished);
        ri_unpublished = NULL;
    }
}
bool RendererInitContext(bool publicEntry)
{
    WstGLCtx *ctx;
    assert(ri_active && !gCtx);
    /* Public failure would deadlock gMutex: tests use publicEntry only with
     * a known-success topology; all injected failures use the internal call. */
    if (publicEntry) assert(ri_config.openResult >= 0 && !ri_config.failResources && !ri_config.failGBM);
    ri_watch_service = true;
    ctx = publicEntry ? WstGLInit() : wstInitCtx();
    ri_watch_service = false;
    if (ctx) gCtx = ctx;
    /* Auto-created display server can fail before publishing its service. */
    if (!gDisplayServer && !gVideoServer) ri_reclaim_unpublished();
    return ctx != NULL;
}
void RendererInitDestroyContext(void)
{
    if (!gCtx) return;
    if (ri_update_window)
    {
        assert(gCtx->nwFirst == ri_update_window && !ri_update_window->nativeWindow);
        gCtx->nwFirst = gCtx->nwLast = NULL;
        free(ri_update_window); ri_update_window = NULL;
    }
    if (ri_offload_initialized)
    {
        int semResult, mutexResult;
        assert(!gCtx->offloadThreadStarted);
        semResult = sem_destroy(&gCtx->offloadMsgQ.sem);
        mutexResult = pthread_mutex_destroy(&gCtx->offloadMsgQ.mutex);
        assert(semResult == 0 && mutexResult == 0);
        (void)semResult; (void)mutexResult;
        ri_offload_initialized = false;
    }
    RendererCleanup();
}
void RendererInitEnd(void)
{
    RendererInitDestroyContext(); ri_reclaim_unpublished();
    ri_active = false;
    memset(RendererDRMFailures(), 0, sizeof(*RendererDRMFailures()));
}
RendererInitState RendererInitGetState(void)
{
    RendererInitState s = {0};
    s.maxWidth = gGraphicsMaxWidth; s.maxHeight = gGraphicsMaxHeight;
    s.fps = g_emitFPS; s.debugLevel = g_activeLevel; s.frameDebug = g_frameDebug;
    if (!gCtx) return s;
    s.present = true; s.master = gCtx->isMaster; s.atomic = gCtx->haveAtomic;
    s.planes = gCtx->usePlanes; s.videoServer = gCtx->useVideoServer;
    s.vblank = gCtx->useVBlank; s.zpos = gCtx->useZPos; s.secure = gCtx->secureGraphics;
    s.globalZoom = gCtx->globalZoomActive; s.allow4k = gCtx->allow4kZoom;
    s.output = gCtx->outputEnable; s.graphics = gCtx->graphicsEnable; s.video = gCtx->videoEnable;
    s.refreshStarted = gCtx->refreshThreadStarted; s.offloadStarted = gCtx->offloadThreadStarted;
    s.connectorProps = gCtx->connectorProps != NULL; s.crtcProps = gCtx->crtcProps != NULL;
    s.connectorId = gCtx->conn ? gCtx->conn->connector_id : 0;
    s.encoderId = gCtx->enc ? gCtx->enc->encoder_id : 0;
    s.encoderCrtcId = gCtx->enc ? gCtx->enc->crtc_id : 0;
    s.crtcId = gCtx->crtc ? gCtx->crtc->crtc_id : 0;
    s.planeCount = gCtx->overlayPlanes.totalCount;
    s.width = gCtx->modeCurrent.hdisplay; s.height = gCtx->modeCurrent.vdisplay; s.rate = gCtx->modeCurrent.vrefresh;
    s.zoomMode = gCtx->zoomMode; s.videoDebugLevel = gCtx->videoDebugLevel;
    s.modeSet = gCtx->modeSet; s.modePending = gCtx->modeSetPending;
    return s;
}
unsigned RendererInitFindEncoder(int crtcIndex, unsigned crtcId, bool replaceCurrent)
{
    drmModeResPtr r = ri_resources(RI_CARD_FD);
    drmModeEncoderPtr old = replaceCurrent ? ri_alloc(1, sizeof(*old)) : NULL;
    drmModeEncoderPtr found;
    unsigned id;
    assert(r && crtcIndex >= 0 && crtcIndex < RI_LIMIT);
    found = wstFindEncoderForCrtc(RI_CARD_FD, r, old, crtcIndex, crtcId);
    id = found ? found->encoder_id : 0;
    ri_trace.selectedEncoderCrtc = found ? found->crtc_id : 0;
    if (found) { assert(found->crtc_id == crtcId); rd_free_encoder(found); }
    ri_free_resources(r);
    return id;
}
void RendererInitPrepareUpdate(bool resourcesPresent, bool window, bool atomic, bool modeSet)
{
    assert(ri_active && !gCtx);
    if (!RendererCreateContext(640, 480)) abort();
    gCtx->drmFd = RI_CARD_FD;
    gCtx->res = resourcesPresent ? ri_resources(RI_CARD_FD) : NULL;
    gCtx->modeSet = modeSet; gCtx->haveAtomic = atomic;
    if (window)
    {
        ri_update_window = ri_alloc(1, sizeof(*ri_update_window));
        ri_update_window->width = 1280; ri_update_window->height = 720;
        gCtx->nwFirst = gCtx->nwLast = ri_update_window;
    }
}
void RendererInitUpdate(void) { assert(gCtx); wstUpdateCtx(gCtx); }
void RendererInitUpdateNull(void) { wstUpdateCtx(NULL); }

RendererInitServiceResult RendererInitService(int kind)
{
    RendererInitServiceResult r = {0};
    WstServerCtx *service = NULL;
    DisplayServerCtx *display = NULL;
    VideoServerCtx *video = NULL;
    assert(ri_active && !ri_unpublished && kind >= 0 && kind <= 2);
    ri_watch_service = true;
    if (kind == 0) r.result = wstInitServiceServer("fixture", &service);
    else if (kind == 1)
    {
        display = ri_alloc(1, sizeof(*display));
        r.result = wstInitDisplayServer(display); service = display->server;
    }
    else
    {
        video = ri_alloc(1, sizeof(*video));
        r.result = wstInitVideoServer(video); service = video->server;
    }
    ri_watch_service = false;
    r.published = service != NULL;
    assert(ri_unpublished);
    r.socketFd = ri_unpublished->socketFd; r.lockFd = ri_unpublished->lockFd;
    r.pathsCleared = !ri_unpublished->addr.sun_path[0] && !ri_unpublished->lock[0];
    r.threadStarted = ri_unpublished->threadStarted;
    if (service) ri_unpublished = NULL; /* Published: normal production teardown. */
    if (display) wstTermDisplayServer(display);
    else if (video) wstTermVideoServer(video);
    else if (service) wstTermServiceServer(service);
    ri_reclaim_unpublished();
    return r;
}
bool RendererInitServiceRefcount(void)
{
    WstServerCtx *s = NULL;
    bool retained;
    if (!wstInitServiceServer("fixture", &s) || !s) abort();
    s->refCnt = 2;
    wstTermServiceServer(s);
    retained = s->refCnt == 1 && s->socketFd == RI_SOCKET_FD &&
               ri_trace.shutdowns == 0 && ri_trace.unlinks == 1;
    wstTermServiceServer(s);
    return retained;
}
bool RendererInitConnectionFailure(bool video)
{
    if (video) return wstCreateVideoServerConnection(NULL, RI_SOCKET_FD) == NULL;
    return wstCreateDisplayServerConnection(NULL, RI_SOCKET_FD) == NULL;
}
void RendererInitDestroyConnection(bool video, bool descriptor)
{
    /* Never seed threadStarted: no retry sleeps or invalid pthread_join. */
    assert(!gCtx);
    if (video)
    {
        VideoServerConnection *c = ri_alloc(1, sizeof(*c));
        /* wstOffloadFlushConn dereferences gCtx unconditionally. Supply an
         * empty queue with REAL synchronization, not a null/zeroed semaphore. */
        if (!RendererCreateContext(640, 480)) abort();
        gCtx->drmFd = -1;
        if (pthread_mutex_init(&gCtx->offloadMsgQ.mutex, NULL) != 0) abort();
        if (sem_init(&gCtx->offloadMsgQ.sem, 0, 0) != 0) abort();
        ri_offload_initialized = true;
        pthread_mutex_init(&c->mutex, NULL); c->socketFd = descriptor ? RI_SOCKET_FD : -1;
        wstDestroyVideoServerConnection(c);
        RendererInitDestroyContext();
    }
    else
    {
        DisplayServerConnection *c = ri_alloc(1, sizeof(*c));
        pthread_mutex_init(&c->mutex, NULL); c->socketFd = descriptor ? RI_SOCKET_FD : -1;
        wstDestroyDisplayServerConnection(c);
    }
}