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
#include "common.h"
#include "server.h"
#include "spi_device/spi.h"
#include "timer.h"
#include <json-c/json.h>

#define CONSUMER "Motor controller"

#define STARTER_BUTTON_TIMER 200
#define MSG_A_PERIOD_S 5
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
#define MODEM_RECONNECT_TIME 60

#define ADDRESS "tcp://192.168.193.106:1883"
#define CLIENTID "Kelakadu"
#define TOPIC "voltage/"
#define SUBS_TOPIC "command/"
#define QOS 0
#define CMD_QOS 2
#define TIMEOUT 10000L

timer_w_t motor_cutoff_timer;
timer_w_t msg_A_timer;
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

static int get_rssi(void) { return -33; }

static MQTTClient *make_connection();

static int assign_pin(struct gpiod_chip *chip, struct gpiod_line **line,
                      int pin, enum pin_dir dir) {
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

static int gpio_init() {
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

static void hard_reset_modem(void) {
  printf("Resetting USB power.\n");
  gpiod_line_set_value(usb_power, 0);
  usleep(USB_POWER_RESET_TIME * 1000 * 1000);
  gpiod_line_set_value(usb_power, 1);
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

  // Prepare the MQTT message
  // Publish a message
  MQTTClient_message pubmsg = MQTTClient_message_initializer;
  MQTTClient_deliveryToken token;
  pubmsg.payload = (char *)jsonString;
  pubmsg.payloadlen = (int)strlen(jsonString);
  pubmsg.qos = QOS;
  pubmsg.retained = 0;

  // Publish the message to topic
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

static void start_motor() {
  printf("Starting motor\n");
  gpiod_line_set_value(no, 1);
  usleep(STARTER_BUTTON_TIMER * 1000);
  gpiod_line_set_value(no, 0);
}

static void stop_motor() {
  printf("Stopping motor\n");
  gpiod_line_set_value(nc, 1);
  usleep(STARTER_BUTTON_TIMER * 1000);
  gpiod_line_set_value(nc, 0);
}

static void stop_motor_t(union sigval sv) {
  stop_motor();
  stop_timer(&motor_cutoff_timer);
}

// Function to control the motor
static void setMotorState(const char *state, const int timeout) {
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
static void setValveState(const char *v0State) {
  printf("Setting valve0 state %s\n", v0State);
  if (strcmp(v0State, "close") == 0) {
    gpiod_line_set_value(valve0, 1);
    gpiod_line_set_value(valve1, 0);
  } else if (strcmp(v0State, "open") == 0) {
    gpiod_line_set_value(valve0, 0);
    gpiod_line_set_value(valve1, 1);
  }
}

// Message arrival callback function
static int messageArrived(void *context, char *topicName, int topicLen,
                          MQTTClient_message *message) {
  char *payload = message->payload;

  // Parse the JSON message
  struct json_object *parsed_json;
  struct json_object *command, *state, *timeout;

  parsed_json = json_tokener_parse(payload);
  if (!json_object_object_get_ex(parsed_json, "command", &command)) {
    printf("Error parsing command\n");
    goto graceful_end;
  }
  if (!json_object_object_get_ex(parsed_json, "state", &state)) {
    printf("Error parsing state\n");
    goto graceful_end;
  }
  if (!json_object_object_get_ex(parsed_json, "timeout", &timeout)) {
    printf("Error parsing timeout\n");
    goto graceful_end;
  }

  // Interpret and execute the command
  if (strcmp(json_object_get_string(command), "set_motor_state") == 0) {
    printf("Received set_motor_state command\n");
    setMotorState(json_object_get_string(state), json_object_get_int(timeout));
  } else if (strcmp(json_object_get_string(command), "set_valve0_state") == 0) {
    printf("Received set_valve0_state command\n");
    setValveState(json_object_get_string(state));
  } else {
    printf("Unknown command received.\n");
  }

graceful_end:
  // Clean up
  MQTTClient_freeMessage(&message);
  MQTTClient_free(topicName);

  return 1;
}

static MQTTClient *make_connection() {
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
      usleep(MODEM_RECONNECT_TIME * 1000 * 1000); // wait for modem to connect to network
    } else {
      printf("MQTT connection established\n");
      break;
    }
  }

  // Subscribe to the topic
  MQTTClient_subscribe(client, SUBS_TOPIC, CMD_QOS);

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
