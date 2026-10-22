#include <stdio.h>
#include <string.h>
#include "../westeros-gl/display-response.h"

static int check( int condition, const char *message )
{
   if ( !condition ) { fprintf( stderr, "%s\n", message ); return 1; }
   return 0;
}

int main( void )
{
   unsigned char output[259];
   char longResponse[400];
   size_t length;
   int failures= 0;
   memset( longResponse, 'A', sizeof(longResponse) );
   length= wstPackDisplayResponse( output, sizeof(output), longResponse, sizeof(longResponse) );
   failures += check( length == 258, "long response was not bounded" );
   failures += check( output[2] == 255, "encoded length overflowed" );
   failures += check( output[257] == 0, "response was not terminated" );
   length= wstPackDisplayResponse( output, sizeof(output), "ok", 3 );
   failures += check( length == 6 && output[2] == 3 && !memcmp(output+3, "ok\0", 3), "valid response changed" );
   failures += check( wstPackDisplayResponse( output, 3, "x", 2 ) == 0, "undersized destination accepted" );
   return failures ? 1 : 0;
}
