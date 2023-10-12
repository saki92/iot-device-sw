#ifndef TIMER_H
#define TIMER_H
#include <signal.h>
#include <time.h>
void adjust_timer(int sec, int interval, timer_t *timerid);

void start_timer(int sec, int interval, void (*handler)(union sigval),
                 timer_t *timerid, int userint);
#endif
