# qlbrowser

A text browser for QDOS and SMSQ/E. It requires an installed, working
`TCP_` driver. Refer to your device and driver documentation for setup.
The browser opens `TCP_host:port` directly and needs no environment
variables, terminal database or resident browser launcher.

SNLTCP v1.0.0 support on QIMSI Gold over USB SER2 is coming soon.

## Build and install

Run `make package` with `qdos-gcc` on your build machine.

- `build/dist/qlbrowser` is the type-1 executable with its Q-emulator
  transfer header, using the same packager as QLVI. The header contains the
  dataspace from the linker.
- `build/qdos/qlbrowser` is the raw QDOS GCC output with its XTcc trailer,
  for transfer tools that recognize that format.

Transfer the executable to a drive accessible from the QL. Use a transfer
tool that interprets the packaged file's Q-emulator header and preserves
the native executable type and dataspace.

Launch it from SuperBASIC using the path where you installed it. For
example, if you installed it on `win1_`:

```basic
EX win1_qlbrowser
```

## HTTPS helper

HTTPS browsing requires the Python 3 helper in `tools/https_helper.py`.
Run it on the Linux host on your network whose loopback interface your
`TCP_` driver reaches at `127.0.0.1`. The browser connects to the helper
at `127.0.0.1:8787` through that driver. With SMSQmulator's built-in
`TCP_` device, run the helper on the same machine as SMSQmulator.

From the repository root on that host, run:

```sh
python3 tools/https_helper.py
```

Leave the helper running while you browse. [CNN Lite](https://lite.cnn.com/)
and [NPR Text-Only](https://text.npr.org/) both work well with qlbrowser.
The helper checks remote certificates and hostnames using the host's
trusted CA store. TLS protects the
connection from the helper to the website. Use `--ca-file PATH` to add a CA
for a private HTTPS site. Plain HTTP works through the `TCP_` driver
without the helper.

## BBC News RSS and articles

The browser supports RSS feeds like the BBC RSS feed.

With the HTTPS helper running, enter
`https://feeds.bbci.co.uk/news/rss.xml`. The helper converts RSS 2.0 to
HTML with numbered headlines and summaries. Choose a written story
(video entries also appear in the BBC feed), then press `B` to return.

For HTTPS HTML over 256 KiB, the helper can read up to 8 MiB and extract
headings and text from HTML articles. This keeps large scripts and page
furniture off the serial link. For other large HTML pages, the helper can
keep headings, text, and links in a simplified page. Responses that exceed
the helper's input or decoded size limit return a size error.

## Controls and scope

The browser centers its window horizontally and uses the available display
height. It uses standard 512 × 256 geometry if display limits are unavailable.
On QIMSI Gold and Q68, the window width depends on the display mode.

| Display mode | Resolution | Window width |
| --- | --- | --- |
| Normal QL Mode 4 | 512 × 256 | 80% |
| Large QL Mode 4 | 1024 × 768 | 50% |
| 8-bit Aurora | 1024 × 768 | 50% |
| Small 16-bit | 512 × 256 | 80% |
| Medium 16-bit | 512 × 384 | 80% |
| Large 16-bit | 1024 × 512 | 50% |

The browser measures text columns and page rows from the console. HTML prose
wraps at word boundaries, with hard breaks for words longer than a line.
Link numbers stay with their first word when they fit together on a line.
URLs can be up to 1,023 characters long.
Paragraphs and linked list entries have blank lines between them. A link
number replaces the bullet when the link starts a list item. The QL console
underlines link numbers in lists and the full text of inline links.

### Navigation

| Action | Keyboard | Mouse |
| --- | --- | --- |
| Open a URL | Type the URL and press Return, or press `O` and Return for a dedicated URL prompt. | |
| Follow a link | Type its number and press Return. | Left-click its number or any visible line of its text. |
| Go back | `B` and Return | Left-click `[Back]` or right-click inside the browser window. |
| Move down one screen | `N` and Return, Down Arrow, or Space | Left-click `[Next]` or `[More below]`, or scroll down. |
| Move up one screen | `P` and Return or Up Arrow | Left-click `[Prev]` or scroll up. |
| Refresh the current page | `R` and Return | Left-click `[Refresh]`. |
| Quit | `Q` and Return | |

Use mouse controls and the immediate arrow and Space shortcuts at an empty
command prompt. Mouse controls use the Pointer Environment. Alt+Up and
Alt+Down also page at an empty prompt.

Back restores the previous page from memory, including its scroll position
and links. A second consecutive Back refetches the older page. Refresh
reloads the current URL without adding a history entry and keeps the current
screen position if that screen still exists after reloading.

Press `M` and Return to see the last mouse button event or pointer setup
status. Use Backspace or the QL's Ctrl+Left key to erase input. Escape
clears the current prompt.

The browser uses HTTP/1.1 GET and displays simple HTML or plain text
from 200 responses, up to 256 KiB each. In HTML, common legacy entities such
as `&copy` also work without a semicolon. Latin-1 accents fold to ASCII,
headphones render as `[Audio]`, and other emoji in the U+1F300–U+1FAFF
range render as `*`. The browser supports relative and absolute HTTP links,
follows up to five HTTP or HTTPS redirects, and decodes chunked responses. The HTTPS helper handles gzip and deflate
with bounded decoding. In-page fragment navigation is outside the current
scope.

## License

Copyright 2026 Steve Jennen. See the [BSD 2-Clause License](LICENSE).

The executable packager in `tools/package_qdos.py` comes from Steve
Jennen's QLVI project and retains its original notice. The QDOS build uses
the QDOS GCC toolchain and its runtime libraries.
