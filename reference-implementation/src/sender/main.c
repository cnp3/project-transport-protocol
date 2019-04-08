#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>

#include "transmit.h"

#include "../common/macros.h"
#include "../common/net.h"
#include "../common/pktbuf.h"

PRIVATE void usage(const char* argv)
{
    LOG("Usage: %s [OPTIONS] hostname port\n"
		"Where OPTIONS are:\n"
		"\t--buf, -b, [BUFSIZE] Limit the send buffer to [BUFSIZE] slots.\n"
		"\t--filename, -f, [FILE] Send the content of [FILE], otherwise, send "
		"the content of stdin.\n", argv);
    exit(EXIT_SUCCESS);
}

PRIVATE struct option long_opts[] = {
    {"filename", required_argument, 0, 'f'},
    {"buf", required_argument, 0, 'b'},
    {0, 0, 0, 0}
};

PRIVATE int parse_options(int argc, char** argv, FILE **f,
        char **host, char **port, const char *fmask, int *buf_size)
{
    int c, option_index;
    option_index = 0;
    while (1) {
        c = getopt_long(argc, argv, "f:b:", long_opts, &option_index);
        if (c == -1)
            break;
        switch (c) {
            case 'f':
                *f = fopen(optarg, fmask);
                if (!*f) {
                    ERROR("Cannot read the content of %s: %s", optarg,
							strerror(errno));
                    return errno;
                }
                LOG("Sending the content of %s\n", optarg);
                break;
			case 'b':
				*buf_size = atoi(optarg);
				LOG("Setting send buffer size to %u", *buf_size);
				break;
            default:
                usage(argv[0]);
                break;
        }
    }

    if (argc >= optind + 2) {
		*host = argv[optind];
		*port = argv[optind + 1];
	}

    return 0;
}

int main(int argc, char** argv)
{
    pktbuf_t *buf;
    char *host = "::1", *port = "1341";
    int err = -ENOMEM;
    FILE *in = stdin;
	int buf_size = 32;

    if ((err = parse_options(argc, argv, &in, &host, &port, "r", &buf_size)))
        goto exit;

    if (!(buf = pktbuf_new(buf_size)))
        goto_trace(err_options, "Failed to allocate buffer");

    if ((err = net_open_socket(host, port, &connect)) != NET_OK)
        goto_trace(err_buffer, "Failed to resolve receiver address");

    if ((err = transmit(fileno(in), buf)))
        trace("A transmission error occured");

    net_close_socket();

err_buffer:
    pktbuf_free(buf);
err_options:
    if (in != stdin)
        fclose(in);
exit:
    return err;
}


