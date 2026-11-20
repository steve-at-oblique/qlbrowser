#ifndef BROWSER_H
#define BROWSER_H

#define URL_SIZE 1024
#define HOST_SIZE 129
#define BODY_LIMIT 262144
#define HTTPS_HELPER_PORT 8787
#define WIDTH 79
#define MAX_WIDTH 511
#define PAGE_LINES 19

struct url {
    char host[HOST_SIZE];
    unsigned port;
    int secure;
    char target[URL_SIZE];
};

int parse_url(const char *input, struct url *url);
int resolve_url(const struct url *base, const char *link, char *output);
const char *fetch(struct url *url, char *address, char *body, int *html);

long tcp_open(const char *host, unsigned port);
int tcp_send(long channel, char *data, int length);
int tcp_byte(long channel);
void tcp_close(long channel);

extern int screen_width, screen_lines;

const char *console_pointer_status(void);
int console_open(void);
void console_close(void);
void console_clear(void);
void console_underline(int enabled);
void console_write(const char *data, int length);
/* With a click callback, an empty command accepts browser navigation shortcuts.
   The callback handles left clicks at zero-based character coordinates. */
int console_line(char *buffer, int size, int (*click)(int, int, char *));

#define TCP_EOF (-1)
#define TCP_ERROR (-2)
#define TCP_TIMEOUT (-3)

#endif
