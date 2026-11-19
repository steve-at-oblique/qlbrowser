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
    strcpy(body, "<div>Intro</div><p><a href='/story'>Headline with a continuation "
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

    previous_page = page;
    page = &pages[1];
    history_scroll[0] = scroll;
    depth = 2;
    scroll = 0;
    assert(!navigate(history[0], 1));
    assert(depth == 1 && scroll == screen_lines && !previous_page);
    display(NULL);
    assert(click_at(0, 2, command) && !strcmp(command, "1"));
    strcpy(body, "<p>First paragraph.</p><p>Second paragraph.</p>"
                 "<ul><li>One</li><li><a href='/story'>Two</a></li>"
                 "<li><a href='/third'>Three</a></li></ul>");
    render(&url, 1);
    assert(!strcmp(page->rendered, "First paragraph.\n\nSecond paragraph.\n\n"
                                  "- One\n[1] Two\n\n[2] Three\n"));
    assert(!strncmp(page->rendered + page->link_start[0], "[1] Two", 7));
    assert(page->marker_end[0] - page->link_start[0] == 3);
    assert(page->link_end[0] - page->link_start[0] == 7);
    scroll = 5;
    display(NULL);
    assert(click_at(5, 2, command) && !strcmp(command, "1"));
    assert(!click_at(0, 3, command));
    return 0;
}
