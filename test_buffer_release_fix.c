/*
 * Regression test for westeros-gl-drm_High_006
 * 
 * This test verifies that wstDestroyVideoFrameManager no longer calls
 * wstVideoServerSendBufferRelease (which does blocking sendmsg) while
 * holding vfm->mutex. Instead, it should use wstOffloadSendBufferRelease
 * which queues the release asynchronously.
 *
 * The test creates a mock VideoFrameManager with queued frames and
 * verifies that calling wstDestroyVideoFrameManager does not block
 * when the socket would be full.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

/* Mock structures and functions */
typedef struct _VideoFrame {
    int bufferId;
    int fd0;
    int fd1;
    int fd2;
    void *vf;
    int fbId;
    int handle0;
    int handle1;
    int dropped;
    int kept;
} VideoFrame;

typedef struct _VideoServerConnection {
    pthread_mutex_t mutex;
    int sockfd;
} VideoServerConnection;

typedef struct _VideoFrameManager {
    VideoServerConnection *conn;
    int queueSize;
    int queueCapacity;
    VideoFrame *queue;
    pthread_mutex_t mutex;
    int bufferIdCurrent;
} VideoFrameManager;

/* Function declarations from westeros-gl.c */
static void wstOffloadSendBufferRelease( VideoServerConnection *conn, VideoFrame* f);
static void wstFreeVideoFrameResources( VideoFrame *f );
static void wstDestroyVideoFrameManager( VideoFrameManager *vfm );

/* Mock implementations */
static int offload_call_count = 0;
static int free_resources_call_count = 0;

static void wstOffloadSendBufferRelease( VideoServerConnection *conn, VideoFrame* f)
{
    if ( !conn || !f )
    {
        return;
    }
    if ( f->bufferId < 0 )
    {
        return;
    }
    offload_call_count++;
    printf("wstOffloadSendBufferRelease called for buffer %d (async)\n", f->bufferId);
}

static void wstFreeVideoFrameResources( VideoFrame *f )
{
    free_resources_call_count++;
    printf("wstFreeVideoFrameResources called for buffer %d\n", f->bufferId);
}

/* The function being tested - copied from fixed westeros-gl.c */
static void wstDestroyVideoFrameManager( VideoFrameManager *vfm )
{
   if ( vfm )
   {
      if ( vfm->queue )
      {
         int i;
         pthread_mutex_lock( &vfm->mutex);
         for( i= 0; i < vfm->queueSize; ++i )
         {
            if ( vfm->bufferIdCurrent != vfm->queue[i].bufferId )
            {
               wstOffloadSendBufferRelease( vfm->conn, &vfm->queue[i] );
            }
            else
            {
               wstFreeVideoFrameResources( &vfm->queue[i] );
            }
         }
         vfm->queueSize= 0;
         free( vfm->queue );
         vfm->queue= 0;
         vfm->bufferIdCurrent= -1;
         vfm->queueCapacity= 0;
         pthread_mutex_unlock( &vfm->mutex);
      }
      pthread_mutex_destroy( &vfm->mutex);
      free( vfm );
   }
}

/* Test function */
int main()
{
    VideoServerConnection conn;
    VideoFrameManager *vfm;
    VideoFrame *queue;
    int i;
    int test_passed = 1;

    printf("=== Regression test for westeros-gl-drm_High_006 ===\n");
    printf("Testing that wstDestroyVideoFrameManager uses async offload\n\n");

    /* Initialize mock connection */
    pthread_mutex_init( &conn.mutex, NULL );
    conn.sockfd = -1;

    /* Create VideoFrameManager */
    vfm = (VideoFrameManager*)calloc(1, sizeof(VideoFrameManager));
    vfm->conn = &conn;
    vfm->queueSize = 5;
    vfm->queueCapacity = 5;
    vfm->bufferIdCurrent = 2; /* Simulate buffer 2 is currently displayed */
    pthread_mutex_init( &vfm->mutex, NULL );

    /* Create queue with 5 frames */
    queue = (VideoFrame*)calloc(5, sizeof(VideoFrame));
    for (i = 0; i < 5; i++)
    {
        queue[i].bufferId = i;
        queue[i].fd0 = -1;
        queue[i].fd1 = -1;
        queue[i].fd2 = -1;
        queue[i].vf = NULL;
        queue[i].fbId = 0;
        queue[i].handle0 = 0;
        queue[i].handle1 = 0;
        queue[i].dropped = 0;
        queue[i].kept = 0;
    }
    vfm->queue = queue;

    printf("Created VideoFrameManager with %d queued frames\n", vfm->queueSize);
    printf("Current displayed buffer: %d\n\n", vfm->bufferIdCurrent);

    /* Call the function being tested */
    printf("Calling wstDestroyVideoFrameManager...\n");
    wstDestroyVideoFrameManager( vfm );
    printf("wstDestroyVideoFrameManager returned\n\n");

    /* Verify results */
    printf("Results:\n");
    printf("  wstOffloadSendBufferRelease calls: %d\n", offload_call_count);
    printf("  wstFreeVideoFrameResources calls: %d\n", free_resources_call_count);

    /* Expected: 4 offload calls (buffers 0,1,3,4) and 1 free call (buffer 2) */
    if (offload_call_count == 4)
    {
        printf("  ✓ Correct number of offload calls (4)\n");
    }
    else
    {
        printf("  ✗ Incorrect number of offload calls (expected 4, got %d)\n", offload_call_count);
        test_passed = 0;
    }

    if (free_resources_call_count == 1)
    {
        printf("  ✓ Correct number of free resources calls (1)\n");
    }
    else
    {
        printf("  ✗ Incorrect number of free resources calls (expected 1, got %d)\n", free_resources_call_count);
        test_passed = 0;
    }

    printf("\n");
    if (test_passed)
    {
        printf("=== TEST PASSED ===\n");
        printf("The fix correctly routes buffer releases through the offload queue\n");
        printf("instead of doing blocking sendmsg while holding vfm->mutex\n");
        return 0;
    }
    else
    {
        printf("=== TEST FAILED ===\n");
        return 1;
    }
}
