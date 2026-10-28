#ifndef RENDERER_INIT_SUPPORT_H
#define RENDERER_INIT_SUPPORT_H
#include <stdbool.h>
#include <stdint.h>

/* INTEGRATION CONTRACT (shared files intentionally not edited here)
 * 1. In renderer_test_support.c include renderer_init_redirects.h BEFORE any
 *    source redirects (it parses libc/DRM prototypes), alongside the DRM
 *    redirects. Include renderer_init_unredirects.h immediately after the
 *    original source, and renderer_init_support.inc LAST, after DRM support.
 * 2. At the start of l1_open/l1_resources/l1_connector, respectively:
 *      if (RendererInitMocksActive()) return ri_open(path, flags);
 *      if (RendererInitMocksActive()) return ri_resources(fd);
 *      if (RendererInitMocksActive()) return ri_connector(fd, id);
 *    At the start of l1_free_resources/l1_free_connector, respectively:
 *      if (RendererInitMocksActive()) { ri_free_resources(value); return; }
 *      if (RendererInitMocksActive()) { ri_free_connector(value); return; }
 *    Use the actual argument name (the original connector free uses value).
 *    Keep all pre-existing inactive behavior and main close/dlsym/surface mocks.
 * 3. In rd_ioctl, BEFORE its existing non-GEM rejection, handle capabilities:
 *      if (RendererInitMocksActive() && request == DRM_IOCTL_SET_CLIENT_CAP) {
 *          int result;
 *          va_start(ap, request);
 *          result = ri_client_cap(fd, va_arg(ap, struct drm_set_client_cap *));
 *          va_end(ap);
 *          return result;
 *      }
 *    This is NOT a second ioctl macro. All other DRM mocks remain unchanged.
 * 4. Add tests/L1/renderer_init_test.cpp to test_westeros_gl_renderer_l1.
 *    Keep existing NO_GENERIC_AVSYNC, DRM_NO_UEVENT_HOTPLUG,
 *    DRM_NO_REFRESH_LOCK, DRM_NO_OUT_FENCE and DRM_NO_NATIVE_FENCE definitions.
 *
 * All init resource/connector results are independently heap allocated. They
 * MUST use the active free hooks, not the original static-object no-op frees.
 * Encoder/CRTC/plane/GBM allocations use existing rd_* heap free contracts.
 * Reset/End bracket the hooks; these tests must run sequentially like the
 * existing singleton renderer fixtures. No production source changes needed.
 */

#ifdef __cplusplus
extern "C" {
#endif
enum { RI_CARD_FD = 211, RI_LOCK_FD = 212, RI_SOCKET_FD = 213, RI_LIMIT = 4 };
typedef struct {
    unsigned id, encoder;
    bool missing, connected;
    unsigned modes;
} RendererInitConnector;
typedef struct {
    unsigned id, crtc, possibleCrtcs;
    bool missing;
} RendererInitEncoder;
typedef struct {
    int openResult, lockOpenResult, masterResult;
    int universalResult, atomicResult, flockResult, socketResult;
    int bindResult, listenResult, attrInitResult, attrDetachResult;
    bool failResources, failGBM, failVersion, failPlaneResources, failPlane;
    bool failCrtc, validMode;
    char driver[32];
    unsigned connectorCount, encoderCount, crtcCount, planeCount;
    RendererInitConnector connectors[RI_LIMIT];
    RendererInitEncoder encoders[RI_LIMIT];
    unsigned crtcs[RI_LIMIT];
} RendererInitConfig;
typedef struct {
    unsigned opens, lockOpens, resourceQueries, resourceFrees;
    unsigned connectorQueries, connectorFrees, masterCalls, versionQueries, versionFrees;
    unsigned gbmCreates, encoderQueries, crtcQueries, planeResourceQueries;
    unsigned planeResourceFrees, planeQueries, capCount;
    unsigned encoderIds[32], connectorIds[32], crtcIds[32];
    uint64_t caps[8], capValues[8];
    unsigned threads, offloadThreads, refreshThreads, serviceThreads, connectionThreads;
    unsigned attrInits, attrDetaches, flocks, sockets, binds, listens, shutdowns, unlinks;
    unsigned reclaimedServices, selectedEncoderCrtc;
    int liveResources, liveConnectors, lastMasterFd, lastGBMFd;
    int socketDomain, socketType, socketProtocol, backlog, shutdownFd, shutdownHow;
    char cardPath[160], lockPath[160], boundPath[160], unlinked[8][160];
} RendererInitTrace;
typedef struct {
    bool present, master, atomic, planes, videoServer, vblank, zpos;
    bool secure, globalZoom, allow4k, output, graphics, video;
    bool refreshStarted, offloadStarted, connectorProps, crtcProps, fps;
    bool frameDebug, modeSet, modePending;
    unsigned connectorId, encoderId, crtcId, encoderCrtcId, planeCount;
    int width, height, rate, maxWidth, maxHeight, debugLevel, zoomMode, videoDebugLevel;
} RendererInitState;
typedef struct {
    bool result, published, pathsCleared, threadStarted;
    int socketFd, lockFd;
} RendererInitServiceResult;

/* Common harness reset hook: requires inactive init hooks and no context. */
void RendererInitResetMocks(void);
void RendererInitReset(void);
void RendererInitEnd(void);
void RendererInitDestroyContext(void);
bool RendererInitMocksActive(void);
RendererInitConfig *RendererInitSettings(void);
const RendererInitTrace *RendererInitCalls(void);
RendererInitState RendererInitGetState(void);
bool RendererInitContext(bool publicEntry);
unsigned RendererInitFindEncoder(int crtcIndex, unsigned crtcId, bool replaceCurrent);
void RendererInitPrepareUpdate(bool resources, bool window, bool atomic, bool modeSet);
void RendererInitUpdate(void);
void RendererInitUpdateNull(void);
/* kind: 0 service, 1 display, 2 video. Failed unpublished service allocations
 * are observed first, then explicitly reclaimed by the fixture (production
 * does not reclaim them). This is NOT an automatic-cleanup assertion. */
RendererInitServiceResult RendererInitService(int kind);
bool RendererInitServiceRefcount(void);
bool RendererInitConnectionFailure(bool video);
void RendererInitDestroyConnection(bool video, bool descriptor);
#ifdef __cplusplus
}
#endif
#endif