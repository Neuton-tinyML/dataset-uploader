#ifndef SENDER_H
#define SENDER_H

#include <stdio.h>
#include <stdint.h>
#include <uv.h>

#include "simple_csv.h"


typedef enum
{
	STATE_GET_MODEL_INFO = 0,
	STATE_SEND_DATASET_INFO,
	STATE_SEND_SAMPLES,
	STATE_GET_PERFORMANCE_COUNTERS,
	STATE_SHUTDOWN,
}
SenderState;


typedef struct
{
	uv_timer_t* timer;
	uv_udp_t* socket;
	int fd;
	uv_fs_t* read_request;
	struct sockaddr_in addr;

	SenderState state;
	uint32_t sampleSent;
	uint32_t retries;
	uint32_t maxRetries;
	float*   sample;
	uint32_t sampleSize;
	uint32_t error;

	SimpleCsvReader *csvReader;

	uint32_t columnsInSample;
	uint32_t columnsInResult;
	uint32_t taskType;

	uint32_t isUdp;
}
Sender;


Sender* sender_create(uint8_t isUdp, const char* dataset,
					  int bindPort, int sendPort,
					  const char* serial, int speed);
void sender_destroy(Sender *sender);
int sender_run(Sender* sender, uint32_t delay);
void sender_finish(Sender* sender);


#endif // SENDER_H
