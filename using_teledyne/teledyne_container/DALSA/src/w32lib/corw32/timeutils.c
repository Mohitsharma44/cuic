/****************************************************************************** 
Copyright (c) 2004 Coreco inc / 2010 DALSA Corp.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

  - Redistributions of source code must retain the above copyright notice, 
    this list of conditions and the following disclaimer. 

  - Redistributions in binary form must reproduce the above copyright notice, 
    this list of conditions and the following disclaimer in the documentation 
    and/or other materials provided with the distribution. 

  - Neither the name of the DALSA nor the names of its contributors 
    may be used to endorse or promote products derived from this software 
    without specific prior written permission. 


THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE 
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, 
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED 
OF THE POSSIBILITY OF SUCH DAMAGE.

===============================================================================

timeutils.c			 											

Description:
   Posix timing and delay compatibility layer with Win32.

Platform:
	-Generic Posix.

History:
   1.00 March 11, 2004, parhug

$Log: timeutils.c $
Revision 1.3  2006/03/27 12:28:39  PARHUG
Make sure that "Sleep" at least yields the time slice (nanosleep does not always do so).
Revision 1.2  2005/01/07 11:38:25  parhug
Added GetTickCount
Revision 1.1  2004/08/19 12:26:10  parhug
Initial revision

*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/time.h>
#include <sched.h>

#include <corposix.h>
#include <linuxobjs.h>

#if defined(USE_MONOTONIC_CLOCK)
	#define CORW32_CLOCKID	CLOCK_MONOTONIC
#else
	#define CORW32_CLOCKID	CLOCK_REALTIME
#endif

// Return the current time from the configured clock id.
// (Converted to a timeval to minimize impace of replacement of gettimeofday)...
void GetCurrentClockTime( struct timeval *tv )
{
	struct timespec ts;
	
	if (tv != NULL)
	{
		ts.tv_sec = 0;
		ts.tv_nsec = 0;
		if ( clock_gettime( CORW32_CLOCKID, &ts) == 0)
		{
			tv->tv_sec = ts.tv_sec;
			tv->tv_usec = (ts.tv_nsec / 1000);
		}	
	}
}


BOOL QueryPerformanceFrequency( LARGE_INTEGER *pLfreq)
{
	// All of the time intervals will be in microseconds.
	// Unix standard decrees that CLOCKS_PER_SECOND is 1000000 even though the
	// granularity is not the same.
	pLfreq->LowPart = 1000000;
	pLfreq->HighPart = 0;
	return TRUE;
}

BOOL QueryPerformanceCounter( LARGE_INTEGER *pLcount)
{
	struct timeval tstart;
	//struct timezone tz;
	u_int64_t bigtime;

	//????gettimeofday( &tstart, &tz);
	GetCurrentClockTime( &tstart );
	
	bigtime = (u_int64_t) (tstart.tv_sec * 1000000) + (u_int64_t)tstart.tv_usec;
	pLcount->QuadPart = bigtime;
	return TRUE;
}

// Sleep for a specified number of milliseconds.
// (Standard Unix sleep(x) is for x seconds !!!
unsigned int Sleep( int millisecs)
{
	if ( millisecs == 0)
	{
		// Yield time-slice.
		sched_yield();
		return 0;
	}
	else
	{
		struct timespec sleeptime, remainder;

		sleeptime.tv_sec = millisecs/1000;
		sleeptime.tv_nsec = (millisecs - (sleeptime.tv_sec*1000))* 1000000;
		remainder.tv_sec = 0;
		remainder.tv_nsec = 0;

		sched_yield();
		nanosleep( &sleeptime, &remainder );
		return (remainder.tv_sec*1000  + remainder.tv_nsec/1000000);
	}
}

// Return the number of milliseconds since the system was started.
unsigned long GetTickCount( void )
{
	unsigned long tick;
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	GetCurrentClockTime( &tv );
	tick = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
	return tick;
}

// Retrieves the system time, in milliseconds. The system time is the time elapsed since Windows was started.
unsigned long timeGetTime( void )
{
	// Same thing as GetTickCount
	return GetTickCount();
}

//HANDLE WINAPI CreateWaitableTimer(
//  _In_opt_ LPSECURITY_ATTRIBUTES lpTimerAttributes,
//  _In_     BOOL                  bManualReset,
//  _In_opt_ LPCTSTR               lpTimerName                // this type (LPCTSTR) is not defined for now, replaced by void *         
//);
HANDLE CreateWaitableTimer( LPSECURITY_ATTRIBUTES lpTimerAttributes, BOOL bManualReset, void * lpTimerName)
{
   // None of the passed parameters are implemented for now!!!

   PHANDLEOBJ timer_handle = INVALID_HANDLE_VALUE;
   timer_t id_timer;
   struct sigevent evp;

   evp.sigev_notify = SIGEV_NONE;   // No asynchronous notification will be delivered when the event of interest occurs.

   timer_create(CLOCK_REALTIME, &evp, &id_timer);

   CreateObjectHandle( &timer_handle, Timer);
   if(VALID_HANDLEOBJ(Timer,timer_handle))
   {
      timer_handle->eventType = (bManualReset) ? ManualReset : AutoReset; // eventType is set but not used for now
		timer_handle->refCount = 1; // We are open
		timer_handle->state = STATE_OPEN;
      timer_handle->timer = id_timer;

      return RegisterTimerHandle(timer_handle);   
   }
   else
   {
      return INVALID_HANDLE_VALUE;
   }
}

//BOOL WINAPI SetWaitableTimer(
//  _In_           HANDLE           hTimer,
//  _In_     const LARGE_INTEGER    *pDueTime,
// _In_           LONG             lPeriod,
//  _In_opt_       PTIMERAPCROUTINE pfnCompletionRoutine,     // this type (PTIMERAPCROUTINE) is not defined for now, replaced by void *
//  _In_opt_       LPVOID           lpArgToCompletionRoutine, // this type (LPVOID) is not defined for now, replaced by void *
//  _In_           BOOL             fResume
//);
BOOL SetWaitableTimer( HANDLE hTimer, const LARGE_INTEGER *pDueTime, LONG lPeriod, void * pfnCompletionRoutine, 
                       void * lpArgToCompletionRoutine, BOOL fResume)
{
   // Only hTimer and pDueTime are implemented for now

   struct itimerspec value;
   
   // absolute value is needed, because in windows positive = absolute, negative = relative
   value.it_value.tv_sec = (time_t) (abs(pDueTime->QuadPart) / 10000000);  // Windows timer ticks every 100ns, so we received the time in seconds in 
                                                                           // that timebase. We need to divide it by 10Million to get it back in 
                                                                           // a normal seconds timebase.  

   value.it_value.tv_nsec = 0;
 
   value.it_interval.tv_sec = 0;
   value.it_interval.tv_nsec = 0;   

   // By default, the timer is 'relative' in Linux if no flag is passed

   return !timer_settime(HANDLE_REF(hTimer)->timer, 0, &value, NULL);   // linux fct returns 0 if succeed, windows must return non-zero if succeed
}




