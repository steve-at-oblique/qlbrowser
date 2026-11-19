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
   `/index.html`, `/second.html` and `/index.html`, in that order.
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
to reload the final page-one URL. The fixture log should show `/start`
once, followed by requests for page one, page two and page one again.

## Controls and scope

Type a URL or press `O` and Return for a dedicated URL prompt. Enter a
number shown next to a link to follow it. `B` refetches the previous page
and restores its scroll position when available. `N` and `P` move down
and up by one screen. `Q` quits. Use Backspace or the QL's Ctrl+Left key
to erase input. Escape clears the current prompt.

The browser uses HTTP/1.1 GET and displays simple HTML or plain text
from 200 responses, up to 32 KiB each. It displays ASCII text and supports
relative and absolute HTTP links, follows up to five plain HTTP redirects,
and decodes chunked responses. It currently lacks TLS, compressed responses
and in-page fragment navigation.

Run `make test` for a loopback smoke test using the same browser core with
host sockets. It checks the executable header, the two-page navigation
sequence through a redirect and chunked response, Back refetching the final
URL, and recovery from HTTP errors. The QIMSI
test above checks the QDOS console, QDOS I/O traps and USB driver path.
