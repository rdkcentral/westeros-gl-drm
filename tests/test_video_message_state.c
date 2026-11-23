#include <stdbool.h>
#include <stdio.h>
#include "../westeros-gl/video-message-state.h"

static void countSink( void *context )
{
   int *count= context;
   ++(*count);
}

static int check( bool condition, const char *message )
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
   const char dependent[]= "FHSPIAWRKE";
   unsigned int i;
   int failures= 0;

   for( i= 0; dependent[i]; ++i )
   {
      int id= dependent[i];
      int sinkCalls= 0;
      failures += check( !wstVideoServerDispatchMessage( id, false, false, false, countSink, &sinkCalls ), "missing plane accepted" );
      failures += check( !wstVideoServerDispatchMessage( id, true, false, true, countSink, &sinkCalls ), "missing DRM plane accepted" );
      failures += check( !wstVideoServerDispatchMessage( id, true, true, false, countSink, &sinkCalls ), "missing frame manager accepted" );
      failures += check( sinkCalls == 0, "rejected message reached sink" );
      failures += check( wstVideoServerDispatchMessage( id, true, true, true, countSink, &sinkCalls ), "initialized message rejected" );
      failures += check( sinkCalls == 1, "initialized message missed sink" );
   }

   {
      int sinkCalls= 0;
      failures += check( wstVideoServerDispatchMessage( 'V', false, false, false, countSink, &sinkCalls ), "initialization message rejected" );
      failures += check( wstVideoServerDispatchMessage( 'O', false, false, false, countSink, &sinkCalls ), "independent message rejected" );
      failures += check( sinkCalls == 2, "valid independent message missed sink" );
   }
   return failures ? 1 : 0;
}
