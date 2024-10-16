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

#include "MQTTClient.h"
#include "aes/aes.h"
#include "common.h"
#include "server.h"
#include "spi_device/spi.h"
#include "timer.h"
#include <json-c/json.h>

#define CONSUMER "Motor controller"
#define SERVER_IP "192.168.193.106"
#define DEVICE_ID 1

#define STARTER_BUTTON_TIMER 200
#define MSG_A_PERIOD_S 30
#define USB_POWER_RESET_TIME 5
#define GPIO_CHIP_0 "gpiochip0"
#define GPIO_CHIP_1 "gpiochip1"
#define GPIO_CHIP_2 "gpiochip2"
#define VAL0_PIN 15
#define VAL1_PIN 18
#define NC_PIN 0
#define NO_PIN 1
#define MOTOR_STATE_PIN 16
#define MAX_CONN_ERR 3
#define USB_POWER_PIN 26

#define ADDRESS "tcp://192.168.193.106:1883"
#define CLIENTID "Kelakadu"
#define TOPIC "voltage/"
#define SUBS_TOPIC "command/"
#define QOS 1
#define TIMEOUT 10000L

int client_socket = -1;
timer_w_t motor_cutoff_timer;
timer_w_t msg_A_timer;
int conn_err_cnt = MAX_CONN_ERR;
MQTTClient client;

struct gpiod_chip *chip2;
struct gpiod_line *valve0;
struct gpiod_line *valve1;
struct gpiod_line *nc;
struct gpiod_line *no;
struct gpiod_line *motor_state;
struct gpiod_chip *chip1;
struct gpiod_line *usb_power;

enum pin_dir { INPUT, OUTPUT };

int get_rssi(void) { return -33; }

MQTTClient *make_connection();

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
  {
    char *chipname = GPIO_CHIP_2;
    chip2 = gpiod_chip_open_by_name(chipname);
    if (!chip2) {
      printf("Open chip failed\n");
      return -1;
    }
  }

  int ret = 0;
  ret = assign_pin(chip2, &valve0, VAL0_PIN, OUTPUT);
  ret |= assign_pin(chip2, &valve1, VAL1_PIN, OUTPUT);
  ret |= assign_pin(chip2, &nc, NC_PIN, OUTPUT);
  ret |= assign_pin(chip2, &no, NO_PIN, OUTPUT);
  ret |= assign_pin(chip2, &motor_state, MOTOR_STATE_PIN, INPUT);

  {
    char *chipname = GPIO_CHIP_1;
    chip1 = gpiod_chip_open_by_name(chipname);
    if (!chip1) {
      printf("Open chip failed\n");
      return -1;
    }
  }

  ret |= assign_pin(chip1, &usb_power, USB_POWER_PIN, OUTPUT);
  gpiod_line_set_value(usb_power, 1);

  return ret;
}

void hard_reset_modem(void) {
  printf("Resetting USB power.\n");
  gpiod_line_set_value(usb_power, 0);
  usleep(USB_POWER_RESET_TIME * 1000 * 1000);
  gpiod_line_set_value(usb_power, 1);
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
      // try reconnecting
      close(client_socket);
      stop_timer(&msg_A_timer);
      // client_socket = make_connection();
      start_timer(MSG_A_PERIOD_S, MSG_A_PERIOD_S, send_msg_A, &msg_A_timer, -1);
      conn_err_cnt = 0;
    }
  } else {
    conn_err_cnt = 0;
  }
}

static float adc_to_coil_curr_conv(const uint16_t v) {
  const float resolution = 0.05;
  const float zeroCurVal = 1.65;
  const float curVal = (v - zeroCurVal) / resolution;
  return curVal;
}

static float adc_to_phase_volt_conv(const uint16_t v) {
  const int r1 = 2e6;
  const int r2 = 20e3;
  const float pv = v * (r1 + r2) / r2;
  return pv;
}

static void float_to_str(const float val, char *s, const int len) {
  memset(s, 0, len);
  snprintf(s, len, "%.2f", val);
}

static void send_phase_voltages(void) {
  const float adc0 = adc_to_coil_curr_conv(get_avg_voltage(0, 5));
  const float adc1 = adc_to_phase_volt_conv(get_avg_voltage(1, 5));
  const float adc2 = adc_to_phase_volt_conv(get_avg_voltage(2, 5));
  const float adc3 = adc_to_phase_volt_conv(get_avg_voltage(3, 5));

  const int val0_state = gpiod_line_get_value(valve0);
  const int val1_state = gpiod_line_get_value(valve1);
  const int nc_state = gpiod_line_get_value(nc);
  const int no_state = gpiod_line_get_value(no);
  const int motor_state_val = gpiod_line_get_value(motor_state);
  const int remTime = get_timer_state(&motor_cutoff_timer);

  char str[10];
  const int str_len = sizeof(str);

  // Create a JSON object
  struct json_object *jsonObj = json_object_new_object();

  // Add data to the JSON object
  snprintf(str, str_len, "%s", val0_state ? "close" : "open");
  json_object_object_add(jsonObj, "valve0_state", json_object_new_string(str));

  snprintf(str, str_len, "%s", val1_state ? "close" : "open");
  json_object_object_add(jsonObj, "valve1_state", json_object_new_string(str));

  snprintf(str, str_len, "%s", nc_state ? "close" : "open");
  json_object_object_add(jsonObj, "nc_state", json_object_new_string(str));

  snprintf(str, str_len, "%s", no_state ? "close" : "open");
  json_object_object_add(jsonObj, "no_state", json_object_new_string(str));

  snprintf(str, str_len, "%s", motor_state_val ? "off" : "on");
  json_object_object_add(jsonObj, "motor_state", json_object_new_string(str));

  snprintf(str, str_len, "%d", remTime);
  json_object_object_add(jsonObj, "cutoff_time", json_object_new_string(str));

  float_to_str(adc0, str, str_len);
  json_object_object_add(jsonObj, "coil_current", json_object_new_string(str));

  float_to_str(adc1, str, str_len);
  json_object_object_add(jsonObj, "phase1", json_object_new_string(str));

  float_to_str(adc2, str, str_len);
  json_object_object_add(jsonObj, "phase2", json_object_new_string(str));

  float_to_str(adc3, str, str_len);
  json_object_object_add(jsonObj, "phase3", json_object_new_string(str));

  // Convert the JSON object to a string
  const char *jsonString = json_object_to_json_string(jsonObj);
  printf("JSON Message: %s\n", jsonString);

  // Prepare the MQTT message
  // Publish a message
  MQTTClient_message pubmsg = MQTTClient_message_initializer;
  MQTTClient_deliveryToken token;
  pubmsg.payload = (char *)jsonString;
  pubmsg.payloadlen = (int)strlen(jsonString);
  pubmsg.qos = QOS;
  pubmsg.retained = 0;

  // Publish the message to the ThingsBoard telemetry topic
  MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
  printf("Publishing data: %s on topic %s\n", jsonString, TOPIC);

  // Wait for the message to be delivered
  MQTTClient_waitForCompletion(client, token, TIMEOUT);
  printf("Message with delivery token %d delivered\n", token);

  // Cleanup
  json_object_put(jsonObj); // Decrement reference count and free if necessary
}

static void close_mqtt_conn(MQTTClient *c) {
  // Disconnect from the broker
  MQTTClient_disconnect(*c, 10000);
  MQTTClient_destroy(c);
}

static void send_message(void) {
  if (!MQTTClient_isConnected(client)) {
    make_connection();
  }
  send_phase_voltages();
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

// Function to control the motor
void setMotorState(const char *state, int timeout) {
  uint8_t curMotorState = gpiod_line_get_value(motor_state);
  if (strcmp(state, "on") == 0) {
    printf("Turning on the motor for %d minutes.\n", timeout);
    // motor_state HI=OFF; LOW=ON
    int remTimeSec = timeout * 60;
    if ((timeout > 0) && (curMotorState)) {
      start_motor();
      start_timer(remTimeSec, 0, stop_motor_t, &motor_cutoff_timer, -1);
    } else if ((timeout > 0) && (!curMotorState)) { // adjust timer
      adjust_timer(remTimeSec, 0, stop_motor_t, &motor_cutoff_timer, -1);
    }
  } else if (strcmp(state, "off") == 0) {
    if (!curMotorState) {
      printf("Turning off the motor.\n");
      stop_motor();
      stop_timer(&motor_cutoff_timer);
    }
  }
}

// Function to control valve states
// both valves cannot be closed at the same time
void setValveState(const char *v0State) {
  printf("Setting valve0 state %s\n", v0State);
  if (strcmp(v0State, "close") == 0) {
    gpiod_line_set_value(valve0, 0);
    gpiod_line_set_value(valve1, 1);
  } else if (strcmp(v0State, "open") == 0) {
    gpiod_line_set_value(valve0, 1);
    gpiod_line_set_value(valve1, 0);
  }
}

// Message arrival callback function
int messageArrived(void *context, char *topicName, int topicLen,
                   MQTTClient_message *message) {
  char *payload = message->payload;

  // Parse the JSON message
  struct json_object *parsed_json;
  struct json_object *command, *state, *timeout;

  parsed_json = json_tokener_parse(payload);
  if (!json_object_object_get_ex(parsed_json, "command", &command)) {
    printf("Error parsing command\n");
    return 0;
  }
  if (!json_object_object_get_ex(parsed_json, "state", &state)) {
    printf("Error parsing state\n");
    return 0;
  }
  if (!json_object_object_get_ex(parsed_json, "timeout", &timeout)) {
    printf("Error parsing timeout\n");
    return 0;
  }

  // Interpret and execute the command
  if (strcmp(json_object_get_string(command), "set_motor_state") == 0) {
    setMotorState(json_object_get_string(state), json_object_get_int(timeout));
  } else if (strcmp(json_object_get_string(command), "set_valve0_state") == 0) {
    setValveState(json_object_get_string(state));
  } else {
    printf("Unknown command received.\n");
  }

  // Clean up
  MQTTClient_freeMessage(&message);
  MQTTClient_free(topicName);

  return 1;
}

MQTTClient *make_connection() {
  MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
  int rc;

  // Create MQTT client
  MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE,
                    NULL);
  MQTTClient_setCallbacks(client, NULL, NULL, messageArrived, NULL);
  MQTTClient_setTraceLevel(MQTTCLIENT_TRACE_MAXIMUM);

  // Set connection options
  conn_opts.keepAliveInterval = 20;
  conn_opts.cleansession = 1;
  conn_opts.username = "kelakadu";
  conn_opts.password = "Q3;'bw4RGK_#!Zb";

  // Connect to the broker (only once)
  while (1) {
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
      printf("Failed to connect, return code %d\n", rc);
      hard_reset_modem();
      usleep(30 * 1000 * 1000); // wait for 30 sec
      exit(EXIT_FAILURE);
    } else {
      printf("MQTT connection established\n");
      break;
    }
  }

  // Subscribe to the topic
  MQTTClient_subscribe(client, SUBS_TOPIC, QOS);

  printf("Listening for messages on topic: %s\n", SUBS_TOPIC);
  return &client;
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

  // set default valve states on restart
  setValveState("open");
  // Send MSG A periodically
  while (1) {
    send_message();
    usleep(MSG_A_PERIOD_S * 1000 * 1000);
  }

  return 0;
}
