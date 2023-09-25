/*

Server to handle message from device and client. Message implementation are
detailed in
https://docs.google.com/document/d/1qKUy8KDCigRU7vQpS-_mssLqFaAL-WPIdzxzrYwKAF0

*/

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "server.h"
#include "aes/aes.h"

struct device_s all_devices[MAX_DEVICES];

void rand_device_list(struct device_s device_list[MAX_DEVICES]) {
  for (int i = 0; i < 2; i++) {
    struct device_s *d = device_list + i;
    d->id = 1 + i;
    d->rem_cut_off_time = 45;
  }
}

void store_data(const int in_socket, const char in_buffer[MSG_SIZE]) {
  struct device_s *current_device;
  for (int i = 0; i < MAX_DEVICES; i++) {
    int current_device_id = all_devices[i].id;
    if (current_device_id == 0 || current_device_id == in_buffer[3]) {
      current_device = &all_devices[i];
      break;
    }
    perror("All device buffer full. No space for new device");
    exit(EXIT_FAILURE);
  }

  int idx = 1;
  current_device->passcode = *((int16_t *)(in_buffer + idx));
  idx += 2;
  current_device->id = in_buffer[idx];
  printf("storing device id %d\n", current_device->id);
  idx++;
  current_device->last_rssi = in_buffer[idx];
  idx++;

  idx += 5; // Skipping ADC for now
  current_device->rem_cut_off_time = *((int16_t *)(in_buffer + idx));
  idx += 2;
  current_device->gpio_states = in_buffer[idx];
  current_device->socket = in_socket;
  memcpy(current_device->msg_A_buf, in_buffer, sizeof(char) * MSG_SIZE);
}

void get_device_list(char out_buffer[MSG_SIZE]) {
  memset(out_buffer, 0, sizeof(char) * MSG_SIZE);
  int device_cnt = 0;
  for (int i = 0; i < MAX_DEVICES; i++) {
    int current_device_id = all_devices[i].id;
    if (current_device_id != 0) {
      out_buffer[device_cnt + 2] = current_device_id;
      device_cnt++;
    }
  }

  out_buffer[0] = MSG_TYPE_D0;
  out_buffer[1] = device_cnt;
  printf("dev cnt %d\n", device_cnt);
}

int get_device_buffer(int device_id, char out_buffer[MSG_SIZE],
                      enum message_types msg_type) {
  printf("got device id %d\n", device_id);
  memset(out_buffer, 0, sizeof(char) * MSG_SIZE);
  struct device_s *device;
  for (int i = 0; i < MAX_DEVICES; i++) {
    device = &all_devices[i];
    if (device->id == device_id) { /* Msg A and D1; B and C2 are relayed between
                                      device and user. So it is just copied */
      printf("found device\n");
      if (msg_type == MSG_TYPE_D1)
        memcpy(out_buffer, device->msg_A_buf, sizeof(char) * MSG_SIZE);
      else if (msg_type == MSG_TYPE_B)
        memcpy(out_buffer, device->msg_C2_buf, sizeof(char) * MSG_SIZE);
      break;
    }
  }
  out_buffer[0] = msg_type;
  return device->socket;
}

void set_device_buffer(int device_id, const char in_buffer[MSG_SIZE]) {
  struct device_s *device;
  for (int i = 0; i < MAX_DEVICES; i++) {
    device = &all_devices[i];
    if (device->id == device_id) {
      memset(device->msg_C2_buf, 0, sizeof(char) * MSG_SIZE);
      memcpy(device->msg_C2_buf, in_buffer, sizeof(char) * MSG_SIZE);
      break;
    }
  }
}

int handle_client_message(const int in_socket,
                          const char in_buffer[AES_MSG_SIZE],
                          char out_buffer[AES_MSG_SIZE]) {
  enum message_types msg_type = in_buffer[MSG_TYPE_IDX];
  printf("Msg type %d\n", msg_type);
  int out_socket = -1;
  int device_id;

  switch (msg_type) {
  case MSG_TYPE_A:
    printf("Got msg A\n");
    store_data(in_socket, in_buffer);
    break;

  case MSG_TYPE_C0:
    // rand_device_list(all_devices);
    get_device_list(out_buffer);
    unsigned int passcode = *((uint16_t *)(in_buffer + 1));
    printf("Received passcode %d\n", passcode);
    if (passcode == CLIENT_PASSCODE) {
      printf("Responding to client\n");
      out_socket = in_socket;
    } else {
      out_socket = -1;
    }
    break;

  case MSG_TYPE_C1:
    device_id = in_buffer[1];
    get_device_buffer(device_id, out_buffer, MSG_TYPE_D1);
    out_socket = in_socket;
    break;

  case MSG_TYPE_C2:
    unsigned char iv[AES_IV_LENGTH_BYTE];
    memcpy(iv, in_buffer + 1, AES_IV_LENGTH_BYTE);
    unsigned char key[AES_KEY_LENGTH_BYTE] = AES_KEY;

    uint8_t decData[MSG_SIZE];
    memset(decData, 0, sizeof(decData));
    decryptAES(in_buffer + 1 + AES_IV_LENGTH_BYTE, MSG_SIZE, (uint8_t *)key,
               (uint8_t *)iv, decData);
    device_id = decData[3];
    set_device_buffer(device_id, decData);
    out_socket = get_device_buffer(device_id, decData, MSG_TYPE_B);

    memcpy(out_buffer, iv, AES_IV_LENGTH_BYTE);
    encryptAES(decData, MSG_SIZE, (uint8_t *)key, (uint8_t *)iv, out_buffer + AES_IV_LENGTH_BYTE);
    break;

  default:
    perror("Unknown message type");
  }

  return out_socket;
}

int main() {
  int server_fd, new_socket, client_sockets[MAX_CLIENTS],
      max_clients = MAX_CLIENTS;
  struct sockaddr_in address;
  int addrlen = sizeof(address);
  char in_buffer[AES_MSG_SIZE] = {0};
  char out_buffer[AES_MSG_SIZE] = {0};

  // Create a socket
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("Socket creation failed");
    exit(EXIT_FAILURE);
  }

  // Set socket options to allow reusing the address
  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt))) {
    perror("Setsockopt failed");
    exit(EXIT_FAILURE);
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(SERVER_PORT);

  // Bind the socket to the specified port
  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("Bind failed");
    exit(EXIT_FAILURE);
  }

  // Listen for incoming connections
  if (listen(server_fd, max_clients) < 0) {
    perror("Listen failed");
    exit(EXIT_FAILURE);
  }

  printf("Server listening on port %d...\n", SERVER_PORT);

  fd_set read_fds;
  int max_sd, activity;
  for (int i = 0; i < max_clients; i++) {
    client_sockets[i] = 0;
  }

  while (1) {
    FD_ZERO(&read_fds);
    FD_SET(server_fd, &read_fds);
    max_sd = server_fd;

    for (int i = 0; i < max_clients; i++) {
      if (client_sockets[i] > 0) {
        FD_SET(client_sockets[i], &read_fds);
      }
      if (client_sockets[i] > max_sd) {
        max_sd = client_sockets[i];
      }
    }

    activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL);
    if ((activity < 0) && (errno != EINTR)) {
      printf("Select error\n");
    }

    if (FD_ISSET(server_fd, &read_fds)) {
      if ((new_socket = accept(server_fd, (struct sockaddr *)&address,
                               (socklen_t *)&addrlen)) < 0) {
        perror("Accept failed");
      }

      printf("New connection, socket fd is %d\n", new_socket);

      for (int i = 0; i < max_clients; i++) {
        if (client_sockets[i] == 0) {
          client_sockets[i] = new_socket;
          printf("Adding to list of sockets as %d\n", i);
          break;
        }
      }
    }

    for (int i = 0; i < max_clients; i++) {
      if (FD_ISSET(client_sockets[i], &read_fds)) {
        int valread;
        if ((valread = read(client_sockets[i], in_buffer, AES_MSG_SIZE)) == 0) {
          getpeername(client_sockets[i], (struct sockaddr *)&address,
                      (socklen_t *)&addrlen);
          printf("Host disconnected, ip %s, port %d\n",
                 inet_ntoa(address.sin_addr), ntohs(address.sin_port));
          close(client_sockets[i]);
          client_sockets[i] = 0;
        } else {
          printf("Received message from client %d\n", i);
          int send_socket =
              handle_client_message(client_sockets[i], in_buffer, out_buffer);
          if (send_socket > -1) {
            int sent_bytes;
            sent_bytes = send(send_socket, out_buffer, sizeof(out_buffer), 0);
            if (sent_bytes < 0)
              printf("Could not send data to socket %d\n", send_socket);
            else {
              printf("Send success %d bytes\n", sent_bytes);
            }
          }
        }
      }
    }
  }

  return 0;
}
