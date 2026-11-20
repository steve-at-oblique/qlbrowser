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
