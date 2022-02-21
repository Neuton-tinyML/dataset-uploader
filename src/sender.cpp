#include <stdlib.h>
#include <string.h>

#include "protocol.h"
#include "sender.h"
#include "sender_fsm.h"
#include "parser.h"
#include "simple_csv.h"


static int sender_read(Sender* sender);

static Sender* instance = NULL;


static int set_interface_attribs(int fd, int speed, int parity, int stop)
{
	struct termios tty;
	memset (&tty, 0, sizeof tty);

	if (tcgetattr (fd, &tty) != 0)
		return -1;

	cfsetospeed (&tty, speed);
	cfsetispeed (&tty, speed);

	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
	// disable IGNBRK for mismatched speed tests; otherwise receive break
	// as \000 chars
	tty.c_iflag &= ~IGNBRK;         // disable break processing
	tty.c_lflag = 0;                // no signaling chars, no echo,
	// no canonical processing
	tty.c_oflag = 0;                // no remapping, no delays
	tty.c_cc[VMIN]  = 0;            // read doesn't block
	tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

	tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

	tty.c_iflag &= ~(ICRNL | INLCR); // disable input translating

	tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
	// enable reading
	tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
	tty.c_cflag |= parity;
	if (stop) tty.c_cflag |= CSTOPB;
	else tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CRTSCTS;

	if (tcsetattr (fd, TCSANOW, &tty) != 0)
		return -1;

	return 0;
}


static void set_blocking(int fd, uint8_t vmin, uint8_t vtime)
{
	struct termios tty;
	memset (&tty, 0, sizeof tty);

	if (tcgetattr (fd, &tty) != 0)
		return;

	tty.c_cc[VMIN]  = vmin;
	tty.c_cc[VTIME] = vtime;

	tcsetattr (fd, TCSANOW, &tty);
}


static void on_valid_packet(void* data, uint32_t size)
{
	PacketHeader* hdr = (PacketHeader*) data;
	sender_fsm(instance, NULL, hdr, size);
}


static int sender_init_uv_handles(Sender* sender, uint8_t isUdp, int bindPort, int sendPort,
								  const char* serial, int speed)
{
	uv_timer_t* timer = (uv_timer_t*) calloc(1, sizeof(uv_timer_t));
	if (!timer)
		return 1;

	sender->timer = timer;
	sender->timer->data = sender;
	if (0 != uv_timer_init(uv_default_loop(), timer))
	{
		fprintf(stderr, "Failed to init timer\n");
		return 2;
	}

	sender->isUdp = isUdp;

	if (sender->isUdp)
	{
		uv_udp_t* sock = (uv_udp_t*) calloc(1, sizeof(uv_udp_t));
		if (!sock)
			return 3;

		sender->socket = sock;
		sender->socket->data = sender;
		if (0 != uv_udp_init(uv_default_loop(), sock))
		{
			fprintf(stderr, "Failed to init socket\n");
			return 4;
		}

		uv_ip4_addr("127.0.0.1", sendPort, &sender->addr);

		struct sockaddr_in addr;
		uv_ip4_addr("127.0.0.1", bindPort, &addr);
		if (0 != uv_udp_bind(sock, (const struct sockaddr*) &addr, 0))
		{
			fprintf(stderr, "Failed to bind port %u\n", bindPort);
			return 5;
		}
	}
	else
	{
		uv_fs_t open_req = { 0 };
		int fd = sender->fd = uv_fs_open(uv_default_loop(), &open_req, serial, UV_FS_O_NOCTTY | UV_FS_O_RDWR, 0, NULL);
		if (0 > fd)
		{
			fprintf(stderr, "Failed to open %s: %s\n", serial, uv_strerror(fd));
			return 3;
		}

		set_blocking(fd, 0, 0);
		set_interface_attribs(fd, speed, 0, 1);
	}

	if (0 != parser_init(on_valid_packet))
	{
		fprintf(stderr, "Failed to init parser\n");
		return 6;
	}

	return 0;
}


Sender* sender_create(uint8_t isUdp, const char* dataset, int bindPort, int sendPort,
					  const char* serial, int speed)
{
	SimpleCsvReader *csvReader;

	try 
	{
		csvReader = new SimpleCsvReader(dataset);
	}
	catch (std::exception&)
	{
		fprintf(stderr, "File not found: %s\n", dataset);
		return NULL;
	}

	Sender* sender = (Sender*) calloc(1, sizeof(Sender));
	if (!sender)
		return NULL;

	if (0 != sender_init_uv_handles(sender, isUdp, bindPort, sendPort, serial, speed))
	{
		sender_destroy(sender);
		return NULL;
	}

	sender->csvReader = csvReader;

	auto header = csvReader->GetParcedLine<std::string>();
	if (!header.size())
	{
		fprintf(stderr, "Nothing to send: empty file\n");
		sender_destroy(sender);
		free(sender);
		return NULL;
	}

	sender->columnsInSample = header.size() + 1;
	sender->sampleSize = (sender->columnsInSample) * sizeof(float);
	sender->sample = (float*) calloc(sender->columnsInSample, sizeof(float));
	if (!sender->sample)
	{
		fprintf(stderr, "Failed to alloc sample buffer\n");
		sender_destroy(sender);
		free(sender);
		return NULL;
	}

	return sender;

err:

	fprintf(stderr, "Error parsing dataset file\n");
	sender_destroy(sender);
	free(sender);

	return NULL;
}

void sender_destroy(Sender* sender)
{
	sender_finish(sender);

	if (!sender)
		return;

	if (sender->timer)
		free(sender->timer);

	if (sender->socket)
		free(sender->socket);

	if (sender->sample)
		free(sender->sample);
		
	if (sender->csvReader)
		delete sender->csvReader;

	memset(sender, 0, sizeof(Sender));
}


static void sender_on_recv(uv_udp_t* handle, ssize_t nRead, const uv_buf_t* buffer,
						   const struct sockaddr* addr, unsigned flags)
{
	if (nRead <= 0 || !buffer)
		goto _free;

	for (ssize_t i = 0; i < nRead; i++)
		parser_parse(buffer->base[i]);

_free:

	if (buffer && buffer->base)
		free(buffer->base);
}


uint8_t sender_read_sample(Sender* sender)
{
	if (!sender)
		return 0;

	auto values = sender->csvReader->GetParcedLine<float>();
	if (values.size() == 0)
		return 0;

	if (sender->columnsInSample != (values.size() + 1))
	{
		fprintf(stderr, "%s: failed to read sample\n", __func__);
		return 0;
	}

	for (uint32_t i = 0; i < values.size(); ++i)
		sender->sample[i] = values[i];
	
	sender->sample[values.size()] = 1.0;

	return 1;
}


static void sender_alloc_rx_buffer(uv_handle_t* handle, size_t requestedSize, uv_buf_t* buffer)
{
	if (!buffer)
		return;

	buffer->base = (char*) calloc(1, requestedSize);
	buffer->len =requestedSize;

	if (!buffer->base)
	{
		fprintf(stderr, "Failed to allocate recv buffer\n");
		memset(buffer, 0, sizeof(uv_buf_t));
	}
}


static void sender_fs_read(uv_fs_t *req)
{
	Sender* sender = (Sender*) req->data;
	ssize_t result = req->result;

	if (result > 0)
	{
		for (ssize_t i = 0; i < result; i++)
			parser_parse(req->bufsml[0].base[i]);
	}

	for (uint32_t i = 0; i < (sizeof(req->bufsml) / sizeof(req->bufsml[0])); i++)
		if (req->bufsml[i].base)
			free(req->bufsml[i].base);

	free(req);

	if (result < 0)
		return;

	if (0 != sender_read(sender))
	{
		fprintf(stderr, "%s: failed to read serial port\n", __func__);
		sender_finish(sender);
	}

	return;
}


static int sender_read(Sender* sender)
{
	uv_buf_t buf;
	sender_alloc_rx_buffer(NULL, 2048, &buf);
	if (!buf.base)
	{
		fprintf(stderr, "Failed to allocate recv buffer\n");
		sender_finish(sender);
		return 1;
	}

	uv_fs_t* req = (uv_fs_t*) calloc(1, sizeof(uv_fs_t));
	if (!req)
	{
		fprintf(stderr, "Failed to allocate read request\n");
		sender_finish(sender);
		return 2;
	}

	req->data = sender;

	int res = uv_fs_read(uv_default_loop(), req, sender->fd, &buf, 1, -1, sender_fs_read);
	if (0 != res)
	{
		fprintf(stderr, "%s: %s\n", __func__, uv_strerror(res));
		sender_finish(sender);
		return 3;
	}

	sender->read_request = req;

	return 0;
}


int sender_run(Sender* sender, uint32_t delay)
{
	if (!sender)
		return 1;

	sender->error = 1;
	instance = sender;

	if (sender->isUdp)
	{
		if (0 != uv_udp_recv_start(sender->socket, sender_alloc_rx_buffer, sender_on_recv))
		{
			fprintf(stderr, "Failed to start recv\n");
			return 2;
		}
	}
	else
	{
		if (0 != sender_read(sender))
			return 3;
	}

	sender->retries = 0;
	sender->maxRetries = 3;

	state_transition_delayed(sender, STATE_GET_MODEL_INFO, delay);

	sender->error = 0;



	return uv_run(uv_default_loop(), UV_RUN_DEFAULT);
}

void sender_finish(Sender* sender)
{
	if (!sender)
		return;

	if (sender->timer)
	{
		uv_timer_stop(sender->timer);
		uv_unref((uv_handle_t*) sender->timer);
	}

	if (sender->socket)
	{
		uv_udp_recv_stop(sender->socket);
		uv_unref((uv_handle_t*) sender->socket);
	}

	if (sender->fd > 0)
	{
		uv_fs_t req = { 0 };
		uv_fs_close(uv_default_loop(), &req, sender->fd, NULL);
	}

	if (sender->state != STATE_SHUTDOWN)
		sender->error = 1;
}
