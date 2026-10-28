#ifndef RENDERER_INIT_REDIRECTS_H
#define RENDERER_INIT_REDIRECTS_H
/* Parse real prototypes before ANY harness macro substitutions. */
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/file.h>
#include <unistd.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "renderer_init_support.h"

/* Shared-mock hooks: declarations only, NOT duplicate redirects. */
static int ri_open(const char *, int);
static drmModeResPtr ri_resources(int);
static drmModeConnectorPtr ri_connector(int, uint32_t);
static void ri_free_resources(drmModeResPtr);
static void ri_free_connector(drmModeConnectorPtr);
static int ri_client_cap(int, struct drm_set_client_cap *);

static void *ri_calloc(size_t, size_t);
static int ri_set_master(int);
static drmVersionPtr ri_version(int);
static void ri_free_version(drmVersionPtr);
static drmModeEncoderPtr ri_encoder(int, uint32_t);
static drmModeCrtcPtr ri_crtc(int, uint32_t);
static drmModePlaneResPtr ri_plane_resources(int);
static void ri_free_plane_resources(drmModePlaneResPtr);
static drmModePlanePtr ri_plane(int, uint32_t);
static struct gbm_device *ri_gbm_create(int);
static int ri_thread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
static int ri_attr_init(pthread_attr_t *);
static int ri_attr_detach(pthread_attr_t *, int);
static int ri_flock(int, int);
static int ri_socket(int, int, int);
static int ri_bind(int, const struct sockaddr *, socklen_t);
static int ri_listen(int, int);
static int ri_shutdown(int, int);
static int ri_unlink(const char *);

#define calloc ri_calloc
#define drmSetMaster ri_set_master
#define drmGetVersion ri_version
#define drmFreeVersion ri_free_version
#define drmModeGetEncoder ri_encoder
#define drmModeGetCrtc ri_crtc
#define drmModeGetPlaneResources ri_plane_resources
#define drmModeFreePlaneResources ri_free_plane_resources
#define drmModeGetPlane ri_plane
#define gbm_create_device ri_gbm_create
#define pthread_create ri_thread_create
#define pthread_attr_init ri_attr_init
#define pthread_attr_setdetachstate ri_attr_detach
#define flock ri_flock
#define socket ri_socket
#define bind ri_bind
#define listen ri_listen
#define shutdown ri_shutdown
#define unlink ri_unlink
#endif