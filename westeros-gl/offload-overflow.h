#ifndef OFFLOAD_OVERFLOW_H
#define OFFLOAD_OVERFLOW_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*WstOffloadDiscard)( uint32_t, int, void *, void * );

static inline bool wstOffloadHandleOverflow( int fullness, int capacity, uint32_t type, int param_int, void *param_pv, void *param_pv2, WstOffloadDiscard discard )
{
   if ( (fullness >= 0) && (capacity > 1) && (fullness < capacity-1) )
   {
      return false;
   }
   discard( type, param_int, param_pv, param_pv2 );
   return true;
}

#endif
