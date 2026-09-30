#include <stdio.h>
#include "../westeros-gl/offload-overflow.h"

static int discardCount;
static uint32_t discardedType;

static void discard( uint32_t type, int param_int, void *param_pv, void *param_pv2 )
{
   (void)param_int;
   (void)param_pv;
   (void)param_pv2;
   ++discardCount;
   discardedType= type;
}

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
   uint32_t type;
   int failures= 0;
   for( type= 0; type <= 5; ++type )
   {
      discardCount= 0;
      failures += check( !wstOffloadHandleOverflow( 62, 64, type, 0, 0, 0, discard ), "non-full queue discarded work" );
      failures += check( discardCount == 0, "non-full queue invoked discard" );
      failures += check( wstOffloadHandleOverflow( 63, 64, type, 0, 0, 0, discard ), "full queue accepted work" );
      failures += check( discardCount == 1, "overflow cleanup was not exactly once" );
      failures += check( discardedType == type, "overflow cleanup received wrong type" );
   }
   discardCount= 0;
   failures += check( wstOffloadHandleOverflow( -1, 64, 1, 0, 0, 0, discard ), "invalid fullness accepted" );
   failures += check( discardCount == 1, "invalid fullness was not discarded" );
   return failures ? 1 : 0;
}
