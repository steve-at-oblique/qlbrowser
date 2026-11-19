#include "browser.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BAD_LINE (-4)
#define MAX_REDIRECTS 5

static int equal(const char *a, const char *b)
{
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        ++a;
        ++b;
    }
    return !*a && !*b;
}

int parse_url(const char *input, struct url *url)
{
    const char *host, *p, *end;
    unsigned long port;

    if (strlen(input) >= URL_SIZE)
        return 0;
    url->secure = !strncmp(input, "https://", 8);
    if (!url->secure && strncmp(input, "http://", 7))
        return 0;
    for (p = input; *p; ++p)
        if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127 || *p == '\\')
            return 0;
    host = input + 7 + url->secure;
    p = host;
    while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
           (*p >= '0' && *p <= '9') || *p == '.' || *p == '-')
        ++p;
    if (p == host || p - host >= HOST_SIZE)
        return 0;
    memcpy(url->host, host, p - host);
    url->host[p - host] = 0;
    port = url->secure ? 443 : 80;
    if (*p == ':') {
        ++p;
        if (!isdigit((unsigned char)*p))
            return 0;
        port = 0;
        while (isdigit((unsigned char)*p)) {
            port = port * 10 + (*p++ - '0');
            if (port > 65535)
                return 0;
        }
    }
    if (!port || (*p && *p != '/' && *p != '?' && *p != '#'))
        return 0;
    url->port = (unsigned)port;
    end = strchr(p, '#');
    if (!end)
        end = p + strlen(p);
    if (*p != '/') {
        url->target[0] = '/';
        memcpy(url->target + 1, p, end - p);
        url->target[1 + (end - p)] = 0;
    } else {
        memcpy(url->target, p, end - p);
        url->target[end - p] = 0;
    }
    return 1;
}

/* Normalize relative paths without changing the query or percent escapes. */
static void normalize_path(char *path)
{
    char *read = path, *write = path, *end;
    int size;

    while (*read == '/') {
        end = read + 1 + strcspn(read + 1, "/?");
        size = end - read;
        if (size == 3 && !memcmp(read, "/..", 3)) {
            while (write > path && *--write != '/')
                ;
            if (*end != '/')
                *write++ = '/';
        } else if (size == 2 && read[1] == '.') {
            if (*end != '/')
                *write++ = '/';
        } else {
            memmove(write, read, size);
            write += size;
        }
        read = end;
    }
    memmove(write, read, strlen(read) + 1);
}

int resolve_url(const struct url *base, const char *link, char *output)
{
    struct url checked;
    char origin[HOST_SIZE + 16], path[URL_SIZE];
    const char *end;
    int prefix = 0, length;

    length = strcspn(link, "#");
    if (!length)
        return 0;
    if (!strncmp(link, "http://", 7) || !strncmp(link, "https://", 8) ||
        !strncmp(link, "//", 2)) {
        prefix = link[0] == '/' ? 5 + base->secure : 0;
        if (prefix + length >= URL_SIZE)
            return 0;
        memcpy(output, base->secure ? "https:" : "http:", prefix);
        memcpy(output + prefix, link, length);
        output[prefix + length] = 0;
        return parse_url(output, &checked);
    }
    end = link + strcspn(link, ":/?#");
    if (*end == ':')
        return 0;
    sprintf(origin, "%s://%s", base->secure ? "https" : "http", base->host);
    if (base->port != (base->secure ? 443 : 80))
        sprintf(origin + strlen(origin), ":%u", base->port);
    if (*link != '/') {
        prefix = strcspn(base->target, "?");
        if (*link != '?')
            while (prefix > 0 && base->target[prefix - 1] != '/')
                --prefix;
    }
    if (prefix + length >= URL_SIZE)
        return 0;
    memcpy(path, base->target, prefix);
    memcpy(path + prefix, link, length);
    path[prefix + length] = 0;
    normalize_path(path);
    if (strlen(origin) + strlen(path) >= URL_SIZE)
        return 0;
    strcpy(output, origin);
    strcat(output, path);
    return parse_url(output, &checked);
}

static const char *stream_error(int status)
{
    if (status == TCP_TIMEOUT)
        return "Timed out waiting for HTTP data.";
    if (status == TCP_EOF)
        return "HTTP response ended early.";
    return "TCP read failed.";
}

static int read_line(long channel, char *line, int size)
{
    int c, n = 0;

    while ((c = tcp_byte(channel)) >= 0) {
        if (c == '\n') {
            if (!n || line[n - 1] != '\r')
                return BAD_LINE;
            --n;
            line[n] = 0;
            return n;
        }
        if (!c || n == size - 1)
            return BAD_LINE;
        line[n++] = (char)c;
    }
    return c;
}

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = tolower((unsigned char)c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static const char *read_chunked(long channel, char *body, char *line, int size)
{
    unsigned long count;
    int n, c, digit, used = 0, trailers = 0;
    char *p;

    for (;;) {
        n = read_line(channel, line, size);
        if (n < 0)
            return n == BAD_LINE ? "Invalid HTTP chunk header." : stream_error(n);
        p = line;
        count = 0;
        while ((digit = hex_digit(*p)) >= 0) {
            count = count * 16 + digit;
            if (count > (unsigned long)(BODY_LIMIT - used))
                return "Page exceeds 32 KiB.";
            ++p;
        }
        if (p == line)
            return "Invalid HTTP chunk size.";
        p += strspn(p, " \t");
        if (*p && *p != ';')
            return "Invalid HTTP chunk size.";
        if (!count)
            break;
        while (count--) {
            c = tcp_byte(channel);
            if (c < 0)
                return stream_error(c);
            body[used++] = c ? (char)c : ' ';
        }
        c = tcp_byte(channel);
        if (c < 0)
            return stream_error(c);
        if (c != '\r')
            return "Invalid HTTP chunk terminator.";
        c = tcp_byte(channel);
        if (c < 0)
            return stream_error(c);
        if (c != '\n')
            return "Invalid HTTP chunk terminator.";
    }
    while ((n = read_line(channel, line, size)) > 0) {
        trailers += n + 2;
        if (trailers > 8192 || !strchr(line, ':'))
            return "Invalid or oversized HTTP trailers.";
    }
    if (n < 0)
        return n == BAD_LINE ? "Invalid HTTP trailer." : stream_error(n);
    body[used] = 0;
    return NULL;
}

static const char *response(long channel, char *body, int *html, char *location, int secure)
{
    char line[1024], *value, *end;
    long length = -1, parsed;
    int n, c, used = 0, status, headers = 0, redirect, chunked = 0;

    n = read_line(channel, line, sizeof(line));
    if (n < 0)
        return n == BAD_LINE ? "Invalid HTTP status line." : stream_error(n);
    if (n < 12 || strncmp(line, "HTTP/1.", 7) || line[8] != ' ' ||
        !isdigit((unsigned char)line[9]) || !isdigit((unsigned char)line[10]) ||
        !isdigit((unsigned char)line[11]) || (line[12] && line[12] != ' '))
        return "Invalid HTTP status line.";
    status = atoi(line + 9);
    redirect = status == 301 || status == 302 || status == 303 ||
               status == 307 || status == 308;
    if (secure && status == 502)
        return "HTTPS helper could not load the remote page.";
    if (secure && status == 413)
        return "Page exceeds 32 KiB.";
    if (status != 200 && !redirect) {
        sprintf(body, "HTTP status %d. This build expects 200 OK.", status);
        return body;
    }
    *html = 1;
    while ((n = read_line(channel, line, sizeof(line))) > 0) {
        headers += n + 2;
        if (headers > 8192)
            return "HTTP headers exceed 8 KiB.";
        value = strchr(line, ':');
        if (!value)
            return "Invalid HTTP header.";
        *value++ = 0;
        while (*value == ' ' || *value == '\t')
            ++value;
        end = value + strlen(value);
        while (end > value && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = 0;
        if (redirect) {
            if (equal(line, "Location")) {
                if (*location || !*value || strlen(value) >= URL_SIZE)
                    return "Invalid HTTP redirect location.";
                strcpy(location, value);
            }
        } else if (equal(line, "Content-Length")) {
            parsed = strtol(value, &end, 10);
            if (!isdigit((unsigned char)*value) || *end || parsed < 0 ||
                (length >= 0 && length != parsed))
                return "Invalid HTTP Content-Length.";
            length = parsed;
        } else if (equal(line, "Transfer-Encoding")) {
            if (chunked || !equal(value, "chunked"))
                return "Unsupported HTTP transfer encoding.";
            chunked = 1;
        } else if (equal(line, "Content-Encoding") && !equal(value, "identity")) {
            return "Unsupported HTTP content encoding.";
        } else if (equal(line, "Content-Type")) {
            end = value;
            while (*end && *end != ';' && *end != ' ' && *end != '\t')
                ++end;
            *end = 0;
            *html = equal(value, "text/html");
            if (!*html && !equal(value, "text/plain")) {
                /* Keep remote control bytes out of the console. */
                for (end = value; *end; ++end)
                    if ((unsigned char)*end < 32 || (unsigned char)*end > 126)
                        *end = '?';
                sprintf(body, "Unsupported Content-Type: %.28s",
                        *value ? value : "(empty)");
                return body;
            }
        }
    }
    if (n < 0)
        return n == BAD_LINE ? "Invalid or oversized HTTP header." : stream_error(n);
    if (redirect)
        return *location ? NULL : "HTTP redirect has no Location header.";
    if (chunked) {
        if (length >= 0)
            return "Conflicting HTTP body length headers.";
        return read_chunked(channel, body, line, sizeof(line));
    }
    if (length > BODY_LIMIT)
        return "Page exceeds 32 KiB.";

    while (length < 0 || used < length) {
        c = tcp_byte(channel);
        if (c == TCP_EOF && length < 0)
            break;
        if (c < 0)
            return stream_error(c);
        if (used == BODY_LIMIT)
            return "Page exceeds 32 KiB.";
        body[used++] = c ? (char)c : ' ';
    }
    body[used] = 0;
    return NULL;
}

static const char *fetch_once(const struct url *url, char *body, int *html,
                              char *location)
{
    char request[URL_SIZE + HOST_SIZE + 120], host[HOST_SIZE + 6];
    const char *error;
    long channel;

    channel = url->secure ? tcp_open("127.0.0.1", HTTPS_HELPER_PORT)
                          : tcp_open(url->host, url->port);
    if (channel < 0) {
        if (url->secure)
            return "Cannot connect to the HTTPS helper on port 8787.";
        sprintf(body, "TCP open failed (%ld). Check the driver, daemon and URL.", channel);
        return body;
    }
    if (url->port == (url->secure ? 443 : 80))
        strcpy(host, url->host);
    else
        sprintf(host, "%s:%u", url->host, url->port);
    sprintf(request,
            "GET %s%s%s HTTP/1.1\r\nHost: %s\r\n"
            "Connection: close\r\nAccept-Encoding: identity\r\n\r\n",
            url->secure ? "https://" : "", url->secure ? host : "",
            url->target, host);
    if (tcp_send(channel, request, strlen(request)) < 0)
        error = "TCP write failed.";
    else
        error = response(channel, body, html, location, url->secure);
    tcp_close(channel);
    return error;
}

const char *fetch(struct url *url, char *address, char *body, int *html)
{
    char location[URL_SIZE];
    const char *error;
    int redirects = 0;

    for (;;) {
        location[0] = 0;
        error = fetch_once(url, body, html, location);
        if (error || !*location)
            return error;
        if (redirects++ == MAX_REDIRECTS)
            return "Too many HTTP redirects.";
        if (!resolve_url(url, location, address) || !parse_url(address, url))
            return "Invalid or unsupported HTTP redirect.";
    }
}
