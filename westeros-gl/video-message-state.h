#ifndef VIDEO_MESSAGE_STATE_H
#define VIDEO_MESSAGE_STATE_H

#include <stdbool.h>

typedef void (*WstVideoMessageSink)( void *context );

static inline bool wstVideoServerDispatchMessage( int id, bool hasPlane, bool hasDrmPlane, bool hasVfm, WstVideoMessageSink sink, void *context )
{
   bool valid= true;

   switch( id )
   {
      case 'F':
      case 'H':
      case 'S':
      case 'P':
      case 'I':
      case 'A':
      case 'W':
      case 'R':
      case 'K':
      case 'E':
         valid= hasPlane && hasDrmPlane && hasVfm;
         break;
      default:
         break;
   }
   if ( valid && sink )
   {
      sink( context );
   }
   return valid;
}

#endif
