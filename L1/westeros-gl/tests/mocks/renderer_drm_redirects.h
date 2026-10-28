#ifndef RENDERER_DRM_REDIRECTS_H
#define RENDERER_DRM_REDIRECTS_H
/* Include before westeros-gl.c, preferably before the existing redirect macros.
 * Real declarations must be parsed BEFORE macro substitution. */
#include <sys/select.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static drmModeObjectPropertiesPtr rd_object_properties(int, uint32_t, uint32_t);
static drmModePropertyPtr rd_property(int, uint32_t);
static void rd_free_object_properties(drmModeObjectPropertiesPtr);
static void rd_free_property(drmModePropertyPtr);
static drmModeAtomicReqPtr rd_atomic_alloc(void);
static void rd_atomic_free(drmModeAtomicReqPtr);
static int rd_atomic_add(drmModeAtomicReqPtr, uint32_t, uint32_t, uint64_t);
static int rd_atomic_commit(int, drmModeAtomicReqPtr, uint32_t, void *);
static int rd_create_blob(int, const void *, size_t, uint32_t *);
static int rd_destroy_blob(int, uint32_t);
static int rd_add_fb(int, uint32_t, uint32_t, uint8_t, uint8_t, uint32_t, uint32_t, uint32_t *);
static int rd_rm_fb(int, uint32_t);
static int rd_set_crtc(int, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *, int, drmModeModeInfoPtr);
static int rd_set_plane(int, uint32_t, uint32_t, uint32_t, uint32_t, int32_t, int32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static int rd_page_flip(int, uint32_t, uint32_t, uint32_t, void *);
static int rd_handle_event(int, drmEventContextPtr);
static int rd_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
static int rd_ioctl(int, unsigned long, ...);
static struct gbm_bo *rd_lock_front(struct gbm_surface *);
static void rd_release_buffer(struct gbm_surface *, struct gbm_bo *);
static union gbm_bo_handle rd_bo_handle(struct gbm_bo *);
static uint32_t rd_bo_stride(struct gbm_bo *);
static void rd_free_plane(drmModePlanePtr);
static void rd_free_crtc(drmModeCrtcPtr);
static void rd_free_encoder(drmModeEncoderPtr);
static void rd_device_destroy(struct gbm_device *);

#define drmModeObjectGetProperties rd_object_properties
#define drmModeGetProperty rd_property
#define drmModeFreeObjectProperties rd_free_object_properties
#define drmModeFreeProperty rd_free_property
#define drmModeAtomicAlloc rd_atomic_alloc
#define drmModeAtomicFree rd_atomic_free
#define drmModeAtomicAddProperty rd_atomic_add
#define drmModeAtomicCommit rd_atomic_commit
#define drmModeCreatePropertyBlob rd_create_blob
#define drmModeDestroyPropertyBlob rd_destroy_blob
#define drmModeAddFB rd_add_fb
#define drmModeRmFB rd_rm_fb
#define drmModeSetCrtc rd_set_crtc
#define drmModeSetPlane rd_set_plane
#define drmModePageFlip rd_page_flip
#define drmHandleEvent rd_handle_event
#define select rd_select
#define ioctl rd_ioctl
#define gbm_surface_lock_front_buffer rd_lock_front
#define gbm_surface_release_buffer rd_release_buffer
#define gbm_bo_get_handle rd_bo_handle
#define gbm_bo_get_stride rd_bo_stride
#define drmModeFreePlane rd_free_plane
#define drmModeFreeCrtc rd_free_crtc
#define drmModeFreeEncoder rd_free_encoder
#define gbm_device_destroy rd_device_destroy
#endif