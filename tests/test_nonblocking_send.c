#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "../westeros-gl/nonblocking-send.h"

static int check( int condition, const char *message )
{
   if ( !condition )
   {
      fprintf( stderr, "%s\n", message );
      return 1;
   }
   return 0;
}

int main( void )
{
   int fd[2];
   char value= 'x';
   char received= 0;
   char fill[1024]= {0};
   struct iovec iov= { &value, 1 };
   struct msghdr msg;
   int failures= 0;

   memset( &msg, 0, sizeof(msg) );
   msg.msg_iov= &iov;
   msg.msg_iovlen= 1;
   failures += check( socketpair( AF_UNIX, SOCK_DGRAM, 0, fd ) == 0, "socketpair failed" );
   failures += check( wstSendMessageNonBlocking( fd[0], &msg, 1 ) == 1, "normal send failed" );
   failures += check( read( fd[1], &received, 1 ) == 1 && received == value, "normal delivery failed" );
   while ( send( fd[0], fill, sizeof(fill), MSG_DONTWAIT|MSG_NOSIGNAL ) > 0 ) {}
   iov.iov_base= fill;
   iov.iov_len= sizeof(fill);
   failures += check( wstSendMessageNonBlocking( fd[0], &msg, sizeof(fill) ) < 0, "backpressure was not reported" );
   failures += check( send( fd[0], &value, 1, MSG_DONTWAIT|MSG_NOSIGNAL ) < 0, "failed connection remained writable" );
   close( fd[0] );
   close( fd[1] );
   return failures ? 1 : 0;
}
