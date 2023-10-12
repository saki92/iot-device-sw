#include "timer.h"
#include <signal.h>
#include <string.h>
#include <time.h>

void adjust_timer(int sec, int interval, timer_t *timerid) {
  struct itimerspec its;

  its.it_value.tv_sec = sec;
  its.it_value.tv_nsec = 0;
  its.it_interval.tv_sec = interval;
  its.it_interval.tv_nsec = 0;
  timer_settime(*timerid, 0, &its, NULL);
}

void start_timer(int sec, int interval, void (*handler)(union sigval),
                 timer_t *timerid, int userint) {
  struct sigevent sev;
  struct itimerspec its;

  memset(&sev, 0, sizeof(sev));
  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = handler;
  sev.sigev_value.sival_int = userint;
  sev.sigev_notify_attributes = NULL;
  timer_create(CLOCK_REALTIME, &sev, timerid);

  its.it_value.tv_sec = sec;
  its.it_value.tv_nsec = 0;
  its.it_interval.tv_sec = interval;
  its.it_interval.tv_nsec = 0;
  timer_settime(*timerid, 0, &its, NULL);
}
