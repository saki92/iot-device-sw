#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <gpiod.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "aes/aes.h"
#include "common.h"
#include "server.h"
#include "spi_device/spi.h"
#include "timer.h"

#define CONSUMER "Motor controller"
#define SERVER_IP "192.168.193.106"
#define DEVICE_ID 1

#define STARTER_BUTTON_TIMER 200
#define MSG_A_PERIOD_S 10
#define GPIO_CHIP_0 "gpiochip0"
#define GPIO_CHIP_1 "gpiochip1"
#define GPIO_CHIP_2 "gpiochip2"
#define VAL0_PIN 15
#define VAL1_PIN 18
#define NC_PIN 0
#define NO_PIN 1
#define MOTOR_STATE_PIN 16
#define MAX_CONN_ERR 3

int client_socket = -1;
timer_w_t motor_cutoff_timer;
timer_w_t msg_A_timer;
int conn_err_cnt = MAX_CONN_ERR;

struct gpiod_chip *chip;
struct gpiod_line *valve0;
struct gpiod_line *valve1;
struct gpiod_line *nc;
struct gpiod_line *no;
struct gpiod_line *motor_state;

enum pin_dir { INPUT, OUTPUT };

int get_rssi(void) { return -33; }

int make_connection();

int assign_pin(struct gpiod_chip *chip, struct gpiod_line **line, int pin,
               enum pin_dir dir) {
  if (!chip) {
    return -1;
  }

  *line = gpiod_chip_get_line(chip, pin);
  if (!(*line)) {
    return -1;
  }

  int ret = 0;
  if (dir == OUTPUT)
    ret = gpiod_line_request_output(*line, CONSUMER, 0);
  else
    ret = gpiod_line_request_input(*line, CONSUMER);

  return ret;
}

int gpio_init() {
  char *chipname = GPIO_CHIP_2;
  chip = gpiod_chip_open_by_name(chipname);
  if (!chip) {
    printf("Open chip failed\n");
    return -1;
  }

  int ret = 0;
  ret = assign_pin(chip, &valve0, VAL0_PIN, OUTPUT);
  ret |= assign_pin(chip, &valve1, VAL1_PIN, OUTPUT);
  ret |= assign_pin(chip, &nc, NC_PIN, OUTPUT);
  ret |= assign_pin(chip, &no, NO_PIN, OUTPUT);
  ret |= assign_pin(chip, &motor_state, MOTOR_STATE_PIN, INPUT);

  return ret;
}

uint8_t gen_GPIO_state_byte(void) {
  int val0_state = gpiod_line_get_value(valve0);
  int val1_state = gpiod_line_get_value(valve1);
  int nc_state = gpiod_line_get_value(nc);
  int no_state = gpiod_line_get_value(no);
  int motor_state_val = gpiod_line_get_value(motor_state);

  uint8_t byte = ((motor_state_val << 4) | (val1_state << 3) |
                  (val0_state << 2) | (nc_state << 1) | no_state) &
                 0xFF;

  return byte;
}

void gen_msg_A(uint8_t buffer[MSG_SIZE]) {
  buffer[0] = MSG_TYPE_A;
  buffer[1] = PASSCODE_LO;
  buffer[2] = PASSCODE_HI;
  buffer[3] = DEVICE_ID;
  buffer[4] = get_rssi();

  uint16_t adc0 = get_raw_voltage(0);
  uint16_t adc1 = get_raw_voltage(1);
  uint16_t adc2 = get_raw_voltage(2);
  uint16_t adc3 = get_raw_voltage(3);

  buffer[5] = adc0 & 0xFF;       // 8 bits
  buffer[6] = (adc0 >> 8) & 0x3; // MSB 2 bits

  buffer[7] = adc1 & 0xFF;       // 8 bits
  buffer[8] = (adc1 >> 8) & 0x3; // MSB 2 bits

  buffer[9] = adc2 & 0xFF;        // 8 bits
  buffer[10] = (adc2 >> 8) & 0x3; // MSB 2 bits

  buffer[11] = adc3 & 0xFF;       // 8 bits
  buffer[12] = (adc3 >> 8) & 0x3; // MSB 2 bits

  int remTime = get_timer_state(&motor_cutoff_timer);
  buffer[13] = remTime & 0xFF;
  buffer[14] = (remTime >> 8) & 0xFF;

  buffer[15] = gen_GPIO_state_byte();
}

void send_msg_A(union sigval sv) {
  uint8_t message[MSG_SIZE];
  memset(message, 0, sizeof(message));
  gen_msg_A(message);
  int sent_bytes = send(client_socket, message, sizeof(message), 0);
  if (sent_bytes != MSG_SIZE) {
    printf("Error. Sent %d out of %d bytes\n", sent_bytes, MSG_SIZE);
    conn_err_cnt++;
    if (conn_err_cnt >= MAX_CONN_ERR) {
      close(client_socket);
      client_socket = make_connection();
      conn_err_cnt = 0;
    }
  } else {
    conn_err_cnt = 0;
  }
}

void start_motor() {
  printf("Starting motor\n");
  gpiod_line_set_value(no, 1);
  usleep(STARTER_BUTTON_TIMER * 1000);
  gpiod_line_set_value(no, 0);
}

void stop_motor() {
  printf("Stopping motor\n");
  gpiod_line_set_value(nc, 1);
  usleep(STARTER_BUTTON_TIMER * 1000);
  gpiod_line_set_value(nc, 0);
}

void stop_motor_t(union sigval sv) {
  stop_motor();
  stop_timer(&motor_cutoff_timer);
}

void handle_msg_B(uint8_t buffer[MSG_SIZE]) {
  if (buffer[0] != MSG_TYPE_B)
    return;

  if ((buffer[1] != PASSCODE_LO) || (buffer[2] != PASSCODE_HI))
    return;

  if (buffer[3] != DEVICE_ID)
    return;

  printf("Received MSG B\n");
  uint16_t remTime = ((buffer[5] << 8) & 0xFF00) | (buffer[4] & 0xFF);
  uint8_t recMotorState = buffer[6] & 0x1;
  uint8_t val0State = (buffer[6] >> 1) & 0x1;
  uint8_t val1State = (buffer[6] >> 2) & 0x1;

  // motor_state HI=OFF; LOW=ON
  uint8_t curMotorState = gpiod_line_get_value(motor_state);
  gpiod_line_set_value(valve0, val0State);
  gpiod_line_set_value(valve1, val1State);

  int remTimeSec = remTime * 60;

  if (recMotorState) {
    if ((remTime > 0) && (curMotorState)) {
      start_motor();
      start_timer(remTimeSec, 0, stop_motor_t, &motor_cutoff_timer, -1);
    } else if ((remTime > 0) && (!curMotorState)) { // adjust timer
      adjust_timer(remTimeSec, 0, stop_motor_t, &motor_cutoff_timer, -1);
    }
  } else {
    if (!curMotorState) {
      stop_motor();
      stop_timer(&motor_cutoff_timer);
    }
  }
}

void receive_data(void) {

  while (1) {
    // Receive data from the server
    int8_t buffer[AES_MSG_SIZE];
    int rec_bytes = read(client_socket, buffer, sizeof(buffer));

    // Close socket if server terminates connection
    printf("got %d bytes\n", rec_bytes);
    if (rec_bytes < 1) {
      usleep(10 * 1000 * 1000);
      continue;
    }

    // Decrypt recieved message
    uint8_t iv[AES_IV_LENGTH_BYTE];
    memcpy(iv, buffer, AES_IV_LENGTH_BYTE);
    uint8_t key[AES_KEY_LENGTH_BYTE] = AES_KEY;
    uint8_t decData[MSG_SIZE] = {0};
    decryptAES(buffer + AES_IV_LENGTH_BYTE, MSG_SIZE + MSG_SIZE, key, iv,
               decData);

    // Process message
    handle_msg_B(decData);
  }
}

int make_connection() {
  int soc;
  struct sockaddr_in server_address;

  // Create a socket
  soc = socket(AF_INET, SOCK_STREAM, 0);
  if (soc == -1) {
    perror("Error creating socket");
  }

  // Set up the server address structure
  server_address.sin_family = AF_INET;
  server_address.sin_port =
      htons(SERVER_PORT); // Change this port number as needed
  server_address.sin_addr.s_addr =
      inet_addr(SERVER_IP); // Replace with the server's IP address or domain

  // Connect to the server
  if (connect(soc, (struct sockaddr *)&server_address,
              sizeof(server_address)) == -1) {
    perror("Error connecting to server");
  } else {
    printf("connection with server etablished on soc %d\n", soc);
  }
  return soc;
}

int main() {
  msg_A_timer.isValid = false;
  motor_cutoff_timer.isValid = false;
  // Initialize GPIO
  if (gpio_init() < 0) {
    printf("Error in gpio init\n");
    return -1;
  }

  // Initialize SPI
  if (spi_init() < 0) {
    printf("Error initializing SPI0\n");
    return -1;
  }

  // Send MSG A periodically
  start_timer(MSG_A_PERIOD_S, MSG_A_PERIOD_S, send_msg_A, &msg_A_timer, -1);

  receive_data();

  // Close the socket
  close(client_socket);

  return 0;
}
