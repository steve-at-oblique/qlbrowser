#include "browser.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINKS 99
#define HISTORY 16
#define RENDER_SIZE (BODY_LIMIT * 2 + MAX_LINKS * 6 + 1)

int screen_width = WIDTH, screen_lines = PAGE_LINES;

static char body[BODY_LIMIT + 1];
struct page {
    char rendered[RENDER_SIZE];
    char links[MAX_LINKS][URL_SIZE];
    int link_start[MAX_LINKS], link_end[MAX_LINKS], link_count;
};
static struct page pages[2];
static struct page *page = &pages[0], *previous_page;
static const char *visible_start, *visible_end;
static const struct {
    const char *label;
    char key;
} navigation[] = {
    {"[Back]", 'b'}, {"[Next]", 'n'}, {"[Prev]", 'p'}
};
static char history[HISTORY][URL_SIZE];
static int history_scroll[HISTORY];
static char word[MAX_WIDTH];
static int word_length;
static int cursor, column, scroll, depth, pending_space;

static void show(const char *text)
{
    console_write(text, strlen(text));
}

static void line_break(void)
{
    pending_space = 0;
    if (column) {
        page->rendered[cursor++] = '\n';
        column = 0;
    }
}

static void emit(int c)
{
    if (column == screen_width)
        line_break();
    page->rendered[cursor++] = (char)c;
    ++column;
}

static void flush_word(void)
{
    int i;

    if (!word_length)
        return;
    if (column && column + pending_space + word_length > screen_width)
        line_break();
    if (column && pending_space)
        emit(' ');
    pending_space = 0;
    for (i = 0; i < word_length; ++i)
        emit(word[i]);
    word_length = 0;
}

static void newline(void)
{
    flush_word();
    line_break();
}

static void text_byte(int c)
{
    if (isspace((unsigned char)c)) {
        flush_word();
        pending_space = column != 0;
        return;
    }
    if (word_length == screen_width)
        flush_word();
    word[word_length++] = c >= 32 && c < 127 ? c : '?';
}

static int name_is(const char *p, int n, const char *name)
{
    if (strlen(name) != (size_t)n)
        return 0;
    while (n--)
        if (tolower((unsigned char)*p++) != *name++)
            return 0;
    return 1;
}

static int entity(const char **p)
{
    static const char *names[] = {"amp", "lt", "gt", "quot", "apos", "nbsp"};
    static const char values[] = "&<>\"' ";
    const char *start = *p, *end = start + 1;
    char *number_end;
    long value;
    unsigned i;

    while (end - start <= 12 && (*end == '#' || isalnum((unsigned char)*end)))
        ++end;
    if (*end != ';' || end == start + 1)
        return '&';
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (name_is(start + 1, end - start - 1, names[i])) {
            *p = end;
            return values[i];
        }
    }
    if (start[1] == '#') {
        if (start[2] == 'x' || start[2] == 'X')
            value = strtol(start + 3, &number_end, 16);
        else
            value = strtol(start + 2, &number_end, 10);
        if (number_end == end) {
            *p = end;
            return value == 160 ? ' ' : value >= 32 && value < 127 ? (int)value : '?';
        }
    }
    return '&';
}

static int add_link(const struct url *base, const char *p, const char *end)
{
    const char *name, *start, *stop;
    char href[URL_SIZE], marker[8];
    int n, quote;

    if (page->link_count == MAX_LINKS)
        return 0;
    while (p < end) {
        while (p < end && isspace((unsigned char)*p))
            ++p;
        name = p;
        while (p < end && !isspace((unsigned char)*p) && *p != '=')
            ++p;
        n = p - name;
        while (p < end && isspace((unsigned char)*p))
            ++p;
        if (p == end || *p != '=')
            continue;
        ++p;
        while (p < end && isspace((unsigned char)*p))
            ++p;
        quote = p < end && (*p == '\'' || *p == '"') ? *p++ : 0;
        start = p;
        while (p < end && (quote ? *p != quote : !isspace((unsigned char)*p)))
            ++p;
        stop = p;
        if (p < end && quote)
            ++p;
        if (!name_is(name, n, "href"))
            continue;
        n = 0;
        while (start < stop && n < URL_SIZE - 1) {
            href[n++] = *start == '&' ? entity(&start) : *start;
            ++start;
        }
        href[n] = 0;
        if (start == stop && resolve_url(base, href, page->links[page->link_count])) {
            flush_word();
            if (column && column + 5 > screen_width)
                newline();
            else if (column)
                emit(' ');
            page->link_start[page->link_count] = cursor;
            sprintf(marker, "[%d]", ++page->link_count);
            for (n = 0; marker[n]; ++n)
                emit(marker[n]);
            pending_space = 1;
            return page->link_count;
        }
        return 0;
    }
    return 0;
}

static void render_html(const struct url *base)
{
    const char *p = body, *name, *attrs, *end, *hidden = NULL;
    int closing, n, quote, c, active_link = 0;

    while (*p) {
        if (!strncmp(p, "<!--", 4)) {
            end = strstr(p + 4, "-->");
            if (!end)
                break;
            p = end + 3;
            continue;
        }
        if (*p != '<' || (!isalpha((unsigned char)p[1]) &&
                          p[1] != '!' && p[1] != '/')) {
            if (!hidden) {
                c = *p == '&' ? entity(&p) : (unsigned char)*p;
                text_byte(c);
            }
            ++p;
            continue;
        }
        closing = p[1] == '/';
        name = p + 1 + closing;
        attrs = name;
        while (isalnum((unsigned char)*attrs))
            ++attrs;
        n = attrs - name;
        quote = 0;
        for (end = attrs; *end; ++end) {
            if (quote) {
                if (*end == quote)
                    quote = 0;
            } else if (*end == '"' || *end == '\'') {
                quote = *end;
            } else if (*end == '>') {
                break;
            }
        }
        if (!*end)
            break;
        p = end + 1;
        if (hidden) {
            if (closing && name_is(name, n, hidden))
                hidden = NULL;
        } else if (!closing && name_is(name, n, "script")) {
            hidden = "script";
        } else if (!closing && name_is(name, n, "style")) {
            hidden = "style";
        } else if (!closing && name_is(name, n, "head")) {
            hidden = "head";
        } else if (name_is(name, n, "a")) {
            if (active_link) {
                flush_word();
                page->link_end[active_link - 1] = cursor;
                active_link = 0;
            }
            if (!closing)
                active_link = add_link(base, attrs, end);
        } else if (name_is(name, n, "p") || name_is(name, n, "br") ||
                   name_is(name, n, "div") || name_is(name, n, "li") ||
                   name_is(name, n, "ul") || name_is(name, n, "ol") ||
                   (n == 2 && tolower((unsigned char)*name) == 'h' &&
                    name[1] >= '1' && name[1] <= '6')) {
            newline();
        }
    }
    if (active_link) {
        flush_word();
        page->link_end[active_link - 1] = cursor;
    }
}

static void render(const struct url *url, int html)
{
    const unsigned char *p;

    cursor = column = pending_space = word_length = page->link_count = 0;
    if (html) {
        render_html(url);
    } else {
        for (p = (const unsigned char *)body; *p; ++p) {
            if (*p == '\n') {
                page->rendered[cursor++] = '\n';
                column = 0;
            } else if (*p != '\r') {
                emit(*p == '\t' ? ' ' : *p >= 32 && *p < 127 ? *p : '?');
            }
        }
    }
    newline();
    page->rendered[cursor] = 0;
}

static const char *at_line(int line)
{
    const char *p = page->rendered, *end;

    while (line-- > 0 && (end = strchr(p, '\n')) != NULL)
        p = end + 1;
    return p;
}

static void display(const char *message)
{
    static const char title[] = "qlbrowse " QLBROWSE_VERSION;
    char heading[MAX_WIDTH + 1];
    int limit;
    unsigned i;

    console_clear();
    visible_start = visible_end = page->rendered;
    show(title);
    if (depth) {
        limit = screen_width - (int)strlen(title) - 2;
        if (limit > 3) {
            if (strlen(history[depth - 1]) > (size_t)limit)
                sprintf(heading, "%.*s...", limit - 3, history[depth - 1]);
            else
                strcpy(heading, history[depth - 1]);
            show("  ");
            show(heading);
        }
    }
    show("\n");
    for (i = 0; i < sizeof(navigation) / sizeof(navigation[0]); ++i) {
        show(navigation[i].label);
        show(" ");
    }
    show(" O Open  Q Quit\n");
    if (depth) {
        visible_start = at_line(scroll);
        visible_end = at_line(scroll + screen_lines);
        console_write(visible_start, visible_end - visible_start);
        show(*visible_end ? "[More below]\n" : "\n");
    } else {
        show("Enter an HTTP or HTTPS URL.\n\n");
    }
    if (message) {
        limit = strlen(message);
        console_write(message, limit > screen_width ? screen_width : limit);
        show("\n");
    }
    show("URL or link number> ");
}

static int click_at(int col, int row, char *command)
{
    const char *line = visible_start, *end;
    int offset, i;

    if (row == 1) {
        unsigned button;
        int start = 0, end_col;

        for (button = 0; button < sizeof(navigation) / sizeof(navigation[0]); ++button) {
            end_col = start + strlen(navigation[button].label);
            if (col >= start && col < end_col) {
                command[0] = navigation[button].key;
                command[1] = 0;
                return 1;
            }
            start = end_col + 1;
        }
        return 0;
    }
    if (row < 2)
        return 0;
    for (i = 2; i < row && line < visible_end; ++i)
        line = strchr(line, '\n') + 1;
    if (line >= visible_end)
        return 0;
    end = strchr(line, '\n');
    if (col < 0 || col >= end - line)
        return 0;
    offset = line - page->rendered + col;
    for (i = 0; i < page->link_count; ++i) {
        if (offset >= page->link_start[i] && offset < page->link_end[i]) {
            sprintf(command, "%d", i + 1);
            return 1;
        }
    }
    return 0;
}

static const char *navigate(const char *address, int back)
{
    struct url url;
    char requested[URL_SIZE];
    const char *error;
    int html;

    if (back && previous_page) {
        page = previous_page;
        previous_page = NULL;
        --depth;
        scroll = history_scroll[depth - 1];
        return NULL;
    }
    if (!parse_url(address, &url))
        return "Enter a valid HTTP or HTTPS URL.";
    strcpy(requested, address);
    if ((error = fetch(&url, requested, body, &html)) != NULL)
        return error;
    if (!back && depth) {
        previous_page = page;
        page = page == &pages[0] ? &pages[1] : &pages[0];
    }
    render(&url, html);
    if (back) {
        --depth;
        strcpy(history[depth - 1], requested);
        scroll = history_scroll[depth - 1];
        while (scroll && !*at_line(scroll))
            scroll -= screen_lines;
    } else {
        if (depth)
            history_scroll[depth - 1] = scroll;
        if (depth == HISTORY) {
            memmove(history, history + 1, sizeof(history) - sizeof(history[0]));
            memmove(history_scroll, history_scroll + 1,
                    sizeof(history_scroll) - sizeof(history_scroll[0]));
            --depth;
        }
        strcpy(history[depth++], requested);
        scroll = 0;
    }
    return NULL;
}

int main(void)
{
    char input[URL_SIZE + 1], *end;
    const char *message = NULL;
    long choice;

    if (!console_open()) {
        puts("qlbrowse could not open its console.");
        return 1;
    }
    for (;;) {
        display(message);
        message = NULL;
        if (!console_line(input, sizeof(input), click_at) || name_is(input, strlen(input), "q"))
            break;
        if (!*input)
            continue;
        if (name_is(input, strlen(input), "o")) {
            show("HTTP URL> ");
            if (!console_line(input, sizeof(input), NULL) || !*input)
                continue;
            message = navigate(input, 0);
        } else if (name_is(input, strlen(input), "m")) {
            message = console_pointer_status();
        } else if (name_is(input, strlen(input), "b")) {
            if (depth < 2)
                message = "No earlier page.";
            else
                message = navigate(history[depth - 2], 1);
        } else if (name_is(input, strlen(input), "n")) {
            if (*at_line(scroll + screen_lines))
                scroll += screen_lines;
        } else if (name_is(input, strlen(input), "p")) {
            if (scroll)
                scroll -= screen_lines;
        } else {
            choice = strtol(input, &end, 10);
            if (end != input && !*end)
                message = choice > 0 && choice <= page->link_count
                          ? navigate(page->links[choice - 1], 0) : "Unknown link number.";
            else
                message = navigate(input, 0);
        }
    }
    console_close();
    return 0;
}
