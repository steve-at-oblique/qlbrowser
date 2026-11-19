/* Exercise the renderer and click mapping without an SMSQ/E pointer device. */
#include <assert.h>

#define main browser_main
#include "../src/browser.c"
#undef main

int main(void)
{
    struct url url;
    char command[URL_SIZE];

    screen_width = 42;
    screen_lines = 2;
    depth = 1;
    strcpy(history[0], "http://example.org/");
    assert(parse_url(history[0], &url));
    strcpy(body, "<p>Intro</p><p><a href='/story'>Headline with a continuation "
                 "on the next row</a> tail</p>");
    render(&url, 1);
    display(NULL);
    assert(!click_at(0, 2, command));
    assert(click_at(0, 3, command) && !strcmp(command, "1"));
    assert(click_at(5, 3, command) && !strcmp(command, "1"));
    assert(click_at(9, 1, command) && !strcmp(command, "n"));

    scroll = screen_lines;
    display("HTTP message");
    assert(click_at(0, 2, command) && !strcmp(command, "1"));
    assert(!click_at(strlen("next row "), 2, command));
    assert(click_at(0, 1, command) && !strcmp(command, "b"));
    assert(click_at(19, 1, command) && !strcmp(command, "p"));
    assert(!click_at(6, 1, command));
    return 0;
}
