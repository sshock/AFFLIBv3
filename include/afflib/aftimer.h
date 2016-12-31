/*
 * Distributed under the Berkeley 4-part license.
 * Simson L. Garfinkel, March 12, 2012
 */


#ifndef __AFTIMER_H__
#define __AFTIMER_H__

#ifdef __cplusplus
#ifndef WIN32
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>
#include <sys/time.h>
#endif
#include <sys/types.h>
#include <stdio.h>
#include <string>

class aftimer {
    struct timeval t0;
    bool running;
    long total_sec;
    long total_usec;
    double lap_time_;			// time from when we last did a "stop"
public:
    aftimer():t0(),running(false),total_sec(0),total_usec(0),lap_time_(0){}

    void start();			// start the timer
    void stop();			// stop the timer

    time_t tstart() { return t0.tv_sec;} // time we started
    double elapsed_seconds();		 // how long timer has been running, total
    double lap_time();			 // how long the timer is running this time
    double eta(double fraction_done);	// calculate ETA in seconds, given fraction
    std::string hms(long t);	// turn a number of seconds into h:m:s
    std::string elapsed_text();		/* how long we have been running */
    std::string eta_text(double fraction_done); // h:m:s
    std::string eta_time(double fraction_done); // the actual time
};

/* This code in part from
 * http://social.msdn.microsoft.com/Forums/en/vcgeneral/thread/430449b3-f6dd-4e18-84de-eebd26a8d668
 */

#ifdef WIN32
#  include <winsock2.h>
#  include <windows.h>
#  ifndef DELTA_EPOCH_IN_MICROSECS
#    if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
#      define DELTA_EPOCH_IN_MICROSECS  11644473600000000Ui64
#    else
#      define DELTA_EPOCH_IN_MICROSECS  11644473600000000ULL
#    endif
#  endif
#endif

inline void timestamp(struct timeval *t)
{
#ifdef WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned __int64 tmpres = 0;
    tmpres |= ft.dwHighDateTime;
    tmpres <<= 32;
    tmpres |= ft.dwLowDateTime;

    /*converting file time to unix epoch*/
    tmpres -= DELTA_EPOCH_IN_MICROSECS;
    tmpres /= 10;  /*convert into microseconds*/
    t->tv_sec = (long)(tmpres / 1000000UL);
    t->tv_usec = (long)(tmpres % 1000000UL);
#else
    gettimeofday(t,NULL);
#endif
}

inline void aftimer::start()
{
    timestamp(&t0);
    running = 1;
}

inline void aftimer::stop(){
    if(running){
	struct timeval t;
	timestamp(&t);
	total_sec  += t.tv_sec - t0.tv_sec;
	total_usec += t.tv_usec - t0.tv_usec;
	lap_time_   = (double)(t.tv_sec - t0.tv_sec)  + (double)(t.tv_usec - t0.tv_usec)/1000000.0;
	running = false;
    }
}

inline double aftimer::lap_time()
{
    return lap_time_;
}

inline double aftimer::elapsed_seconds()
{
    double ret = (double)total_sec + (double)total_usec/1000000.0;
    if(running){
	struct timeval t;
	timestamp(&t);
	ret += t.tv_sec - t0.tv_sec;
	ret += (t.tv_usec - t0.tv_usec) / 1000000.0;
    }
    return ret;
}

inline std::string aftimer::hms(long t)
{
    char   buf[64];
    int    days = t / (60*60*24);

    t = t % (60*60*24);			/* what's left */

    int    h = t / 3600;
    int    m = (t / 60) % 60;
    int    s = t % 60;
    buf[0] = 0;
    switch(days){
    case 0:
	snprintf(buf,sizeof(buf),"%2d:%02d:%02d",h,m,s);
	break;
    case 1:
	snprintf(buf,sizeof(buf),"%d day, %2d:%02d:%02d",days,h,m,s);
	break;
    default:
	snprintf(buf,sizeof(buf),"%d days %2d:%02d:%02d",days,h,m,s);
    }
    return std::string(buf);
}

inline std::string aftimer::elapsed_text()
{
    return hms((int)elapsed_seconds());
}

/**
 * returns the number of seconds until the job is complete.
 */
inline double aftimer::eta(double fraction_done)
{
    double t = elapsed_seconds();
    if(t<=0) return -1;			// can't figure it out
    if(fraction_done<=0) return -1;	// can't figure it out
    return (t * 1.0/fraction_done - t);
}

/**
 * Retuns the number of hours:minutes:seconds until the job is done.
 */
inline std::string aftimer::eta_text(double fraction_done)
{
    double e = eta(fraction_done);
    if(e<0) return std::string("n/a");		// can't figure it out
    return hms((long)e);
}

/**
 * Returns the time when data is due.
 */
inline std::string aftimer::eta_time(double fraction_done)
{
    time_t when = time_t(eta(fraction_done)) + time(0);
    struct tm tm;
#ifdef HAVE_LOCALTIME_R
    localtime_r(&when,&tm);
#else
    tm = *localtime(&when);
#endif

    char buf[64];
    snprintf(buf,sizeof(buf),"%02d:%02d:%02d",tm.tm_hour,tm.tm_min,tm.tm_sec);
    return std::string(buf);
}

#endif

#endif
