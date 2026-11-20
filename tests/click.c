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
    screen_width = 79;
    strcpy(body, "<a href='/headline?x=1&amp;y=2'>"
                 "\xe2\x80\x9c" "I\xe2\x80\x99"
                 "m not MAGA,\xe2\x80\x9d Says Man in MAGA Hat</a>"
                 "<p>&ldquo;It&#8217;s readable&#x201d; &mdash; &copy;</p>"
                 "<p>\x93" "Legacy\x94</p>");
    render(&url, 1);
    assert(strstr(page->rendered, "[1] \"I'm not MAGA,\" Says Man in MAGA Hat\n"));
    assert(strstr(page->rendered, "\"It's readable\" - (c)\n"));
    assert(strstr(page->rendered, "\"Legacy\"\n"));
    assert(!strcmp(page->links[0], "http://example.org/headline?x=1&y=2"));
    strcpy(body, "<p>Thursday, November 19, 2026 \xe2\x80\xa2 1:28 PM EST</p>"
                 "<p>&bull; &bullet; &#8226; &#x2022; \x95</p>");
    render(&url, 1);
    assert(!strcmp(page->rendered, "Thursday, November 19, 2026 * 1:28 PM EST\n\n"
                                  "* * * * *\n"));
    screen_width = 20;
    strcpy(body, "<center>Top | Technology <a href='/entertainment'>"
                 "Entertainment</a></center><center>US edition</center>");
    render(&url, 1);
    assert(strstr(page->rendered, "\n[1] Entertainment\nUS edition\n"));
    assert(!strncmp(page->rendered + page->link_start[0], "[1] Entertainment", 17));

    return 0;
}
