# qlbrowser

A text browser for QDOS and SMSQ/E. The first milestone targets a 32 MB
QIMSI Gold with QLTCPIP V0.1.14 on QIMSI USB SER2. The browser opens
`TCP_host:port` directly and needs no environment variables, terminal
database or resident browser launcher.

## Build and install

Run `make package` with `qdos-gcc` on your build machine.

- `build/dist/qlbrowse` is the type-1 executable with its Q-emulator
  transfer header, using the same packager as QLVI. The header contains the
  dataspace from the linker.
- `build/qdos/qlbrowse` is the raw QDOS GCC output with its XTcc trailer,
  for transfer tools that recognize that format.

For an SNL transfer, put the packaged `qlbrowse` in the host directory
mapped to SNL drive 4. While SNL is active, copy it onto the QIMSI drive
from SuperBASIC.

```basic
COPY snl4_qlbrowse TO win1_qlbrowse
```

Use a transfer that interprets the Q-emulator header and preserves the native
executable type and dataspace. A byte-for-byte host filesystem copy of the
wrapped file onto QIMSI storage alone can leave it unlaunchable.

## Two-page QIMSI test

Hardware verification passed on the 32 MB QIMSI Gold with SMSQ/E and
QLTCPIP V0.1.14 over USB SER2. The user launched `EX win1_qlbrowse`,
loaded page one, followed the link to page two, and returned to page one
with Back.

Two follow-up checks also passed on hardware. After a request returned
HTTP 404, link `1` and Back still worked. Restarting the Linux TCP daemon
with the browser open also allowed link `1` and Back to work without
relaunching the browser or reinstalling the driver.

1. Copy this repository's `fixture` directory to the Linux box. Run the
   following there and leave it running. The two pages are available on
   loopback, which is sufficient when the TCP daemon runs on the same box.

   ```sh
   python3 -m http.server 8000 --bind 127.0.0.1 --directory fixture
   ```

2. Reboot the QL with SNL disabled and stop the SNL host daemon before using
   the same serial port. Start a fresh QLTCPIP daemon from its checkout on
   Linux using the already tested QIMSI USB profile.

   ```sh
   python3 qltcpip_serial.py --trace
   ```

   The default configuration is `qltcpip_config.json`, for QIMSI native USB
   on `/dev/ttyACM0`. Use `--config` with your working profile if it differs.

3. Install the assembled V0.1.14 driver once per boot as described in
   QLTCPIP's `docs/driver.md`. These commands assume your assembled driver
   is already on `win1_` as `tcpdev`. Disconnect any open QLTerm session
   before launching the browser.

   ```basic
   a=RESPR(4096)
   LBYTES win1_tcpdev,a
   CALL a
   EX win1_qlbrowse
   ```

4. Enter `http://127.0.0.1:8000/index.html` at the browser prompt. Expect
   **QL browser test - page one** and **[1] Open page two**. Enter `1`
   to follow the link, `B` to return to page one, and `Q` to quit. Press
   Return after each entry. The Linux HTTP log should show 200 responses for
   `/index.html` and `/second.html`. Back restores page one from memory.
   Here `127.0.0.1` refers to the Linux daemon host. To serve the fixture
   on a separate Linux box, bind to its LAN address and use that address
   in the URL.

## Redirect and chunked-response hardware test

The user verified this fixture on the QIMSI Gold with the updated browser.
The redirect, chunked page, relative link, and Back sequence passed.

Transfer the new executable and start the fixture on the Linux daemon host.
Copy both `tools/http_fixture.py` and the `fixture` directory, keeping
the same layout, then run from their parent directory.

```sh
python3 tools/http_fixture.py
```

After launching `EX win1_qlbrowse`, enter
`http://127.0.0.1:8000/start`. The browser should display page one at
`/pages/index.html`, reached through a redirect and decoded from a chunked
response. Enter `1` to reach page two in the same directory. Enter `B`
to restore page one at its final URL. The fixture log should show `/start`
once, followed by requests for page one and page two.

## Host-assisted HTTPS on Linux

The user verified on the QIMSI Gold that the NPR Text-Only homepage loads,
numbered headlines open, and Back returns to the headline list over
repeated attempts. With the NPR page still open, the user restarted the
Linux QLTCPIP daemon and then the HTTPS helper. Headline navigation and
Back worked again without relaunching qlbrowse.

Copy `tools/https_helper.py` to `~/dev/qlbrowser/tools/` on the **same
Linux box that runs the QLTCPIP daemon**. Python 3 is sufficient. Leave
the daemon running and start the helper in a separate terminal.

```sh
cd ~/dev/qlbrowser
python3 tools/https_helper.py
```

The helper listens only on Linux loopback at `127.0.0.1:8787`. It uses
the host's trusted CA store and checks the remote certificate and hostname.
It accepts HTTPS GET requests from qlbrowse and sends the response over
the existing QL-to-host serial link. TLS protects the Linux-to-site leg.
Use `--ca-file PATH` to add a CA for a private HTTPS site.

On the QL, install the updated executable and run `EX win1_qlbrowse`.
Enter `https://text.npr.org/`, choose a numbered headline, and enter
`B` to return. You can also start with `http://text.npr.org/` to
exercise its redirect to HTTPS. Plain HTTP still goes straight through
QLTCPIP, including the port-8000 fixture.

## BBC News RSS and articles

The user confirmed that the BBC RSS milestone worked on the QIMSI Gold.
The desktop test also verified opening a live BBC article from the feed
and returning with Back.

Update both the executable and the Linux HTTPS helper, then restart the
helper using the instructions above. On the QL, enter
`https://feeds.bbci.co.uk/news/rss.xml`. The helper converts RSS 2.0 to
HTML with numbered headlines and summaries. Choose a written story
(video entries also appear in the BBC feed), then press `B` to return.

For HTTPS HTML over 256 KiB, the helper can read up to 4 MiB and extract
headings and text from HTML articles. This keeps large scripts and page
furniture off the serial link. Oversized pages without a usable article
still return a size error.

## Controls and scope

On SMSQ/E the browser queries the available display from its primary console and derives the number of
text columns and page rows from the console's character dimensions. A
classic QDOS display uses the standard 512 x 256 geometry. HTML prose
wraps at word boundaries, with hard breaks for words longer than a line.
Paragraphs and linked list entries have blank lines between them. A link
number replaces the bullet when the link starts a list item. The QL console
underlines link numbers in lists and the full text of inline links.
The user verified the improved article and linked-list layouts on the
QIMSI Gold.
The user verified word wrapping and full display coverage in Large Mode 4
on the QIMSI Gold. The primary console showed the full NPR Text-Only
homepage at once.

Type a URL or press `O` and Return for a dedicated URL prompt. Enter a
number shown next to a link to follow it. With the SMSQ/E Pointer
Environment, click the link number or any visible line of its text.
The Back, Next, and Prev buttons directly below the URL accept clicks. Press
`M` and Return to see the last mouse event or the pointer setup status.
The user verified mouse navigation on the QIMSI Gold with qlbrowse 0.5.0.
The version and current URL share one header line, also verified on hardware.
The user verified the top navigation buttons on the QIMSI Gold with
qlbrowse 0.5.1.

`B` restores the previous page from memory, including its scroll position
and links. A second consecutive Back refetches the older page. The user
verified fast Back navigation on the QIMSI Gold with qlbrowse 0.6.0.

`N` and `P` move down and up by one screen. `Q` quits. Use Backspace or
the QL's Ctrl+Left key
to erase input. Escape clears the current prompt.

The browser uses HTTP/1.1 GET and displays simple HTML or plain text
from 200 responses, up to 256 KiB each. It displays ASCII text and supports
relative and absolute HTTP links, follows up to five HTTP or HTTPS redirects,
and decodes chunked responses. Compressed responses and in-page fragment
navigation are outside the current scope.

Run `make test` for a loopback smoke test using the same browser core with
host sockets. It checks the executable header, the two-page navigation
sequence through a redirect and chunked response, Back restoring the final URL without another request, recovery from HTTP errors, HTTPS certificate hostname rejection,
larger pages, RSS navigation, and article extraction. The test needs OpenSSL to generate a temporary certificate. The
QIMSI tests check the QDOS console, QDOS I/O traps and USB driver path.
