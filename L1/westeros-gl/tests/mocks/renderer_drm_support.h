#ifndef RENDERER_DRM_SUPPORT_H
#define RENDERER_DRM_SUPPORT_H
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Public test ABI deliberately contains no private renderer/DRM types. */
enum RendererDRMObject { RD_CONNECTOR, RD_CRTC, RD_GRAPHICS, RD_VIDEO };
enum RendererDRMSlot { RD_NEXT, RD_CURRENT, RD_PREVIOUS, RD_FREE };
enum { RD_CONNECTOR_ID = 17, RD_CRTC_ID = 31,
       RD_GRAPHICS_ID = 41, RD_VIDEO_ID = 42, RD_BLOB_ID = 81 };
typedef struct {
    bool failObject, failAtomicAlloc, failLock;
    unsigned failPropertyId;
    int atomicAddResult, blobResult, blobDestroyResult, commitResult;
    int addFBResult, rmFBResult, setCrtcResult, setPlaneResult;
    int pageFlipResult, selectResult, ioctlResult;
    bool selectReady;
} RendererDRMFaults;
typedef struct {
    unsigned object, property;
    uint64_t value;
    char name[32];
} RendererDRMPropertyCall;
typedef struct {
    unsigned plane, crtc, fb, flags;
    int x, y;
    unsigned width, height, sx, sy, sw, sh;
} RendererDRMPlaneCall;
typedef struct {
    unsigned objectQueries, propertyQueries, objectFrees, propertyFrees;
    int liveObjects, liveProperties, liveRequests, liveBlobs;
    unsigned queryObject, queryType;
    unsigned atomicAllocs, atomicFrees, addCount, commitCount, commitFlags;
    RendererDRMPropertyCall adds[256];
    unsigned blobCreates, blobDestroys, blobWidth, blobHeight, blobRate;
    unsigned locks, releases, releasedBO[64], outstandingBO;
    unsigned addFBCount, fbWidth, fbHeight, fbDepth, fbBpp, fbStride, fbHandle;
    unsigned rmCount, removedFB[128], outstandingFB;
    unsigned setCrtcCount, crtcId, crtcFB, crtcConnector, crtcWidth, crtcHeight;
    unsigned planeCount;
    RendererDRMPlaneCall planes[64];
    unsigned pageFlips, pageFlipFB, pageFlipCrtc, pageFlipFlags;
    unsigned selects, events, eventVersion;
    int selectNfds;
    bool selectSawDRM, selectSawStdin, selectNoTimeout;
    unsigned ioctlCount, closedHandles[64], unexpectedIoctl;
    unsigned releasedFrames, releasedFrameIds[64];
    unsigned planeFrees, crtcFrees, encoderFrees, deviceDestroys;
} RendererDRMTrace;
typedef struct {
    bool atomic, modeSet, output, graphics, video, actualSize, zpos;
    bool windowDirty, videoDirty, ready, inUse, hide;
    bool modePending, nextHide, nextHidden;
    int windowWidth, windowHeight;
} RendererDRMOptions;
typedef struct {
    unsigned fb, handle0, handle1;
    int id, fd0, fd1, fd2;
    unsigned visibleWidth, visibleHeight;
    int x, y, width, height;
    bool hide, hidden;
} RendererDRMFrame;
typedef struct {
    bool modeSet, modePending, notifySize, windowDirty, videoDirty, ready;
    bool nextHide, nextHidden;
    unsigned fb, previousFB, bo, previousBO;
    int defaultRate, flipPending, queuedReleases;
    RendererDRMFrame frames[4];
} RendererDRMState;

/* Singleton fixture, like the shared renderer harness. Create resets mocks;
 * Destroy preserves the trace so tests can assert actual teardown operations. */
/* Common harness reset hook: requires no context or active DRM fixture. */
void RendererDRMResetMocks(void);
bool RendererDRMCreate(bool window, bool video);
void RendererDRMDestroy(void);
RendererDRMFaults *RendererDRMFailures(void);
const RendererDRMTrace *RendererDRMCalls(void);
RendererDRMOptions RendererDRMGetOptions(void);
void RendererDRMConfigure(RendererDRMOptions options);
RendererDRMState RendererDRMGetState(void);
bool RendererDRMAcquire(enum RendererDRMObject object);
void RendererDRMRelease(enum RendererDRMObject object);
bool RendererDRMPropertiesPresent(enum RendererDRMObject object);
unsigned RendererDRMPropertyId(enum RendererDRMObject object, const char *name);
void RendererDRMAddProperty(enum RendererDRMObject object, const char *name, uint64_t value);
void RendererDRMSetFrame(enum RendererDRMSlot slot, RendererDRMFrame frame);
void RendererDRMSwap(bool directAtomic);
void RendererDRMReleasePrevious(void);
void RendererDRMDrainReleases(void);
void RendererDRMPageEvent(int pending, unsigned events);
void RendererDRMDetachConnector(void);
void RendererDRMDetachWindowPlane(void);
void RendererDRMDestroyWindow(void);

#ifdef __cplusplus
}
#endif
#endif