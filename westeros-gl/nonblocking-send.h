#ifndef NONBLOCKING_SEND_H
#define NONBLOCKING_SEND_H

#include <errno.h>
#include <sys/socket.h>

static inline ssize_t wstSendMessageNonBlocking( int fd, struct msghdr *msg, size_t expected )
{
   ssize_t sent;
   do
   {
      sent= sendmsg( fd, msg, MSG_NOSIGNAL|MSG_DONTWAIT );
   }
   while ( (sent < 0) && (errno == EINTR) );
   if ( sent != (ssize_t)expected )
   {
      shutdown( fd, SHUT_RDWR );
   }
   return sent;
}

#endif
