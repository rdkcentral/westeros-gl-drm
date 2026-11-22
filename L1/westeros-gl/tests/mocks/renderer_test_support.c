/* Test-only C translation unit. Including the original source preserves its
 * static helpers and gcov source attribution; no production code is copied.
 * Real library headers/linkage cover unexercised paths. All external graphics
 * operations reached by this suite are replaced below, before source inclusion.
 */
#include "renderer_test_support.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "renderer_frame_support.h"
#include "renderer_protocol_support.h"
#include "renderer_drm_support.h"
#include "renderer_init_redirects.h"
#include "renderer_drm_redirects.h"

static int l1_open(const char *path, int flags, ...);
static int l1_close(int fd);
static void *l1_dlsym(void *handle, const char *name);
static drmModeResPtr l1_resources(int fd);
static drmModeConnectorPtr l1_connector(int fd, uint32_t id);
static void l1_free_connector(drmModeConnectorPtr connector);
static void l1_free_resources(drmModeResPtr resources);
static struct gbm_surface *l1_surface_create(struct gbm_device *device,
    uint32_t width, uint32_t height, uint32_t format, uint32_t flags);
static void l1_surface_destroy(struct gbm_surface *surface);

#define open l1_open
#define close l1_close
#define dlsym l1_dlsym
#define drmModeGetResources l1_resources
#define drmModeGetConnector l1_connector
#define drmModeFreeConnector l1_free_connector
#define drmModeFreeResources l1_free_resources
#define gbm_surface_create l1_surface_create
#define gbm_surface_destroy l1_surface_destroy
#define sendmsg RendererProtocolSendmsg
#include "westeros-gl.c"
#undef sendmsg
#include "renderer_init_unredirects.h"
#include "renderer_drm_unredirects.h"
#undef open
#undef close
#undef dlsym
#undef drmModeGetResources
#undef drmModeGetConnector
#undef drmModeFreeConnector
#undef drmModeFreeResources
#undef gbm_surface_create
#undef gbm_surface_destroy

static RendererMockCalls calls;
static drmModeRes resources;
static drmModeConnector connector;
static drmModeModeInfo modes[16];
static uint32_t connectorId = 17;
static bool resourceFailure, connectorFailure;
static const char *missingSymbol;

static int l1_open(const char *path, int flags, ...)
{
    if (RendererInitMocksActive()) return ri_open(path, flags);
    (void)flags;
    ++calls.openCalls;
    snprintf(calls.cardPath, sizeof(calls.cardPath), "%s", path);
    errno = ENOENT;
    return -1; /* Never open a host DRM device, even on a developer machine. */
}

static int l1_close(int fd)
{
    (void)fd;
    ++calls.closeCalls;
    return 0; /* Synthetic descriptors must never close host descriptors. */
}

static EGLDisplay l1_egl_display(EGLNativeDisplayType display)
{
    (void)display;
    return EGL_NO_DISPLAY;
}
static EGLContext l1_egl_context(EGLDisplay display, EGLConfig config,
    EGLContext shared, const EGLint *attributes)
{
    (void)display; (void)config; (void)shared; (void)attributes;
    return EGL_NO_CONTEXT;
}
static EGLSurface l1_egl_window(EGLDisplay display, EGLConfig config,
    EGLNativeWindowType window, const EGLint *attributes)
{
    (void)display; (void)config; (void)window; (void)attributes;
    return EGL_NO_SURFACE;
}
static EGLBoolean l1_egl_swap(EGLDisplay display, EGLSurface surface)
{
    (void)display; (void)surface;
    return EGL_FALSE;
}
static void *l1_dlsym(void *handle, const char *name)
{
    (void)handle;
    ++calls.symbolQueries;
    if (missingSymbol && !strcmp(missingSymbol, name)) return NULL;
    if (!strcmp(name, "eglGetDisplay")) return (void *)l1_egl_display;
    if (!strcmp(name, "eglCreateContext")) return (void *)l1_egl_context;
    if (!strcmp(name, "eglCreateWindowSurface")) return (void *)l1_egl_window;
    if (!strcmp(name, "eglSwapBuffers")) return (void *)l1_egl_swap;
    return NULL;
}

static drmModeResPtr l1_resources(int fd)
{
    if (RendererInitMocksActive()) return ri_resources(fd);
    (void)fd;
    ++calls.resourceQueries;
    return resourceFailure ? NULL : &resources;
}
static drmModeConnectorPtr l1_connector(int fd, uint32_t id)
{
    if (RendererInitMocksActive()) return ri_connector(fd, id);
    (void)fd; (void)id;
    ++calls.connectorQueries;
    return connectorFailure ? NULL : &connector;
}
static void l1_free_connector(drmModeConnectorPtr value)
{
    if (RendererInitMocksActive()) { ri_free_connector(value); return; }
    (void)value;
    ++calls.connectorFrees;
}
static void l1_free_resources(drmModeResPtr value)
{
    if (RendererInitMocksActive()) { ri_free_resources(value); return; }
    (void)value;
}

static struct gbm_surface *l1_surface_create(struct gbm_device *device,
    uint32_t width, uint32_t height, uint32_t format, uint32_t flags)
{
    (void)device;
    ++calls.surfaceCreates;
    calls.surfaceWidth = width;
    calls.surfaceHeight = height;
    calls.surfaceFormat = format;
    calls.surfaceFlags = flags;
    /* Opaque allocation: never passed to real GBM operations by these tests. */
    return (struct gbm_surface *)malloc(1);
}
static void l1_surface_destroy(struct gbm_surface *surface)
{
    ++calls.surfaceDestroys;
    free(surface);
}

void RendererCleanup(void)
{
    if (gCtx)
    {
        while (gCtx->nwFirst)
            WstGLDestroyNativeWindow(gCtx, gCtx->nwFirst->nativeWindow);
        /* Release the fixture's synthetic context through production cleanup. */
        gCtx->refCnt = 1;
        WstGLTerm(gCtx);
    }
}

void RendererReset(void)
{
    RendererCleanup();
    /* Reset shared seams only after the preceding owner has released them.
     * Teardown traces stay observable until this next fixture boundary. */
    RendererDRMResetMocks();
    RendererInitResetMocks();
    RendererFrameReleaseReset();
    RendererProtocolCaptureReset();
    memset(&calls, 0, sizeof(calls));
    memset(&resources, 0, sizeof(resources));
    memset(&connector, 0, sizeof(connector));
    memset(modes, 0, sizeof(modes));
    resources.count_connectors = 1;
    resources.connectors = &connectorId;
    connector.connector_id = connectorId;
    connector.connector_type = DRM_MODE_CONNECTOR_HDMIA;
    connector.connection = DRM_MODE_CONNECTED;
    connector.modes = modes;
    resourceFailure = connectorFailure = false;
    missingSymbol = NULL;
    gRealEGLGetDisplay = NULL;
    gRealEGLCreateContext = NULL;
    gRealEGLCreateWindowSurface = NULL;
    gRealEGLSwapBuffers = NULL;
    gGraphicsMaxWidth = gGraphicsMaxHeight = 0;
    gGraphicsActualSize = false;
    g_activeLevel = 0;
    g_frameDebug = g_emitFPS = false;
}

WstGLCtx *RendererCreateContext(int width, int height)
{
    if (gCtx) return NULL;
    gCtx = (WstGLCtx *)calloc(1, sizeof(WstGLCtx));
    if (gCtx)
    {
        pthread_mutex_init(&gCtx->mutex, NULL);
        gCtx->refCnt = 1;
        gCtx->drmFd = 123; /* Mock-only handle. */
        gCtx->modeSet = true;
        RendererSetCurrentSize(width, height);
        gCtx->modeInfo = &gCtx->modeCurrent;
    }
    return gCtx;
}
bool RendererHasContext(void) { return gCtx != NULL; }
int RendererReferenceCount(void) { return gCtx ? gCtx->refCnt : 0; }
void RendererSetDeviceAvailable(bool available) { gCtx->drmFd = available ? 123 : -1; }
void RendererSetResourceFailure(bool fail) { resourceFailure = fail; }
void RendererSetConnectorFailure(bool fail) { connectorFailure = fail; }
void RendererSetConnectorConnected(bool connected)
{
    connector.connection = connected ? DRM_MODE_CONNECTED : DRM_MODE_DISCONNECTED;
}
void RendererAddMode(int width, int height, int rate, bool interlaced)
{
    if (connector.count_modes >= 16) abort();
    drmModeModeInfo *mode = &modes[connector.count_modes++];
    mode->hdisplay = width;
    mode->vdisplay = height;
    mode->vrefresh = rate;
    mode->flags = interlaced ? DRM_MODE_FLAG_INTERLACE : 0;
    snprintf(mode->name, sizeof(mode->name), "%dx%d", width, height);
}
RendererModeState RendererGetModeState(void)
{
    RendererModeState state = {
        gCtx->modeNext.hdisplay, gCtx->modeNext.vdisplay, gCtx->modeNext.vrefresh,
        !!(gCtx->modeNext.flags & DRM_MODE_FLAG_INTERLACE),
        gCtx->modeSetPending, gCtx->modeSetSizeChange, gCtx->usingSetDisplayMode
    };
    return state;
}
void RendererSetGraphicsLimit(int width, int height)
{
    gGraphicsMaxWidth = width;
    gGraphicsMaxHeight = height;
}
void RendererSetCurrentSize(int width, int height)
{
    gCtx->modeCurrent.hdisplay = width;
    gCtx->modeCurrent.vdisplay = height;
}
void RendererNotifySize(void) { wstGLNotifySizeListeners(); }
void RendererSetExplicitMode(bool selected) { gCtx->usingSetDisplayMode = selected; }
bool RendererModeIsSet(void) { return gCtx->modeSet; }
unsigned RendererWindowCount(void)
{
    unsigned count = 0;
    for (NativeWindowItem *window = gCtx->nwFirst; window; window = window->next) ++count;
    return count;
}
void RendererSetMissingSymbol(const char *name) { missingSymbol = name; }
WstGLCtx *RendererInitDevice(void)
{
    /* Call the internal initializer directly: its error path acquires gMutex.
     * Public WstGLInit holds that same mutex already (a separate production
     * deadlock risk); this suite must not hide it or hang on that path.
     */
    return wstInitCtx();
}
const RendererMockCalls *RendererCalls(void) { return &calls; }
int RendererPutU32(unsigned char *data, unsigned value) { return wstPutU32(data, value); }
unsigned RendererGetU32(unsigned char *data) { return wstGetU32(data); }
int RendererPutS64(unsigned char *data, long long value) { return wstPutS64(data, value); }
long long RendererGetS64(unsigned char *data) { return wstGetS64(data); }
RendererRect RendererSetVideoRect(int x, int y, int width, int height)
{
    VideoFrame frame = {0};
    RendererRect result = {0};
    frame.frameWidth = 1920;
    frame.frameHeight = 1080;
    wstSetVideoFrameRect(&frame, x, y, width, height, &result.skipX, &result.skipY);
    result.x = frame.rectX;
    result.y = frame.rectY;
    result.width = frame.rectW;
    result.height = frame.rectH;
    result.visibleWidth = frame.frameWidthVisible;
    result.visibleHeight = frame.frameHeightVisible;
    return result;
}

/* These wrappers share the original C translation unit, not copied logic. */
#include "renderer_frame_support.inc"
#include "renderer_protocol_support.inc"
#include "renderer_drm_support.inc"
#include "renderer_init_support.inc"