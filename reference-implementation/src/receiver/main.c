#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include "receive.h"

#include "../common/macros.h"
#include "../common/pktbuf.h"
#include "../common/net.h"

PRIVATE void usage(const char* argv)
{
    LOG("Usage: %s [OPTIONS] hostname port\n"
		"Where OPTIONS are:\n"
		"\t--buf, -b, [BUFSIZE] Limit the receive buffer to [BUFSIZE] slots.\n"
		"\t--filename, -f, [FILE] Write the received data to [FILE], otherwise"
		" use stdout.\n", argv);
    exit(EXIT_SUCCESS);
}

PRIVATE struct option long_opts[] = {
    {"filename", required_argument, 0, 'f'},
    {"buf", required_argument, 0, 'b'},
    {0, 0, 0, 0}
};

PRIVATE int parse_options(int argc, char** argv, FILE **f,
        char **host, char **port, const char* fmask)
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
				max_window = atoi(optarg);
				if (max_window> MAX_WINDOW_SIZE)
					max_window = MAX_WINDOW_SIZE;
				LOG("Setting receive buffer size to %u", max_window);
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
    char *host = "::", *port = "1341";
    FILE *out = stdout;
    int err = -ENOMEM;

    if ((err = parse_options(argc, argv, &out, &host, &port, "w")))
        return err;

    if (!(buf = pktbuf_new(32)))
        goto_trace(err_file, "Cannot allocate the receive buffer");

    if (net_open_socket(host, port, &bind))
        goto_trace(err_buf, "Cannot open socket for the specified "
                "hostname/port");

    if ((err = receive(fileno(out), buf)))
		ERROR("A transmission error occured!");

	net_close_socket();
err_buf:
    pktbuf_free(buf);
err_file:
    if (out != stdout)
        fclose(out);

    return err;
}

