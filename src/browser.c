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
    int link_start[MAX_LINKS], link_end[MAX_LINKS];
    int marker_end[MAX_LINKS], link_count;
};
static struct page pages[2];
static struct page *page = &pages[0], *previous_page;
static const char *visible_start, *visible_end;
static const struct {
    const char *label;
    char key;
} navigation[] = {
    {"[Back]", 'b'}, {"[Next]", 'n'}, {"[Prev]", 'p'}, {"[Refresh]", 'r'}
};
static const char more_below[] = "[More below]";
static char history[HISTORY][URL_SIZE];
static int history_scroll[HISTORY];
static char word[MAX_WIDTH];
static int word_length, pending_marker, marker_only;
static int cursor, column, scroll, depth, pending_space, block_gap, list_bullet;

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

static void start_content(void)
{
    if (block_gap) {
        page->rendered[cursor++] = '\n';
        block_gap = 0;
    }
    if (list_bullet) {
        page->rendered[cursor++] = '-';
        page->rendered[cursor++] = ' ';
        column = 2;
        list_bullet = 0;
    }
}

static void emit(int c)
{
    start_content();
    if (column == screen_width)
        line_break();
    page->rendered[cursor++] = (char)c;
    ++column;
}

/* Keep the marker with its first word when both fit on a fresh line. */
static void emit_marker(int first_word)
{
    char marker[8];
    int length, i, link = pending_marker - 1;

    if (!pending_marker)
        return;
    length = sprintf(marker, "[%d]", pending_marker);
    if (column && column + 1 + length + (first_word ? 1 + first_word : 0) > screen_width)
        line_break();
    else if (column)
        emit(' ');
    start_content();
    page->link_start[link] = cursor;
    for (i = 0; i < length; ++i)
        emit(marker[i]);
    page->marker_end[link] = marker_only ? cursor : 0;
    pending_marker = 0;
    pending_space = 1;
}

static void flush_word(void)
{
    int i, starts_link = pending_marker;

    if (!word_length)
        return;
    emit_marker(word_length);
    if (!starts_link && column && column + pending_space + word_length > screen_width)
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
    emit_marker(0);
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

static long entity(const char **p)
{
    static const struct {
        const char *name;
        long value;
        int legacy;
    } entities[] = {
        {"amp", '&', 1}, {"lt", '<', 1}, {"gt", '>', 1}, {"quot", '"', 1},
        {"nbsp", ' ', 1}, {"copy", 0xa9, 1}, {"apos", 39, 0},
        {"lsquo", 0x2018, 0}, {"rsquo", 0x2019, 0}, {"ldquo", 0x201c, 0},
        {"rdquo", 0x201d, 0}, {"ndash", 0x2013, 0}, {"mdash", 0x2014, 0},
        {"hellip", 0x2026, 0}, {"bull", 0x2022, 0}, {"bullet", 0x2022, 0}
    };
    const char *start = *p, *end = start + 1;
    const char *digits;
    char *number_end;
    long value;
    unsigned i;

    if (*end == '#')
        ++end;
    while (end - start <= 12 && isalnum((unsigned char)*end))
        ++end;
    if (end == start + 1)
        return '&';
    for (i = 0; i < sizeof(entities) / sizeof(entities[0]); ++i) {
        if ((*end == ';' || entities[i].legacy) &&
            name_is(start + 1, end - start - 1, entities[i].name)) {
            *p = *end == ';' ? end : end - 1;
            return entities[i].value;
        }
    }
    if (*end == ';' && start[1] == '#') {
        digits = start + 2;
        if (*digits == 'x' || *digits == 'X') {
            ++digits;
            value = strtol(digits, &number_end, 16);
        } else {
            value = strtol(digits, &number_end, 10);
        }
        if (number_end == end && number_end != digits) {
            *p = end;
            return value;
        }
    }
    return '&';
}

/* Leave the cursor on the last byte, like entity(). */
static long text_codepoint(const char **p)
{
    const unsigned char *start = (const unsigned char *)*p;
    unsigned first = *start;
    int i, size;
    long value, minimum;

    if (first >= 0xc2 && first <= 0xdf) {
        size = 2;
        value = first & 0x1f;
        minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
        size = 3;
        value = first & 0x0f;
        minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
        size = 4;
        value = first & 0x07;
        minimum = 0x10000;
    } else {
        /* Legacy single-byte forms fit in one output cell. */
        if (first == 0xa9)
            return 'C';
        if (first == 0x85)
            return '.';
        return first;
    }
    for (i = 1; i < size; ++i) {
        if ((start[i] & 0xc0) != 0x80)
            return first;
        value = (value << 6) | (start[i] & 0x3f);
    }
    if (value < minimum || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff))
        return first;
    *p += size - 1;
    return value;
}

static void html_text_byte(long value)
{
    static const char latin1[] =
        "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs"
        "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
    const char *audio;

    switch (value) {
    case 0x91: case 0x92: case 0x2018: case 0x2019:
        text_byte(39);
        break;
    case 0x93: case 0x94: case 0x201c: case 0x201d:
        text_byte('"');
        break;
    case 0x96: case 0x97: case 0x2013: case 0x2014:
        text_byte('-');
        break;
    case 0x95: case 0x2022:
        text_byte('*');
        break;
    case 0x2026:
        text_byte('.');
        text_byte('.');
        text_byte('.');
        break;
    case 0xa0:
        text_byte(' ');
        break;
    case 0xa9:
        text_byte('(');
        text_byte('c');
        text_byte(')');
        break;
    case 0x1f3a7:
        for (audio = "[Audio]"; *audio; ++audio)
            text_byte(*audio);
        break;
    default:
        if (value >= 0xc0 && value <= 0xff)
            text_byte(latin1[value - 0xc0]);
        else if (value >= 0x1f300 && value <= 0x1faff)
            text_byte('*');
        else
            text_byte(value >= 0 && value < 128 ? (int)value : '?');
    }
}

static int add_link(const struct url *base, const char *p, const char *end, int in_list)
{
    const char *name, *start, *stop;
    char href[URL_SIZE];
    int n, quote;
    long value;

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
            value = *start == '&' ? entity(&start) : (unsigned char)*start;
            if (value == 0xa0)
                value = ' ';
            if (value < 32 || value >= 127)
                return 0;
            href[n++] = (char)value;
            ++start;
        }
        href[n] = 0;
        if (start == stop && resolve_url(base, href, page->links[page->link_count])) {
            flush_word();
            list_bullet = 0;
            pending_marker = ++page->link_count;
            marker_only = in_list;
            return page->link_count;
        }
        return 0;
    }
    return 0;
}

static void render_html(const struct url *base)
{
    const char *p = body, *name, *attrs, *end, *hidden = NULL;
    long c;
    int closing, n, quote, active_link = 0, item_links = -1;

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
                c = *p == '&' ? entity(&p) : text_codepoint(&p);
                html_text_byte(c);
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
                emit_marker(0);
                page->link_end[active_link - 1] = cursor;
                active_link = 0;
            }
            if (!closing)
                active_link = add_link(base, attrs, end, item_links >= 0);
        } else if (name_is(name, n, "p")) {
            newline();
            if (closing && cursor)
                block_gap = 1;
        } else if (name_is(name, n, "li")) {
            newline();
            if (item_links >= 0 && page->link_count > item_links)
                block_gap = 1;
            item_links = closing ? -1 : page->link_count;
            list_bullet = !closing;
        } else if (name_is(name, n, "br") || name_is(name, n, "div") ||
                   name_is(name, n, "center") ||
                   name_is(name, n, "ul") || name_is(name, n, "ol") ||
                   (n == 2 && tolower((unsigned char)*name) == 'h' &&
                    name[1] >= '1' && name[1] <= '6')) {
            newline();
        }
    }
    if (active_link) {
        flush_word();
        emit_marker(0);
        page->link_end[active_link - 1] = cursor;
    }
}

static void render(const struct url *url, int html)
{
    const unsigned char *p;

    cursor = column = pending_space = block_gap = list_bullet = 0;
    word_length = pending_marker = page->link_count = 0;
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

static void display_page(void)
{
    const char *p = visible_start, *start, *end;
    int i;

    for (i = 0; i < page->link_count; ++i) {
        start = page->rendered + page->link_start[i];
        end = page->rendered + (page->marker_end[i] ? page->marker_end[i] : page->link_end[i]);
        if (end <= p)
            continue;
        if (start >= visible_end)
            break;
        if (start > p) {
            console_write(p, start - p);
            p = start;
        }
        if (end > visible_end)
            end = visible_end;
        console_underline(1);
        console_write(p, end - p);
        console_underline(0);
        p = end;
    }
    console_write(p, visible_end - p);
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
        display_page();
        if (*visible_end)
            show(more_below);
        show("\n");
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
    if (row == 2 + screen_lines && *visible_end &&
        col >= 0 && col < (int)sizeof(more_below) - 1) {
        strcpy(command, "n");
        return 1;
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

enum navigation_mode { FOLLOW, BACK, REFRESH };

static const char *navigate(const char *address, enum navigation_mode mode)
{
    struct url url;
    char requested[URL_SIZE];
    const char *error;
    int html;

    if (mode == BACK && previous_page) {
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
    if (mode == FOLLOW && depth) {
        previous_page = page;
        page = page == &pages[0] ? &pages[1] : &pages[0];
    }
    render(&url, html);
    if (mode != FOLLOW) {
        if (mode == BACK) {
            --depth;
            scroll = history_scroll[depth - 1];
        }
        strcpy(history[depth - 1], requested);
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
            message = navigate(input, FOLLOW);
        } else if (name_is(input, strlen(input), "m")) {
            message = console_pointer_status();
        } else if (name_is(input, strlen(input), "b")) {
            if (depth < 2)
                message = "No earlier page.";
            else
                message = navigate(history[depth - 2], BACK);
        } else if (name_is(input, strlen(input), "r")) {
            message = depth ? navigate(history[depth - 1], REFRESH) : "No page to refresh.";
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
                          ? navigate(page->links[choice - 1], FOLLOW) : "Unknown link number.";
            else
                message = navigate(input, FOLLOW);
        }
    }
    console_close();
    return 0;
}
