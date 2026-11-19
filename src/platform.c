#include "browser.h"
#include <stdio.h>
#include <string.h>

#ifdef QDOS
#include <qdos.h>
#include <qptr.h>

char _prog_name[] = "qlbrowse";
char *_endmsg = NULL;
long _stack = 8192;
static chanid_t console;

long tcp_open(const char *host, unsigned port)
{
    char device[HOST_SIZE + 11];

    sprintf(device, "TCP_%s:%u", host, port);
    return io_open(device, 0);
}

int tcp_send(long channel, char *data, int length)
{
    return io_sstrg(channel, 500, data, length) == length ? 0 : -1;
}

int tcp_byte(long channel)
{
    char byte;
    int status = io_fbyte(channel, 500, &byte);

    if (!status)
        return (unsigned char)byte;
    if (status == ERR_EF)
        return TCP_EOF;
    if (status == ERR_NC)
        return TCP_TIMEOUT;
    return TCP_ERROR;
}

void tcp_close(long channel)
{
    io_close(channel);
}

int console_open(void)
{
    QLRECT_t size;
    QLRECT_t window = {512, 256, 0, 0};
    WM_wsiz_t limits;

    /* Use the primary console already opened by the C runtime.
       A second console's pointer limits may be confined to the primary. */
    console = getchid(0);
    if (console < 0)
        return 0;
    /* On SMSQ/E a standalone console can use the whole display. */
    if (!iop_flim(console, -1, &limits) && limits.xsize > 0 && limits.ysize > 0) {
        window.q_width = limits.xsize;
        window.q_height = limits.ysize;
        window.q_x = limits.xorg;
        window.q_y = limits.yorg;
    }
    sd_wdef(console, -1, 0, 0, &window);
    sd_setsz(console, -1, 0, 0);
    sd_setpa(console, -1, 0);
    sd_setin(console, -1, 7);
    if (!sd_chenq(console, -1, &size)) {
        if (size.q_width > 5) {
            screen_width = size.q_width - 1;
            if (screen_width > MAX_WIDTH)
                screen_width = MAX_WIDTH;
        }
        if (size.q_height > 5)
            screen_lines = size.q_height - 5;
    }
    return 1;
}

void console_close(void)
{
    /* The C runtime owns the standard channels. */
}

void console_clear(void)
{
    sd_clear(console, -1);
}

void console_write(const char *data, int length)
{
    int count;

    while (length > 0) {
        count = io_sstrg(console, -1, (void *)data, length > 1024 ? 1024 : length);
        if (count <= 0)
            return;
        data += count;
        length -= count;
    }
}

int console_line(char *buffer, int size)
{
    char key;
    unsigned char code;
    int length = 0, status, overflow = 0;

    sd_cure(console, -1);
    while (!(status = io_fbyte(console, -1, &key))) {
        if (key == 27 || key == '\n' || key == '\r')
            break;
        code = (unsigned char)key;
        if (code == 8 || code == 127 || code == 194) {
            if (overflow) {
                --overflow;
            } else if (length) {
                --length;
                sd_pcol(console, -1);
                io_sbyte(console, -1, ' ');
                sd_pcol(console, -1);
            }
        } else if (code >= 32 && code < 127) {
            if (length < size - 1) {
                buffer[length++] = key;
                console_write(&key, 1);
            } else {
                ++overflow;
            }
        }
    }
    sd_curs(console, -1);
    console_write("\n", 1);
    buffer[length] = 0;
    if (status)
        return 0;
    if (key == 27 || overflow)
        buffer[0] = 0;
    return 1;
}

#else
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static unsigned char received[4096];
static int offset, available;

long tcp_open(const char *host, unsigned port)
{
    struct addrinfo hints, *addresses, *address;
    struct timeval wait;
    char service[6];
    int channel = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    sprintf(service, "%u", port);
    if (getaddrinfo(host, service, &hints, &addresses))
        return -1;
    wait.tv_sec = 10;
    wait.tv_usec = 0;
    for (address = addresses; address; address = address->ai_next) {
        channel = socket(address->ai_family, address->ai_socktype, 0);
        if (channel < 0)
            continue;
        setsockopt(channel, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
        setsockopt(channel, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait));
#ifdef SO_NOSIGPIPE
        {
            int yes = 1;
            setsockopt(channel, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
        }
#endif
        if (!connect(channel, address->ai_addr, address->ai_addrlen))
            break;
        close(channel);
        channel = -1;
    }
    freeaddrinfo(addresses);
    offset = available = 0;
    return channel;
}

int tcp_send(long channel, char *data, int length)
{
    int sent;
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    while (length) {
        sent = send(channel, data, length, flags);
        if (sent < 0 && errno == EINTR)
            continue;
        if (sent <= 0)
            return -1;
        data += sent;
        length -= sent;
    }
    return 0;
}

int tcp_byte(long channel)
{
    int n;

    if (offset == available) {
        do {
            n = recv(channel, received, sizeof(received), 0);
        } while (n < 0 && errno == EINTR);
        if (!n)
            return TCP_EOF;
        if (n < 0)
            return errno == EAGAIN || errno == EWOULDBLOCK ? TCP_TIMEOUT : TCP_ERROR;
        offset = 0;
        available = n;
    }
    return received[offset++];
}

void tcp_close(long channel)
{
    close(channel);
}

int console_open(void)
{
    return 1;
}

void console_close(void)
{
}

void console_clear(void)
{
    if (isatty(STDOUT_FILENO))
        fputs("\033[2J\033[H", stdout);
    else
        putchar('\n');
}

void console_write(const char *data, int length)
{
    fwrite(data, 1, length, stdout);
}

int console_line(char *buffer, int size)
{
    char *end;
    int c;

    fflush(stdout);
    if (!fgets(buffer, size, stdin))
        return 0;
    end = strchr(buffer, '\n');
    if (end)
        *end = 0;
    else {
        c = getchar();
        if (c != '\n' && c != EOF) {
            while ((c = getchar()) != '\n' && c != EOF)
                ;
            /* Reject a truncated command rather than navigating to it. */
            buffer[0] = 0;
        }
    }
    return 1;
}
#endif
