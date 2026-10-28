#ifndef WESTEROS_RENDERER_FRAME_SUPPORT_H
#define WESTEROS_RENDERER_FRAME_SUPPORT_H

#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Integration: include this header BEFORE production source in the harness and
 * map sendmsg to RendererFrameSendmsg around that inclusion. Include the .inc
 * at the end of the harness. No other external mocks are required. */
struct msghdr;
ssize_t RendererFrameSendmsg(int fd, const struct msghdr *message, int flags);

typedef struct RendererFrameFixture RendererFrameFixture;
typedef struct RendererOverlayFixture RendererOverlayFixture;
typedef struct {
    int id, number, fd0, fd1, fd2;
    long long time;
    bool canExpire, advanced;
    int x, y, width, height;
    unsigned visibleWidth, visibleHeight;
} RendererFrameView;
typedef struct {
    int size, capacity, currentId, syncSession, drops;
    float rate;
    bool paused, resetBaseTime, advance, underflow, underflowReported;
    long long flipBase, frameBase, flipCurrent, frameCurrent, adjust;
    bool dirty, ready, forceDirty;
} RendererFrameState;
typedef struct {
    int count, usedCount, available[8], used[8];
    int availableCount, usedListCount;
    bool linksValid;
} RendererOverlayLists;
typedef struct {
    bool inUse, hide, hidden, keepLast, hasConnection;
    int frameCount, resourceId, buffers[4];
    bool kept[4], hasPlane[4];
    long long frameBase, flipBase;
} RendererOverlayState;

RendererFrameFixture *RendererFramesCreate(bool immediate);
void RendererFramesDestroy(RendererFrameFixture *fixture);
void RendererFramesPush(RendererFrameFixture *fixture, int id, long long time);
bool RendererFramesPop(RendererFrameFixture *fixture, long long blank,
                       long long interval, RendererFrameView *out);
RendererFrameView RendererFramesAt(RendererFrameFixture *fixture, int index);
RendererFrameView RendererFramesCurrent(RendererFrameFixture *fixture);
RendererFrameState RendererFramesState(RendererFrameFixture *fixture);
void RendererFramesPause(RendererFrameFixture *fixture, bool paused);
void RendererFramesAdvance(RendererFrameFixture *fixture);
void RendererFramesEos(RendererFrameFixture *fixture);
void RendererFramesSyncType(RendererFrameFixture *fixture, int type);
void RendererFramesRect(RendererFrameFixture *fixture, int x, int y, int w, int h);
void RendererFramesTiming(RendererFrameFixture *fixture, long long flipBase,
                          long long frameBase, long long expireLimit, float rate);
void RendererFramesDisplayed(RendererFrameFixture *fixture, int id, long long time);
void RendererFramesUnderflowReported(RendererFrameFixture *fixture, bool reported);
void RendererFrameReleaseReset(void);
int RendererFrameReleaseCount(void);
int RendererFrameReleasedId(int index);
bool RendererFrameReleaseWireValid(void);

RendererOverlayFixture *RendererOverlaysCreate(void);
void RendererOverlaysDestroy(RendererOverlayFixture *fixture);
int RendererOverlaysAdd(RendererOverlayFixture *fixture, int z, bool video,
                       bool matching, bool primary);
int RendererOverlaysAlloc(RendererOverlayFixture *fixture, bool graphics, bool primaryVideo);
int RendererOverlaysPrimary(RendererOverlayFixture *fixture);
void RendererOverlaysFree(RendererOverlayFixture *fixture, int index);
void RendererOverlaysSeed(RendererOverlayFixture *fixture, int index, bool keep);
RendererOverlayLists RendererOverlaysLists(RendererOverlayFixture *fixture);
RendererOverlayState RendererOverlaysState(RendererOverlayFixture *fixture, int index);

void RendererFrameModeType(int index, bool driver, bool preferred);
void RendererFrameSelectMode(int width, int height, bool atomic);
void RendererFrameSelectRate(int numerator, int denominator);
void RendererFrameCurrentRate(int rate);
int RendererFrameCurrentModeWidth(void);
int RendererFrameCurrentModeHeight(void);
int RendererFrameCurrentModeRate(void);

#ifdef __cplusplus
}
#endif
#endif