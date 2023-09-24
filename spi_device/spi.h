#ifndef SPI_BASE_H
#define SPI_BASE_H
#endif


int spi_init();

uint16_t get_raw_voltage(int channel);

float get_avg_voltage(int channel, int num_read);
