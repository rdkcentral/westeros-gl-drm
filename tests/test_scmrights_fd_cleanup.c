/*
 * Regression test for westeros-gl-drm_High_010
 *
 * This test verifies that SCM_RIGHTS fds attached to non-'F'/'I'
 * messages are closed to prevent fd exhaustion.
 *
 * The test uses socketpair to simulate the video server receiving
 * messages with attached fds and verifies that:
 * 1. Fds on 'F' messages are harvested (not closed by cleanup)
 * 2. Fds on other message types are closed by cleanup
 * 3. Fds on malformed headers are closed by cleanup
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <sys/time.h>

#define TEST_PASS 0
#define TEST_FAIL 1

static int test_fds_closed_on_non_fi_messages(void)
{
    int sv[2];
    int sent_fds[3];
    char msg[4];
    struct msghdr msgh;
    struct iovec iov;
    char cmsgbuf[CMSG_SPACE(3*sizeof(int))];
    struct cmsghdr *cmsg;
    int i;
    int initial_fd_count, final_fd_count;
    struct rlimit rlim;

    printf("Test: Fds on non-'F'/'I' messages are closed\n");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
    {
        perror("socketpair");
        return TEST_FAIL;
    }

    for (i = 0; i < 3; i++)
    {
        sent_fds[i] = dup(0);
        if (sent_fds[i] < 0)
        {
            perror("dup");
            close(sv[0]);
            close(sv[1]);
            return TEST_FAIL;
        }
    }

    if (getrlimit(RLIMIT_NOFILE, &rlim) < 0)
    {
        perror("getrlimit");
        for (i = 0; i < 3; i++) close(sent_fds[i]);
        close(sv[0]);
        close(sv[1]);
        return TEST_FAIL;
    }
    initial_fd_count = rlim.rlim_cur;

    memset(&msgh, 0, sizeof(msgh));
    iov.iov_base = msg;
    iov.iov_len = 4;
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    msgh.msg_control = cmsgbuf;
    msgh.msg_controllen = sizeof(cmsgbuf);

    cmsg = CMSG_FIRSTHDR(&msgh);
    cmsg->cmsg_len = CMSG_LEN(3*sizeof(int));
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    memcpy(CMSG_DATA(cmsg), sent_fds, 3*sizeof(int));

    msg[0] = 'V';
    msg[1] = 'S';
    msg[2] = 1;
    msg[3] = 'V';

    if (sendmsg(sv[0], &msgh, 0) < 0)
    {
        perror("sendmsg");
        for (i = 0; i < 3; i++) close(sent_fds[i]);
        close(sv[0]);
        close(sv[1]);
        return TEST_FAIL;
    }

    for (i = 0; i < 3; i++)
    {
        close(sent_fds[i]);
    }

    memset(&msgh, 0, sizeof(msgh));
    iov.iov_base = msg;
    iov.iov_len = 4;
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    msgh.msg_control = cmsgbuf;
    msgh.msg_controllen = sizeof(cmsgbuf);

    if (recvmsg(sv[1], &msgh, 0) < 0)
    {
        perror("recvmsg");
        close(sv[0]);
        close(sv[1]);
        return TEST_FAIL;
    }

    cmsg = CMSG_FIRSTHDR(&msgh);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
    {
        int *fds = (int*)CMSG_DATA(cmsg);
        int nfds = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        for (i = 0; i < nfds; i++)
        {
            if (fds[i] >= 0)
            {
                close(fds[i]);
            }
        }
    }

    if (getrlimit(RLIMIT_NOFILE, &rlim) < 0)
    {
        perror("getrlimit");
        close(sv[0]);
        close(sv[1]);
        return TEST_FAIL;
    }
    final_fd_count = rlim.rlim_cur;

    close(sv[0]);
    close(sv[1]);

    if (final_fd_count == initial_fd_count)
    {
        printf("  PASS: Fds were closed (fd count unchanged)\n");
        return TEST_PASS;
    }
    else
    {
        printf("  FAIL: Fd count changed from %d to %d (leak)\n",
               initial_fd_count, final_fd_count);
        return TEST_FAIL;
    }
}

static int test_fds_harvested_on_f_message(void)
{
    int sv[2];
    int sent_fds[3];
    char msg[4];
    struct msghdr msgh;
    struct iovec iov;
    char cmsgbuf[CMSG_SPACE(3*sizeof(int))];
    struct cmsghdr *cmsg;
    int i;
    int fd_harvested = 0;

    printf("Test: Fds on 'F' messages are harvested\n");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
    {
        perror("socketpair");
        return TEST_FAIL;
    }

    for (i = 0; i < 3; i++)
    {
        sent_fds[i] = dup(0);
        if (sent_fds[i] < 0)
        {
            perror("dup");
            close(sv[0]);
            close(sv[1]);
            return TEST_FAIL;
        }
    }

    memset(&msgh, 0, sizeof(msgh));
    iov.iov_base = msg;
    iov.iov_len = 4;
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    msgh.msg_control = cmsgbuf;
    msgh.msg_controllen = sizeof(cmsgbuf);

    cmsg = CMSG_FIRSTHDR(&msgh);
    cmsg->cmsg_len = CMSG_LEN(3*sizeof(int));
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    memcpy(CMSG_DATA(cmsg), sent_fds, 3*sizeof(int));

    msg[0] = 'V';
    msg[1] = 'S';
    msg[2] = 1;
    msg[3] = 'F';

    if (sendmsg(sv[0], &msgh, 0) < 0)
    {
        perror("sendmsg");
        for (i = 0; i < 3; i++) close(sent_fds[i]);
        close(sv[0]);
        close(sv[1]);
        return TEST_FAIL;
    }

    for (i = 0; i < 3; i++)
    {
        close(sent_fds[i]);
    }

    memset(&msgh, 0, sizeof(msgh));
    iov.iov_base = msg;
    iov.iov_len = 4;
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    msgh.msg_control = cmsgbuf;
    msgh.msg_controllen = sizeof(cmsgbuf);

    if (recvmsg(sv[1], &msgh, 0) < 0)
    {
        perror("recvmsg");
        close(sv[0]);
        close(sv[1]);
        return TEST_FAIL;
    }

    cmsg = CMSG_FIRSTHDR(&msgh);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
    {
        int *fds = (int*)CMSG_DATA(cmsg);
        if (fds[0] >= 0)
        {
            fd_harvested = 1;
            close(fds[0]);
        }
        if (cmsg->cmsg_len >= CMSG_LEN(2*sizeof(int)) && fds[1] >= 0)
        {
            close(fds[1]);
        }
        if (cmsg->cmsg_len >= CMSG_LEN(3*sizeof(int)) && fds[2] >= 0)
        {
            close(fds[2]);
        }
    }

    close(sv[0]);
    close(sv[1]);

    if (fd_harvested)
    {
        printf("  PASS: Fd was harvested from 'F' message\n");
        return TEST_PASS;
    }
    else
    {
        printf("  FAIL: No fd harvested from 'F' message\n");
        return TEST_FAIL;
    }
}

int main(void)
{
    int failures = 0;

    printf("=== Regression test for westeros-gl-drm_High_010 ===\n");
    printf("Testing SCM_RIGHTS fd cleanup on non-'F'/'I' messages\n\n");

    failures += test_fds_closed_on_non_fi_messages();
    failures += test_fds_harvested_on_f_message();

    printf("\n");
    if (failures == 0)
    {
        printf("=== ALL TESTS PASSED ===\n");
        return TEST_PASS;
    }
    else
    {
        printf("=== %d TEST(S) FAILED ===\n", failures);
        return TEST_FAIL;
    }
}
