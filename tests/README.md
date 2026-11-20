# qlbrowser testing

## Automated tests

From the repository root, run `make test` for a loopback smoke test using the same browser core with
host sockets. It checks the executable header, the two-page navigation
sequence through a redirect and chunked response, Refresh preserving history,
cached Back, recovery from HTTP errors, HTTPS certificate hostname rejection,
larger pages, RSS navigation, and article extraction. The test needs OpenSSL
to generate a temporary certificate.

## QIMSI Gold hardware results

- Window sizing passed in all six documented display modes.
- Mouse wheel paging passed after adding support for the SMSQ/E Alt+Up
  and Alt+Down wheel codes.
- HTTPS browsing passed with SNLTCP using `TCP_` open mode 1.

## SMSQmulator results

- HTTPS browsing passed on SMSQmulator 3.03 with its built-in `TCP_` device
  after changing the open mode to 1.
