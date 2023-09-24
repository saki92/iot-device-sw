#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

#include <spi.h>

#define SPI_DEVICE          "/dev/spidev0.0"
#define LEN_DATA            3
#define NUM_READS           10
#define VREF                3.3

struct spi_ioc_transfer trx;
uint32_t spi_speed = 1000000;
int fd;
int ret;
uint8_t scratch;
uint32_t scratch32;

int spi_init()
{
  fd = open(SPI_DEVICE, O_RDWR);
  if(fd < 0) {
    printf("Could not open the SPI device...\r\n");
    exit(EXIT_FAILURE);
  }

  ret = ioctl(fd, SPI_IOC_RD_MODE, &scratch);
  if(ret != 0) {
    printf("Could not read SPI mode...\r\n");
    close(fd);
    exit(EXIT_FAILURE);
  }
  printf("SPI mode is %x\n", scratch);

  scratch |= SPI_MODE_0;

  ret = ioctl(fd, SPI_IOC_WR_MODE, &scratch);
  if(ret != 0) {
    printf("Could not write SPI mode...\r\n");
    close(fd);
    exit(EXIT_FAILURE);
  }

  ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &scratch32);
  if(ret != 0) {
    printf("Could not read the SPI max speed...\r\n");
    close(fd);
    exit(EXIT_FAILURE);
  }
  printf("SPI max speed is %d\n", scratch32);

  scratch32 = 2000000;

  ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &scratch32);
  if(ret != 0) {
    printf("Could not write the SPI max speed...\r\n");
    close(fd);
    exit(EXIT_FAILURE);
  }
}

uint16_t get_raw_voltage(int channel)
{
  uint8_t tx_buffer[LEN_DATA];
  uint8_t rx_buffer[LEN_DATA];
  trx.tx_buf = (unsigned long)tx_buffer;
  trx.rx_buf = (unsigned long)rx_buffer;
  trx.bits_per_word = 0;
  trx.speed_hz = spi_speed;
  trx.delay_usecs = 0;
  trx.len = LEN_DATA;
  float avgVolt = 0;
  float sumVolt = 0;

  tx_buffer[0] = 0x01; //start bit
  tx_buffer[1] = 0x80 | ((channel & 0x03) << 4); // single or diff | channel number
  tx_buffer[2] = 0x00;

  rx_buffer[0] = 0x00;
  rx_buffer[1] = 0x00;
  rx_buffer[2] = 0x00;

  ret = ioctl(fd, SPI_IOC_MESSAGE(1), &trx);
  if(ret < 0) {
    printf("SPI transfer returned %d... errorno %d\r\n", ret, errno);
  }

  int doc = ((rx_buffer[1] & 0x3) << 8) | (rx_buffer[2] & 0xFF);

  return doc;
}

float get_avg_voltage(int channel, int num_read)
{
  uint8_t tx_buffer[LEN_DATA];
  uint8_t rx_buffer[LEN_DATA];
  trx.tx_buf = (unsigned long)tx_buffer;
  trx.rx_buf = (unsigned long)rx_buffer;
  trx.bits_per_word = 0;
  trx.speed_hz = spi_speed;
  trx.delay_usecs = 0;
  trx.len = LEN_DATA;
  float avgVolt = 0;
  float sumVolt = 0;

  tx_buffer[0] = 0x01; //start bit
  tx_buffer[1] = 0x80 | ((channel & 0x03) << 4); // single or diff | channel number
  tx_buffer[2] = 0x00;

  for (int i = 0; i < num_read; i++) {
    rx_buffer[0] = 0x00;
    rx_buffer[1] = 0x00;
    rx_buffer[2] = 0x00;

    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &trx);
    if(ret < 0) {
      printf("SPI transfer returned %d... errorno %d\r\n", ret, errno);
    }

    int16_t doc = ((rx_buffer[1] & 0x3) << 8) | (rx_buffer[2] & 0xFF);
    float volt = ((float)doc) * VREF / 1024;

    sumVolt += volt;
  }
  avgVolt = sumVolt / num_read;
  return avgVolt;
}
