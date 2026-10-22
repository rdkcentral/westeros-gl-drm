#ifndef DISPLAY_RESPONSE_H
#define DISPLAY_RESPONSE_H

#include <stddef.h>
#include <string.h>

static inline size_t wstPackDisplayResponse( unsigned char *message, size_t messageCapacity, const char *response, size_t responseCapacity )
{
   size_t responseLen= 0;
   size_t payloadCapacity;
   if ( !message || !response || (messageCapacity < 4) )
   {
      return 0;
   }
   payloadCapacity= messageCapacity-3;
   if ( payloadCapacity > 255 )
   {
      payloadCapacity= 255;
   }
   while ( (responseLen+1 < payloadCapacity) && (responseLen < responseCapacity) && response[responseLen] )
   {
      ++responseLen;
   }
   message[0]= 'D';
   message[1]= 'S';
   message[2]= (unsigned char)(responseLen+1);
   memcpy( message+3, response, responseLen );
   message[3+responseLen]= 0;
   return responseLen+4;
}

#endif
