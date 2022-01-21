#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../cmdline.h"
#include "sender.h"


int main(int argc, char** argv)
{
	enum Interface { UDP, SERIAL } interface;

	const char* datasetFilename = NULL;
	const char* serialPort = NULL;

	struct gengetopt_args_info ai;

	if (cmdline_parser(argc, argv, &ai) != 0)
		return 1;

	interface = strcmp("udp", ai.interface_arg) == 0 ? UDP : SERIAL;
	datasetFilename = ai.dataset_arg;
	serialPort = ai.serial_port_arg;

	int bindPort = ai.listen_port_arg;
	int sendPort = ai.send_port_arg;

	int delay = ai.pause_arg;

	int speed = ai.baud_rate_arg;
	switch (speed)
	{
	case   9600:  speed = B9600;    break;
	case 115200:  speed = B115200;  break;
	case 230400:  speed = B230400;  break;
	default: return 1;
	}

	if (datasetFilename == NULL)
	{
		fprintf(stderr, "Dataset file required\n");
		return 1;
	}

	Sender *sender = sender_create(interface == UDP, datasetFilename, bindPort, sendPort,
									serialPort, speed);
	if (!sender)
	{
		fprintf(stderr, "Failed to create sender\n");
		return 1;
	}

	int res = sender_run(sender, delay);

	if (sender->error)
		res = sender->error;

	sender_destroy(sender);
	free(sender);

	return res;
}
