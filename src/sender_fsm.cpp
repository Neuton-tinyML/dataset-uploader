#include <stdlib.h>
#include <string.h>

#include "sender_fsm.h"
#include "checksum.h"
#include "protocol.h"


uint8_t sender_read_sample(Sender* sender);


static uv_buf_t alloc_buffer(Sender* sender)
{
	const size_t size = sender->sampleSize + sizeof(uint16_t) + sizeof(PacketHeader);
	uv_buf_t buffer;

	buffer.base = (char*) calloc(1, size);
	buffer.len = buffer.base ? size : 0;

	if (buffer.len == 0)
	{
		fprintf(stderr, "Failed to allocate send buffer\n");
		sender_finish(sender);
	}

	return buffer;
}


static void make_packet(Sender* sender, uv_buf_t* buffer, size_t size, PacketType type, ErrorCode err)
{
	if (!buffer && buffer->len < sizeof(PacketHeader) && buffer->base == NULL)
	{
		fprintf(stderr, "%s: invalid buffer\n", __func__);
		sender_finish(sender);
		return;
	}

	PacketHeader* hdr = (PacketHeader*) buffer->base;

	hdr->preamble = PREAMBLE;
	hdr->type = type;
	hdr->error = err;
	hdr->size = sizeof(PacketHeader) + size + sizeof(uint16_t);

	if (hdr->size > buffer->len)
	{
		fprintf(stderr, "%s: buffer too small: requested %u actual %zu\n", __func__, hdr->size, buffer->len);
		sender_finish(sender);
		return;
	}

	buffer->len = hdr->size;

	uint16_t crc = crc16_table((uint8_t*) hdr, hdr->size - sizeof(uint16_t), 0);
	memcpy(buffer->base + hdr->size - sizeof(uint16_t), &crc, sizeof(uint16_t));
}


static void sender_onTimer(uv_timer_t* handle)
{
	Sender* sender = (Sender*) handle->data;
	sender_fsm(sender, handle, NULL, 0);
}


static void sender_send_cb(uv_udp_send_t* req, int status)
{
	if (0 != status)
	{
		fprintf(stderr, "%s: status %d\n", __func__, status);
		sender_finish((Sender*) req->data);
		return;
	}

	for (uint32_t i = 0; i < (sizeof(req->bufsml) / sizeof(req->bufsml[0])); i++)
		if (req->bufsml[i].base)
			free(req->bufsml[i].base);

	free(req);
}


static void sender_fs_sent(uv_fs_t *req)
{
	int code = req->result;

	for (uint32_t i = 0; i < (sizeof(req->bufsml) / sizeof(req->bufsml[0])); i++)
		if (req->bufsml[i].base)
			free(req->bufsml[i].base);

	free(req);

	if (0 > code)
	{
		Sender* sender = (Sender*) req->data;
		fprintf(stderr, "%s: failed to send packet\n", __func__);
		sender_finish(sender);
		return;
	}

	return;
}


static void send_packet(Sender* sender, uv_buf_t buffer)
{
	if (sender->isUdp)
	{
		uv_udp_send_t* req = (uv_udp_send_t*) calloc(1, sizeof(uv_udp_send_t));
		if (!req)
		{
			fprintf(stderr, "%s: failed to alloc udp request\n", __func__);
			sender_finish(sender);
			return;
		}
		if (0 != uv_udp_send(req, sender->socket, &buffer, 1,
							(const struct sockaddr*) &sender->addr, sender_send_cb))
		{
			fprintf(stderr, "%s: failed to send packet\n", __func__);
			sender_finish(sender);
			return;
		}
	}
	else
	{
		uv_fs_t* req = (uv_fs_t*) calloc(1, sizeof(uv_fs_t));
		if (!req)
		{
			fprintf(stderr, "%s: failed to alloc fs request\n", __func__);
			sender_finish(sender);
			return;
		}

		req->data = sender;

		if (0 != uv_fs_write(uv_default_loop(), req, sender->fd, &buffer, 1, -1, sender_fs_sent))
		{
			fprintf(stderr, "%s: failed to send packet\n", __func__);
			sender_finish(sender);
			return;
		}
	}

#if defined(SENDER_SIMULATE_PACKETS)
	const uint32_t timeout = 0;
#else
	const uint32_t timeout = 2000;
#endif

	uv_timer_start(sender->timer, sender_onTimer, timeout, 0);
}


#if defined(SENDER_SIMULATE_PACKETS)
static void sim_packet(Sender* sender, void** buffer, size_t* size)
{
	static uint8_t __buf[2048];
	static uint8_t __init = 0;

	if (__init == 0)
	{
		__init = 1;
		memset(__buf, 0, sizeof(__buf));
	}

	PacketHeader* hdr = (PacketHeader*) __buf;

	*buffer = hdr;
	*size = 0;

	hdr->preamble = PREAMBLE;
	hdr->size = sizeof(PacketHeader);
	hdr->error = 0;

	void* payload = hdr + 1;

	SenderState state = sender->state;

	if (state == STATE_GET_MODEL_INFO)
	{
		hdr->type = ANS(MODEL_INFO);
		hdr->size += sizeof(ModelInfo);

		ModelInfo* info = payload;
		info->columnsCount = 3;
		info->taskType = REGRESSION;
	}
	else if (state == STATE_SEND_DATASET_INFO)
	{
		hdr->type = ANS(DATASET_INFO);
	}
	else if (state == STATE_SEND_SAMPLES)
	{
		hdr->type = ANS(DATASET_SAMPLE);
		hdr->size += sizeof(float) * sender->columnsInResult;

		float* result = payload;
		for (uint32_t i = 0; i < sender->columnsInResult; i++)
			result[i] = i;
	}
	else if (state == STATE_GET_PERFORMANCE_COUNTERS)
	{
		hdr->type = ANS(PERF_REPORT);
		hdr->size += sizeof(PerformanceReport);

		PerformanceReport* report = payload;
		report->freq = 48000000;
		report->ramUsage = 10446;
		report->bufferSize = 2048;
		report->flashUsage = 3072;
		report->usSampleMin = 2.0;
		report->usSampleMax = 8.0;
		report->usSampleAvg = 4.0;
	}
	if (state == STATE_SHUTDOWN)
	{
		*buffer = NULL;
		*size = 0;
		return;
	}

	uint16_t* crc = (uint16_t*) ((uint8_t*) hdr + hdr->size);
	hdr->size += sizeof(uint16_t);
	*crc = calc_checksum((uint8_t*) hdr, hdr->size - sizeof(uint16_t));
	*size = hdr->size;
}
#endif


static PacketHeader* check_packet(void* buffer, size_t size)
{
	if (!buffer || !size || size < (sizeof(PacketHeader) + sizeof(uint16_t)))
		return NULL;

	PacketHeader *hdr = (PacketHeader*) buffer;

	if (hdr->preamble != PREAMBLE || hdr->size > size)
		return NULL;

	uint16_t crc_packet;
	memcpy(&crc_packet, (uint8_t*) hdr + hdr->size - sizeof(uint16_t), sizeof(uint16_t));

	if (crc_packet != crc16_table((uint8_t*) hdr, hdr->size - sizeof(uint16_t), 0))
		return NULL;

	if (!IS_ANS(hdr->type))
		return NULL;

	return hdr;
}


void state_transition(Sender* sender, SenderState state)
{
	sender->state = state;
	sender->retries = 0;
	sender->sampleSent = 0;
	uv_timer_start(sender->timer, sender_onTimer, 0, 0);
}


void state_transition_delayed(Sender* sender, SenderState state, uint32_t delay)
{
	sender->state = state;
	sender->retries = 0;
	sender->sampleSent = 0;
	uv_timer_start(sender->timer, sender_onTimer, delay, 0);
}


static const char* error_to_str(uint8_t err)
{
	switch (err)
	{
	case ERROR_SUCCESS:
		return "SUCCESS";
	case ERROR_INVALID_SIZE:
		return "INVALID_SIZE";
	case ERROR_NO_MEMORY:
		return "NO_MEMORY";
	case ERROR_SEND_AGAIN:
		return "SEND_AGAIN";
	default:
		return "UNKNOWN";
	}
}


static const char* state_to_str(uint8_t err)
{
	switch (err)
	{
	case STATE_GET_MODEL_INFO:
		return "GET_MODEL_INFO";
	case STATE_SEND_DATASET_INFO:
		return "SEND_DATASET_INFO";
	case STATE_SEND_SAMPLES:
		return "SEND_SAMPLES";
	case STATE_GET_PERFORMANCE_COUNTERS:
		return "GET_PERFORMANCE_COUNTERS";
	case STATE_SHUTDOWN:
		return "SHUTDOWN";
	default:
		return "UNKNOWN";
	}
}


void sender_fsm(Sender* sender,uv_timer_t* timer, void* buffer, size_t size)
{
	if (!sender)
		return;

#if defined(SENDER_SIMULATE_PACKETS)
	sim_packet(sender, &buffer, &size);
#endif

	PacketHeader* in_packet = check_packet(buffer, size);
	if (in_packet)
		in_packet->size -= sizeof(uint16_t) + sizeof(PacketHeader);

	void* payload = in_packet + 1;

	if (in_packet && PACKET_TYPE(in_packet->type) == TYPE_ERROR)
	{
		fprintf(stderr, "%s: error %s, state %s\n", __func__, error_to_str(in_packet->error), state_to_str(sender->state));
		uv_timer_start(sender->timer, sender_onTimer, 1000, 0);
		return;
	}

	if (sender->state == STATE_GET_MODEL_INFO)
	{
		if (in_packet && PACKET_TYPE(in_packet->type) == TYPE_MODEL_INFO)
		{
			if (in_packet->size >= sizeof(ModelInfo))
			{
				ModelInfo* mi = (ModelInfo*) payload;

				sender->taskType = mi->taskType;
				sender->columnsInResult = mi->columnsCount;
				fprintf(stderr, "Model info: task type: %u, result columns: %u\n",
					   sender->taskType, sender->columnsInResult);

				if (sender->columnsInResult == 0)
				{
					fprintf(stderr, "%s: invalid columns count\n", __func__);
					sender_finish(sender);
					return;
				}

				state_transition(sender, STATE_SEND_DATASET_INFO);
				return;
			}
		}

		if (++sender->retries > sender->maxRetries)
		{
			fprintf(stderr, "%s: timeout get model info\n", __func__);
			sender_finish(sender);
			return;
		}

		uv_buf_t buf = alloc_buffer(sender);

		fprintf(stderr, ">> Request model info\n");

		make_packet(sender, &buf, 0, TYPE_MODEL_INFO, ERROR_SUCCESS);
		send_packet(sender, buf);
	}
	else if (sender->state == STATE_SEND_DATASET_INFO)
	{
		if (in_packet && PACKET_TYPE(in_packet->type) == TYPE_DATASET_INFO)
		{
			fprintf(stderr, "Dataset info: columns in sample: %u\n", sender->columnsInSample);
			state_transition(sender, STATE_SEND_SAMPLES);
			return;
		}

		if (++sender->retries > sender->maxRetries)
		{
			fprintf(stderr, "%s: timeout send dataset info\n", __func__);
			sender_finish(sender);
			return;
		}

		uv_buf_t buf = alloc_buffer(sender);

		DatasetInfo* di = (DatasetInfo*) (buf.base + sizeof(PacketHeader));
		di->columnsCount = sender->columnsInSample;
		di->reverseByteOrder = 0;

		fprintf(stderr, ">> Send dataset info: columns in sample: %u\n", di->columnsCount);

		make_packet(sender, &buf, sizeof(DatasetInfo), TYPE_DATASET_INFO, ERROR_SUCCESS);
		send_packet(sender, buf);
	}
	else if (sender->state == STATE_SEND_SAMPLES)
	{
		if (in_packet && PACKET_TYPE(in_packet->type) == TYPE_DATASET_SAMPLE)
		{
			if (in_packet->size >= (sizeof(float) * sender->columnsInResult))
			{
				static uint8_t hasHeader = 0;
				
				if (sender->sampleSent)
				{
					float* result = (float*) payload;

					if (!hasHeader)
					{
						hasHeader = 1;
						
						if (sender->taskType == 2)
						{
							if (sender->columnsInResult == 1)
							{
								printf("target\n");
							}
							else
							{
								for (uint16_t i = 0; i < sender->columnsInResult; ++i)
								{
									printf("Predicted value for output #%u%s",
											i + 1, (i + 1) < sender->columnsInResult ? "," : "");
								}
								printf("\n");
							}
						}
						else
						{
							printf("target%s", sender->columnsInResult > 1 ? "," : "");
							for (uint16_t i = 0; i < sender->columnsInResult; ++i)
							{
								printf("Probability of %d%s",
										i, (i + 1) < sender->columnsInResult ? "," : "");
							}
							printf("\n");
						}
					}

					if (sender->taskType < 2)
					{
						uint32_t index = 0;
						float max = 0;
						for (uint32_t i = 0; i < sender->columnsInResult; i++)
						{
							if (max < result[i])
							{
								index = i;
								max = result[i];
							}
						}
							
						printf("%u,", index);
					}

					for (uint32_t i = 0; i < sender->columnsInResult; i++)
						printf("%.6f%s", result[i], (i+1) < sender->columnsInResult ? "," : "");
					printf("\n");
				}
			}
			
			sender->retries = 0;
			if (sender->sampleSent)
			{
				// static size_t nSamples = 1;
				// fprintf(stderr, ">> Send dataset sample: #%zu\n", nSamples++);

				if (0 == sender_read_sample(sender))
				{
					fprintf(stderr, "================\n");
					state_transition(sender, STATE_GET_PERFORMANCE_COUNTERS);
					return;
				}
			}
		}

		if (++sender->retries > sender->maxRetries)
		{
			fprintf(stderr, "%s: timeout sending sample(s)\n", __func__);
			sender_finish(sender);
			return;
		}

		if (!sender->sampleSent)
		{
			if (0 == sender_read_sample(sender))
			{
				fprintf(stderr, "%s: failed to read sample\n", __func__);
				sender_finish(sender);
				return;
			}

			sender->sampleSent = 1;
		}

		uv_buf_t buf = alloc_buffer(sender);

		float* data = (float*) (buf.base + sizeof(PacketHeader));
		memcpy(data, sender->sample, sender->sampleSize);

		make_packet(sender, &buf, sender->sampleSize, TYPE_DATASET_SAMPLE, ERROR_SUCCESS);
		send_packet(sender, buf);
	}
	else if (sender->state == STATE_GET_PERFORMANCE_COUNTERS)
	{
		if (in_packet && PACKET_TYPE(in_packet->type) == TYPE_PERF_REPORT)
		{
			if (in_packet->size >= sizeof(PerformanceReport))
			{
				PerformanceReport* pi = (PerformanceReport*) payload;

				fprintf(stderr,
					   "Resource report:\n"
					   "       CPU freq: %u\n"
					   "    Flash usage: %u\n"
					   "RAM usage total: %u\n"
					   "      RAM usage: %u\n"
					   "    UART buffer: %u\n"
					   "\n"
					   "Performance report:\n"
					   "Sample calc time, avg: %3.1f us\n"
					   "Sample calc time, min: %3.1f us\n"
					   "Sample calc time, max: %3.1f us\n"
					   "================\n",
					   pi->freq, pi->flashUsage, pi->ramUsage, pi->ramUsageCur, pi->bufferSize,
					   pi->usSampleAvg, pi->usSampleMin, pi->usSampleMax);

				state_transition(sender, STATE_SHUTDOWN);
				return;
			}
		}

		if (++sender->retries > sender->maxRetries)
		{
			fprintf(stderr, "%s: timeout get performace report\n", __func__);
			sender_finish(sender);
			return;
		}

		uv_buf_t buf = alloc_buffer(sender);

		fprintf(stderr, ">> Request performance report\n");

		make_packet(sender, &buf, 0, TYPE_PERF_REPORT, ERROR_SUCCESS);
		send_packet(sender, buf);
	}
	else if (sender->state == STATE_SHUTDOWN)
	{
		sender_finish(sender);
	}
}
