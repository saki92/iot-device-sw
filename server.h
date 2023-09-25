#ifndef SERVER_BASE_H
#define SERVER_BASE_H

#include "common.h"

#define MAX_DEVICES 2
#define MAX_CLIENTS 3
#define CLIENT_PASSCODE 39403

enum message_types {
  MSG_TYPE_A,
  MSG_TYPE_B,
  MSG_TYPE_C0,
  MSG_TYPE_C1,
  MSG_TYPE_C2,
  MSG_TYPE_D0,
  MSG_TYPE_D1
};

struct device_s {
  char id;
  int passcode;
  int socket;
  int rem_cut_off_time;
  int set_cut_off_time;
  int last_rssi;
  int adc_0;
  int adc_1;
  int adc_2;
  int adc_3;
  char gpio_states;
  char msg_A_buf[MSG_SIZE];
  char msg_C2_buf[MSG_SIZE];
};

#endif
