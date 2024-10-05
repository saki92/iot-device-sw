#ifndef TIMER_H
#define TIMER_H
#include <signal.h>
#include <stdbool.h>
#include <time.h>

typedef struct {
  timer_t timerid;
  bool isValid;
} timer_w_t;

void adjust_timer(int sec, int interval, void (*handler)(union sigval),
                  timer_w_t *timer, int userint);

void start_timer(int sec, int interval, void (*handler)(union sigval),
                 timer_w_t *timer, int userint);

int get_timer_state(timer_w_t *timer);

void stop_timer(timer_w_t *timer);
#endif
