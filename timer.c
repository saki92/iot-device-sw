#include "timer.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

void adjust_timer(int sec, int interval, void (*handler)(union sigval),
                  timer_w_t *timer, int userint) {
  if (!timer->isValid) {
    printf("Error: timer should be running but not!\n");
    start_timer(sec, interval, handler, timer, userint);
    return;
  }
  struct itimerspec its;

  its.it_value.tv_sec = sec;
  its.it_value.tv_nsec = 0;
  its.it_interval.tv_sec = interval;
  its.it_interval.tv_nsec = 0;
  timer_settime(timer->timerid, 0, &its, NULL);
}

void start_timer(int sec, int interval, void (*handler)(union sigval),
                 timer_w_t *timer, int userint) {
  struct sigevent sev;
  struct itimerspec its;

  memset(&sev, 0, sizeof(sev));
  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = handler;
  sev.sigev_value.sival_int = userint;
  sev.sigev_notify_attributes = NULL;
  timer_create(CLOCK_REALTIME, &sev, &timer->timerid);

  its.it_value.tv_sec = sec;
  its.it_value.tv_nsec = 0;
  its.it_interval.tv_sec = interval;
  its.it_interval.tv_nsec = 0;
  timer_settime(timer->timerid, 0, &its, NULL);
  timer->isValid = true;
}

int get_timer_state(timer_w_t *timer) {
  if (!timer->isValid)
    return 0;
  // Get the current state of the timer
  struct itimerspec current_its;
  timer_gettime(timer->timerid, &current_its);
  return (current_its.it_value.tv_sec / 60);
}

void stop_timer(timer_w_t *timer) {
  if (!timer->isValid)
    return;

  timer_delete(timer->timerid);
  timer->isValid = false;
}
