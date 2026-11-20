# Bundled third-party code

Used only by the first-start screen (`port3ds/p3ds_getwad.c`), which can
download Blasphemer when no game data is found. They are bundled rather than
taken from devkitPro's package server so the port builds with nothing but
devkitARM and libctru.

| Directory | What | Version | Licence |
|---|---|---|---|
| `mbedtls/` | [Mbed TLS](https://github.com/Mbed-TLS/mbedtls): HTTPS | 2.28.8 | Apache-2.0 **or** GPL-2.0-or-later (used here under GPL-2.0-or-later); see `mbedtls/LICENSE` |
| `zlib/` | [zlib](https://github.com/madler/zlib): inflate only, for the zip | 1.3.1 | zlib licence; see `zlib/LICENSE` |

Both are unmodified copies of the official release archives, trimmed to what
is built:

- mbedtls: `library/` and `include/` without their build files, and without
  `library/sha1.c`. That file name clashes with Chocolate Doom's
  `src/sha1.c`, and SHA-1 isn't needed here.
- zlib: the inflate sources (`adler32.c crc32.c inffast.c inflate.c
  inftrees.c zutil.c`) and the headers they include.

`mbedtls_config_3ds.h` is the port's own mbedtls configuration: a TLS 1.2
client, nothing else. It is selected with `-DMBEDTLS_CONFIG_FILE` in
`port3ds/Makefile`.
