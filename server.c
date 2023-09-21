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

#define SERVER_IP "35.153.79.3"
#define SERVER_PORT 9482
#define MSG_SIZE 32
#define MSG_TYPE_A 0
#define MSG_TYPE_B 1
#define DEVICE_ID 1
#define PASSCODE_LO 0
#define PASSCODE_HI 0 // later both to be changed for AES crypto
#define STARTER_BUTTON_TIMER 200
#define MSG_A_PERIOD_S 10

#define GPIO_CHIP_0 "gpiochip0"
#define GPIO_CHIP_1 "gpiochip1"
#define VAL0_PIN 35
#define VAL1_PIN 37
#define NC_PIN 36
#define NO_PIN 38
#define MOTOR_STATE_PIN 33

int client_socket;
timer_t motor_cutoff_timer;
timer_t msg_A_timer;

int get_rssi(void) { return -33; }

int get_pin_state(int pin) {
  char *chipname = GPIO_CHIP_0;
  unsigned int line_num = pin;
  int val = -1;
  struct gpiod_chip *chip;
  struct gpiod_line *line;
  int i, ret;

  chip = gpiod_chip_open_by_name(chipname);
  if (!chip) {
    perror("Open chip failed\n");
    goto end;
  }

  line = gpiod_chip_get_line(chip, line_num);
  if (!line) {
    perror("Get line failed\n");
    goto close_chip;
  }

  ret = gpiod_line_request_input(line, "Consumer");
  if (ret < 0) {
    perror("Request line as input failed\n");
    goto release_line;
  }

  val = gpiod_line_get_value(line);
  if (val < 0) {
    perror("Read line input failed\n");
    goto release_line;
  }

release_line:
  gpiod_line_release(line);
close_chip:
  gpiod_chip_close(chip);
end:
  return val;
}

uint8_t gen_GPIO_state_byte(void) {
  int val0_state = get_pin_state(VAL0_PIN);
  int val1_state = get_pin_state(VAL1_PIN);
  int nc_state = get_pin_state(NC_PIN);
  int no_state = get_pin_state(NO_PIN);
  int motor_state = get_pin_state(MOTOR_STATE_PIN);

  uint8_t byte = ((motor_state << 4) | (val1_state << 3) | (val0_state << 2) |
                  (nc_state << 1) | no_state) &
                 0xFF;

  return byte;
}

void start_timer(int sec, void (*handler)(int, siginfo_t *, void *),
                 timer_t *timerid) {
  struct sigevent sev;
  struct itimerspec its;
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = handler;
  sigaction(SIGRTMIN, &sa, NULL);

  sev.sigev_notify = SIGEV_SIGNAL;
  sev.sigev_signo = SIGRTMIN;
  sev.sigev_value.sival_ptr = timerid;
  timer_create(CLOCK_REALTIME, &sev, timerid);

  its.it_value.tv_sec = sec;
  its.it_value.tv_nsec = 0;
  its.it_interval.tv_sec = sec;
  its.it_interval.tv_nsec = 0;
  timer_settime(*timerid, 0, &its, NULL);
}

int get_timer_state(timer_t *timerid) {
  // Get the current state of the timer
  struct itimerspec current_its;
  timer_gettime(*timerid, &current_its);
  return current_its.it_value.tv_sec / 60;
}

void gen_msg_A(uint8_t buffer[MSG_SIZE]) {
  buffer[0] = MSG_TYPE_A;
  buffer[1] = PASSCODE_LO;
  buffer[2] = PASSCODE_HI;
  buffer[3] = DEVICE_ID;
  buffer[4] = get_rssi();

  /*uint16_t adc0 = ADC_Read (0);
   uint16_t adc1 = ADC_Read (1);
   uint16_t adc2 = ADC_Read (2);
   uint16_t adc3 = ADC_Read (3);*/

  uint16_t adc0 = 100;
  uint16_t adc1 = 450;
  uint16_t adc2 = 560;
  uint16_t adc3 = 190;
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

  buffer[12] = gen_GPIO_state_byte();
}

void send_msg_A(int signum, siginfo_t *info, void *context) {
  printf("timer expired. sending msg A\n");
  uint8_t message[MSG_SIZE];
  memset(message, 0, sizeof(message));
  gen_msg_A(message);
  int sent_bytes = send(client_socket, message, sizeof(message), 0);
  if (sent_bytes != MSG_SIZE) {
    printf("Error. Sent %d out of %d bytes\n", sent_bytes, MSG_SIZE);
  }
  printf("sent %d bytes\n", sent_bytes);
}

void *receive_data(void *arg) {

  while (1) {
    // Receive data from the server
    uint8_t buffer[MSG_SIZE];
    recv(client_socket, buffer, sizeof(buffer), 0);
    printf("Server says: %s\n", buffer);
  }
}

int main() {
  struct sockaddr_in server_address;

  // Create a socket
  client_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (client_socket == -1) {
    perror("Error creating socket");
    exit(1);
  }

  // Set up the server address structure
  server_address.sin_family = AF_INET;
  server_address.sin_port =
      htons(SERVER_PORT); // Change this port number as needed
  server_address.sin_addr.s_addr =
      inet_addr(SERVER_IP); // Replace with the server's IP address or domain

  // Connect to the server
  if (connect(client_socket, (struct sockaddr *)&server_address,
              sizeof(server_address)) == -1) {
    perror("Error connecting to server");
    close(client_socket);
    exit(1);
  }

  // Send MSG A periodically
  start_timer(MSG_A_PERIOD_S, send_msg_A, &msg_A_timer);

  pthread_t receiver_thread;
  pthread_create(&receiver_thread, NULL, receive_data, NULL);
  pthread_join(receiver_thread, NULL);
  // Close the socket
  close(client_socket);

  return 0;
}
