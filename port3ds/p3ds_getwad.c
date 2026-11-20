//
// First-start screen for when no game data is found: offers to download
// Blasphemer (a free, BSD-licensed replacement for Heretic's data) from
// its official GitHub release, or explains where to put your own WAD.
//
// The download runs on a worker thread while the main thread draws the
// screens: a picture on the top screen (port3ds/meta/getwad_top.bin, drawn
// by meta/make_art.py) and a touch/button menu on the bottom screen.
//
// Only one exact file is ever accepted: blasphem-0.1.8.zip with a pinned
// SHA-256, which is checked before anything is unpacked.  The zip holds a
// single deflated blasphem.wad, which is inflated with zlib and checked
// against its CRC-32 and size before it is renamed into place.
//

#include <3ds.h>
#include <errno.h>
#include <malloc.h>
#include <netdb.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "zlib.h"

#include "getwad_font_bin.h"
#include "getwad_top_bin.h"

#include "port3ds.h"

// --- The file -----------------------------------------------------------------

#define BLASPHEMER_VERSION "0.1.8"
#define ZIP_URL \
    "https://github.com/Blasphemer/blasphemer/releases/download/v" \
    BLASPHEMER_VERSION "/blasphem-" BLASPHEMER_VERSION ".zip"
#define ZIP_SIZE        7601799
#define WAD_SIZE        20417448
#define WAD_CRC         0x01b5335bUL

static const unsigned char zip_sha256[32] =
{
    0x58, 0x38, 0xd2, 0x11, 0xad, 0xd8, 0x1e, 0x16,
    0x96, 0xa6, 0x21, 0x48, 0x5c, 0x1e, 0x40, 0x77,
    0x10, 0x35, 0xda, 0xf6, 0x60, 0x9d, 0x08, 0xba,
    0xd7, 0x01, 0x8f, 0x52, 0xac, 0xfa, 0x5e, 0x54,
};

#define ZIP_PART  P3DS_DATA_DIR "/blasphem-" BLASPHEMER_VERSION ".zip.part"
#define WAD_PART  P3DS_DATA_DIR "/blasphem.wad.part"
#define WAD_FINAL P3DS_DATA_DIR "/blasphem.wad"

// Zip plus unpacked WAD, with some room to spare.
#define SPACE_NEEDED ((s64) (ZIP_SIZE + WAD_SIZE) + (2 << 20))

// --- Worker state shared with the screens --------------------------------------

typedef enum
{
    JOB_IDLE,
    JOB_CONNECTING,
    JOB_DOWNLOADING,
    JOB_VERIFYING,
    JOB_UNPACKING,
    JOB_DONE,
    JOB_FAILED,
    JOB_CANCELLED,
} job_state_t;

static volatile job_state_t job_state;
static volatile s64 job_done, job_total;
static volatile boolean job_cancel;
static char job_error[256];
static Thread job_thread;

#define CANCELLED_ERR (-0x7f01)     // our own code, outside mbedtls's range

static void Fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void Fail(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(job_error, sizeof(job_error), fmt, ap);
    va_end(ap);
    printf("getwad: %s\n", job_error);
}

// --- Randomness for TLS -----------------------------------------------------------

static int PSEntropy(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void) data;

    if (R_FAILED(PS_GenerateRandomBytes(out, len)))
    {
        // Should not happen; fall back to the tick counter.
        size_t i;

        for (i = 0; i < len; ++i)
        {
            out[i] = (unsigned char) (svcGetSystemTick() >> (i % 7));
        }
    }

    *olen = len;
    return 0;
}

// --- TLS over a socket ----------------------------------------------------------------

typedef struct
{
    int fd;
    mbedtls_ssl_context ssl;
} conn_t;

static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;
static mbedtls_ssl_config tls_conf;

static int SockSend(void *ctx, const unsigned char *buf, size_t len)
{
    int fd = *(int *) ctx;
    int r = send(fd, buf, len, 0);

    if (r < 0)
    {
        return errno == EAGAIN ? MBEDTLS_ERR_SSL_WANT_WRITE
                               : MBEDTLS_ERR_NET_SEND_FAILED;
    }

    return r;
}

// Waits for data in short slices so that Cancel works and a dead
// connection gives up after 20 seconds.
static int SockRecv(void *ctx, unsigned char *buf, size_t len)
{
    int fd = *(int *) ctx;
    int waited = 0;

    for (;;)
    {
        struct pollfd p = { .fd = fd, .events = POLLIN };
        int r;

        if (job_cancel)
        {
            return CANCELLED_ERR;
        }

        r = poll(&p, 1, 250);

        if (r > 0)
        {
            r = recv(fd, buf, len, 0);

            if (r < 0)
            {
                return errno == EAGAIN ? MBEDTLS_ERR_SSL_WANT_READ
                                       : MBEDTLS_ERR_NET_RECV_FAILED;
            }

            return r;
        }

        if (r < 0)
        {
            return MBEDTLS_ERR_NET_RECV_FAILED;
        }

        waited += 250;

        if (waited >= 20000)
        {
            return MBEDTLS_ERR_SSL_TIMEOUT;
        }
    }
}

static boolean TLSInit(void)
{
    static const char pers[] = "chocolate-heretic-3ds";

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_ssl_config_init(&tls_conf);

    mbedtls_entropy_add_source(&entropy, PSEntropy, NULL, 32,
                               MBEDTLS_ENTROPY_SOURCE_STRONG);

    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                              (const unsigned char *) pers,
                              sizeof(pers) - 1) != 0
     || mbedtls_ssl_config_defaults(&tls_conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0)
    {
        Fail("Couldn't start the secure connection code.");
        return false;
    }

    // The file is checked against its SHA-256 instead; see the top.
    mbedtls_ssl_conf_authmode(&tls_conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&tls_conf, mbedtls_ctr_drbg_random, &drbg);
    return true;
}

static void TLSShutdown(void)
{
    mbedtls_ssl_config_free(&tls_conf);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
}

static void ConnClose(conn_t *c)
{
    if (c->fd >= 0)
    {
        mbedtls_ssl_close_notify(&c->ssl);
        close(c->fd);
        c->fd = -1;
    }
    mbedtls_ssl_free(&c->ssl);
}

static boolean ConnOpen(conn_t *c, const char *host)
{
    struct addrinfo hints, *res = NULL;
    int r;

    c->fd = -1;
    mbedtls_ssl_init(&c->ssl);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, "443", &hints, &res) != 0 || res == NULL)
    {
        Fail("Couldn't find %s. Check the internet connection and try "
             "again.", host);
        return false;
    }

    c->fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    if (c->fd < 0 || connect(c->fd, res->ai_addr, res->ai_addrlen) != 0)
    {
        freeaddrinfo(res);
        Fail("Couldn't connect to %s. Check the internet connection and "
             "try again.", host);
        if (c->fd >= 0)
        {
            close(c->fd);
            c->fd = -1;
        }
        return false;
    }

    freeaddrinfo(res);

    if (mbedtls_ssl_setup(&c->ssl, &tls_conf) != 0
     || mbedtls_ssl_set_hostname(&c->ssl, host) != 0)
    {
        Fail("Couldn't set up the secure connection.");
        return false;
    }

    mbedtls_ssl_set_bio(&c->ssl, &c->fd, SockSend, SockRecv, NULL);

    while ((r = mbedtls_ssl_handshake(&c->ssl)) != 0)
    {
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            if (r != CANCELLED_ERR)
            {
                Fail("Couldn't make a secure connection to %s "
                     "(error -0x%04x).", host, (unsigned) -r);
            }
            return false;
        }
    }

    return true;
}

static boolean ConnWrite(conn_t *c, const char *data, size_t len)
{
    while (len > 0)
    {
        int r = mbedtls_ssl_write(&c->ssl, (const unsigned char *) data, len);

        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            continue;
        }

        if (r <= 0)
        {
            Fail("The connection was lost while sending the request.");
            return false;
        }

        data += r;
        len -= r;
    }

    return true;
}

// Returns bytes read, 0 at the end of the stream, or <0 on error.
static int ConnRead(conn_t *c, unsigned char *buf, size_t len)
{
    for (;;)
    {
        int r = mbedtls_ssl_read(&c->ssl, buf, len);

        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            continue;
        }

        if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        {
            return 0;
        }

        return r;
    }
}

// --- HTTPS GET with redirects ---------------------------------------------------------

static boolean SplitURL(const char *url, char *host, size_t host_len,
                        const char **path)
{
    const char *p, *slash;

    if (strncmp(url, "https://", 8) != 0)
    {
        return false;
    }

    p = url + 8;
    slash = strchr(p, '/');

    if (slash == NULL || (size_t) (slash - p) >= host_len)
    {
        return false;
    }

    memcpy(host, p, slash - p);
    host[slash - p] = '\0';
    *path = slash;
    return true;
}

// Case-insensitive search for "\r\nName:" in the response headers.
static const char *FindHeader(const char *headers, const char *name)
{
    size_t n = strlen(name);
    const char *p = headers;

    while ((p = strstr(p, "\r\n")) != NULL)
    {
        p += 2;

        if (strncasecmp(p, name, n) == 0 && p[n] == ':')
        {
            p += n + 1;
            while (*p == ' ')
            {
                ++p;
            }
            return p;
        }
    }

    return NULL;
}

// Downloads url into out, following up to five redirects, feeding every
// byte to the hash.
static boolean HTTPSGet(const char *url, FILE *out,
                        mbedtls_sha256_context *hash)
{
    static char current[1024], location[1024];
    static unsigned char buf[16384];
    int redirects;

    snprintf(current, sizeof(current), "%s", url);

    for (redirects = 0; redirects <= 5; ++redirects)
    {
        char host[256], request[1400];
        const char *path, *h;
        conn_t conn;
        size_t have = 0;
        char *body;
        int status, r;
        s64 length;

        if (!SplitURL(current, host, sizeof(host), &path))
        {
            Fail("The server sent an unexpected address.");
            return false;
        }

        job_state = JOB_CONNECTING;

        if (!ConnOpen(&conn, host))
        {
            ConnClose(&conn);
            return false;
        }

        snprintf(request, sizeof(request),
                 "GET %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: chocolate-heretic-3ds\r\n"
                 "Accept: */*\r\n"
                 "Connection: close\r\n\r\n", path, host);

        if (!ConnWrite(&conn, request, strlen(request)))
        {
            ConnClose(&conn);
            return false;
        }

        // Read the headers; whatever follows them is the start of the body.
        body = NULL;
        while (body == NULL)
        {
            if (have >= sizeof(buf) - 1)
            {
                Fail("The server's answer was too long.");
                ConnClose(&conn);
                return false;
            }

            r = ConnRead(&conn, buf + have, sizeof(buf) - 1 - have);

            if (r <= 0)
            {
                if (r != CANCELLED_ERR)
                {
                    Fail("The connection was lost before the download "
                         "started.");
                }
                ConnClose(&conn);
                return false;
            }

            have += r;
            buf[have] = '\0';
            body = strstr((char *) buf, "\r\n\r\n");
        }

        body[2] = '\0';     // headers end with their last "\r\n"
        body += 4;

        if (sscanf((char *) buf, "HTTP/1.%*d %d", &status) != 1)
        {
            Fail("The server's answer wasn't understood.");
            ConnClose(&conn);
            return false;
        }

        if (status >= 300 && status < 400)
        {
            h = FindHeader((char *) buf, "Location");
            if (h == NULL)
            {
                Fail("The server's redirect had no address.");
                ConnClose(&conn);
                return false;
            }
            snprintf(location, sizeof(location), "%.*s",
                     (int) strcspn(h, "\r\n"), h);
            ConnClose(&conn);
            snprintf(current, sizeof(current), "%s", location);
            continue;
        }

        if (status != 200)
        {
            Fail("GitHub answered with an error (HTTP %d).", status);
            ConnClose(&conn);
            return false;
        }

        h = FindHeader((char *) buf, "Content-Length");
        length = h != NULL ? strtoll(h, NULL, 10) : ZIP_SIZE;

        if (length != ZIP_SIZE)
        {
            Fail("The file on GitHub isn't the expected size (%lld bytes "
                 "instead of %d).", (long long) length, ZIP_SIZE);
            ConnClose(&conn);
            return false;
        }

        job_total = length;
        job_done = 0;
        job_state = JOB_DOWNLOADING;

        // Body bytes that arrived with the headers.
        have = (unsigned char *) buf + have - (unsigned char *) body;
        memmove(buf, body, have);

        for (;;)
        {
            if (have > 0)
            {
                if (fwrite(buf, 1, have, out) != have)
                {
                    Fail("Couldn't write to the SD card.");
                    ConnClose(&conn);
                    return false;
                }
                mbedtls_sha256_update_ret(hash, buf, have);
                job_done += have;
            }

            if (job_done >= length)
            {
                break;
            }

            r = ConnRead(&conn, buf, sizeof(buf));

            if (r <= 0)
            {
                if (r != CANCELLED_ERR)
                {
                    Fail("The connection was lost during the download. "
                         "Try again.");
                }
                ConnClose(&conn);
                return false;
            }

            have = r;
        }

        ConnClose(&conn);
        return true;
    }

    Fail("GitHub redirected too many times.");
    return false;
}

// --- Unpacking ------------------------------------------------------------------------

static u16 Get16(const unsigned char *p)
{
    return p[0] | (p[1] << 8);
}

static u32 Get32(const unsigned char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32) p[3] << 24);
}

static boolean Unpack(void)
{
    static unsigned char in[65536], outbuf[65536];
    unsigned char header[30];
    FILE *zip, *wad;
    z_stream z;
    uLong crc = crc32(0, Z_NULL, 0);
    int r = Z_OK;

    job_state = JOB_UNPACKING;
    job_done = 0;
    job_total = ZIP_SIZE;

    zip = fopen(ZIP_PART, "rb");
    if (zip == NULL)
    {
        Fail("Couldn't read the download back from the SD card.");
        return false;
    }

    // The zip's single entry: a local header, its name and extra field,
    // then the deflated data.
    if (fread(header, 1, sizeof(header), zip) != sizeof(header)
     || Get32(header) != 0x04034b50 || Get16(header + 8) != 8
     || fseek(zip, Get16(header + 26) + Get16(header + 28), SEEK_CUR) != 0)
    {
        fclose(zip);
        Fail("The download isn't the expected zip file.");
        return false;
    }

    wad = fopen(WAD_PART, "wb");
    if (wad == NULL)
    {
        fclose(zip);
        Fail("Couldn't write to the SD card.");
        return false;
    }

    memset(&z, 0, sizeof(z));
    inflateInit2(&z, -MAX_WBITS);

    while (r != Z_STREAM_END)
    {
        if (job_cancel)
        {
            break;
        }

        if (z.avail_in == 0)
        {
            z.avail_in = fread(in, 1, sizeof(in), zip);
            z.next_in = in;
            job_done = ftell(zip);

            if (z.avail_in == 0)
            {
                break;
            }
        }

        z.next_out = outbuf;
        z.avail_out = sizeof(outbuf);
        r = inflate(&z, Z_NO_FLUSH);

        if (r != Z_OK && r != Z_STREAM_END)
        {
            break;
        }

        if (fwrite(outbuf, 1, sizeof(outbuf) - z.avail_out, wad)
            != sizeof(outbuf) - z.avail_out)
        {
            r = Z_ERRNO;
            break;
        }

        crc = crc32(crc, outbuf, sizeof(outbuf) - z.avail_out);
    }

    inflateEnd(&z);
    fclose(zip);

    if (fclose(wad) != 0 && r == Z_STREAM_END)
    {
        r = Z_ERRNO;
    }

    if (job_cancel)
    {
        remove(WAD_PART);
        return false;
    }

    if (r == Z_ERRNO)
    {
        remove(WAD_PART);
        Fail("Couldn't write to the SD card. Is it full?");
        return false;
    }

    if (r != Z_STREAM_END || z.total_out != WAD_SIZE || crc != WAD_CRC)
    {
        remove(WAD_PART);
        Fail("Unpacking failed: the WAD came out damaged.");
        return false;
    }

    remove(WAD_FINAL);
    if (rename(WAD_PART, WAD_FINAL) != 0)
    {
        Fail("Couldn't save blasphem.wad on the SD card.");
        return false;
    }

    remove(ZIP_PART);
    return true;
}

// --- The job ----------------------------------------------------------------------------

static boolean Download(void)
{
    mbedtls_sha256_context hash;
    unsigned char digest[32];
    FILE *out;
    boolean ok;

    out = fopen(ZIP_PART, "wb");
    if (out == NULL)
    {
        Fail("Couldn't write to the SD card.");
        return false;
    }

    mbedtls_sha256_init(&hash);
    mbedtls_sha256_starts_ret(&hash, 0);

    ok = HTTPSGet(ZIP_URL, out, &hash);

    if (fclose(out) != 0 && ok)
    {
        Fail("Couldn't write to the SD card. Is it full?");
        ok = false;
    }

    if (ok)
    {
        job_state = JOB_VERIFYING;
        mbedtls_sha256_finish_ret(&hash, digest);

        if (memcmp(digest, zip_sha256, sizeof(digest)) != 0)
        {
            Fail("The download was damaged (its checksum doesn't match). "
                 "Try again.");
            ok = false;
        }
    }

    mbedtls_sha256_free(&hash);

    if (!ok)
    {
        remove(ZIP_PART);
    }

    return ok;
}

static void Job(void *arg)
{
    struct statvfs st;
    u32 wifi = 0;
    boolean ok = false;

    (void) arg;

    job_error[0] = '\0';
    job_state = JOB_CONNECTING;

    if (statvfs("sdmc:/", &st) == 0
     && (s64) st.f_bavail * (s64) st.f_bsize < SPACE_NEEDED)
    {
        Fail("There isn't enough free space on the SD card: about %d MB "
             "is needed.", (int) (SPACE_NEEDED >> 20));
    }
    else if (R_FAILED(ACU_GetWifiStatus(&wifi)) || wifi == 0)
    {
        Fail("The 3DS isn't connected to the internet. Turn on Wi-Fi "
             "(check the connection in System Settings) and try again.");
    }
    else if (TLSInit())
    {
        ok = Download() && Unpack();
    }

    TLSShutdown();

    if (job_cancel)
    {
        remove(ZIP_PART);
        remove(WAD_PART);
        job_state = JOB_CANCELLED;
    }
    else
    {
        job_state = ok ? JOB_DONE : JOB_FAILED;
    }
}

// --- Drawing --------------------------------------------------------------------------

#define BOT_W 320
#define BOT_H 240

static u16 *bot;    // bottom framebuffer for this frame

#define RGB(r, g, b) ((u16) ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

#define C_BG_TOP     RGB(30, 18, 38)
#define C_BG_BOTTOM  RGB(10, 7, 14)
#define C_PANEL      RGB(44, 30, 54)
#define C_PANEL_HI   RGB(58, 40, 70)
#define C_BORDER     RGB(86, 64, 100)
#define C_GOLD       RGB(236, 184, 60)
#define C_TEXT       RGB(232, 228, 236)
#define C_DIM        RGB(160, 150, 170)
#define C_GREEN      RGB(70, 170, 84)
#define C_GREEN_HI   RGB(120, 214, 120)
#define C_BLUE       RGB(150, 200, 255)
#define C_RED        RGB(230, 96, 80)
#define C_TRACK      RGB(24, 16, 30)

static inline void Plot(int x, int y, u16 c)
{
    if (x >= 0 && x < BOT_W && y >= 0 && y < BOT_H)
    {
        bot[x * BOT_H + (BOT_H - 1 - y)] = c;
    }
}

static inline u16 Blend(u16 bg, u16 fg, int a)
{
    int r = ((bg >> 11) * (255 - a) + (fg >> 11) * a) / 255;
    int g = (((bg >> 5) & 63) * (255 - a) + ((fg >> 5) & 63) * a) / 255;
    int b = ((bg & 31) * (255 - a) + (fg & 31) * a) / 255;

    return (u16) ((r << 11) | (g << 5) | b);
}

static inline void PlotA(int x, int y, u16 c, int a)
{
    if (x >= 0 && x < BOT_W && y >= 0 && y < BOT_H && a > 0)
    {
        u16 *p = &bot[x * BOT_H + (BOT_H - 1 - y)];
        *p = a >= 255 ? c : Blend(*p, c, a);
    }
}

static void Background(void)
{
    int x, y;

    for (y = 0; y < BOT_H; ++y)
    {
        u16 c = Blend(C_BG_TOP, C_BG_BOTTOM, y * 255 / (BOT_H - 1));

        for (x = 0; x < BOT_W; ++x)
        {
            Plot(x, y, c);
        }
    }
}

// A filled rectangle with corners of radius r, antialiased.
static void RoundRect(int x0, int y0, int w, int h, int r, u16 c)
{
    int x, y;

    for (y = 0; y < h; ++y)
    {
        for (x = 0; x < w; ++x)
        {
            int dx = x < r ? r - x : (x >= w - r ? x - (w - r - 1) : 0);
            int dy = y < r ? r - y : (y >= h - r ? y - (h - r - 1) : 0);
            int a = 255;

            if (dx > 0 && dy > 0)
            {
                // Coverage from the distance to the corner's centre.
                int d2 = (dx - 1) * (dx - 1) + (dy - 1) * (dy - 1);
                int r2 = r * r;

                if (d2 >= r2)
                {
                    continue;
                }
                if (d2 > (r - 1) * (r - 1))
                {
                    a = 255 * (r2 - d2) / (r2 - (r - 1) * (r - 1));
                }
            }

            PlotA(x0 + x, y0 + y, c, a);
        }
    }
}

static void Circle(int cx, int cy, int r, u16 c)
{
    RoundRect(cx - r, cy - r, 2 * r, 2 * r, r, c);
}

// --- Text ---------------------------------------------------------------------------------

enum { FONT_BODY, FONT_BOLD, FONT_TITLE };

typedef struct
{
    u8 width, advance;
    s8 xoff;
    u8 reserved;
    u32 offset;
} glyph_t;

static const u8 *FontBlock(int font)
{
    return getwad_font_bin + Get32(getwad_font_bin + 4 + 4 * font);
}

static int LineHeight(int font)
{
    return FontBlock(font)[2];
}

static const glyph_t *Glyph(int font, char ch)
{
    const u8 *b = FontBlock(font);
    int i = (unsigned char) ch - b[0];

    if (i < 0 || i >= b[1])
    {
        i = '?' - b[0];
    }

    return (const glyph_t *) (b + 4 + 8 * i);
}

static int TextWidth(int font, const char *s, int len)
{
    int w = 0;

    for (; len > 0 && *s; ++s, --len)
    {
        w += Glyph(font, *s)->advance;
    }

    return w;
}

static void DrawText(int font, int x, int y, const char *s, int len, u16 c)
{
    const u8 *b = FontBlock(font);
    int h = b[2];

    for (; len > 0 && *s; ++s, --len)
    {
        const glyph_t *g = Glyph(font, *s);
        const u8 *alpha = b + g->offset;
        int gx, gy;

        for (gy = 0; gy < h; ++gy)
        {
            for (gx = 0; gx < g->width; ++gx)
            {
                PlotA(x + g->xoff + gx, y + gy, c, alpha[gy * g->width + gx]);
            }
        }

        x += g->advance;
    }
}

static void Text(int font, int x, int y, const char *s, u16 c)
{
    DrawText(font, x, y, s, strlen(s), c);
}

static void TextCentred(int font, int y, const char *s, u16 c)
{
    Text(font, (BOT_W - TextWidth(font, s, strlen(s))) / 2, y, s, c);
}

// Word-wraps s into a box w pixels wide; returns the y below the text.
static int Paragraph(int font, int x, int y, int w, const char *s, u16 c)
{
    int lh = LineHeight(font);

    while (*s)
    {
        int len = 0, fit = 0;

        while (*s == ' ')
        {
            ++s;
        }

        // Take words while they fit.
        for (;;)
        {
            int end = len;

            while (s[end] == ' ')
            {
                ++end;
            }
            while (s[end] && s[end] != ' ' && s[end] != '\n')
            {
                ++end;
            }

            if (fit > 0 && TextWidth(font, s, end) > w)
            {
                break;
            }

            fit = end;
            len = end;

            if (s[end] == '\0' || s[end] == '\n')
            {
                break;
            }
        }

        DrawText(font, x, y, s, fit, c);
        y += lh;
        s += fit;

        if (*s == '\n')
        {
            ++s;
        }
    }

    return y;
}

// A round button badge with a letter, like the 3DS's own UI.
static void Badge(int cx, int cy, const char *label, u16 bg)
{
    Circle(cx, cy, 9, bg);
    Text(FONT_BOLD, cx - TextWidth(FONT_BOLD, label, 1) / 2,
         cy - LineHeight(FONT_BOLD) / 2, label, RGB(20, 14, 24));
}

static void Hint(int x, int y, const char *badge, const char *text)
{
    if (strlen(badge) == 1)
    {
        Badge(x + 9, y + LineHeight(FONT_BODY) / 2, badge, C_DIM);
        x += 22;
    }
    else
    {
        // "START": a pill.
        int w = TextWidth(FONT_BODY, badge, strlen(badge)) + 12;
        RoundRect(x, y, w, LineHeight(FONT_BODY), 8, C_DIM);
        Text(FONT_BODY, x + 6, y, badge, RGB(20, 14, 24));
        x += w + 6;
    }

    Text(FONT_BODY, x, y, text, C_DIM);
}

// --- Screens --------------------------------------------------------------------------

typedef struct
{
    int x, y, w, h;
} rect_t;

static const rect_t button_download = { 14, 86, 292, 62 };
static const rect_t button_own = { 14, 156, 292, 48 };

static boolean Touched(const rect_t *r, const touchPosition *t)
{
    return t->px >= r->x && t->px < r->x + r->w
        && t->py >= r->y && t->py < r->y + r->h;
}

static void Card(const rect_t *r, boolean lit)
{
    RoundRect(r->x, r->y, r->w, r->h, 10, lit ? C_GOLD : C_BORDER);
    RoundRect(r->x + 2, r->y + 2, r->w - 4, r->h - 4, 8,
              lit ? C_PANEL_HI : C_PANEL);
}

static void Title(const char *s)
{
    Text(FONT_TITLE, 16, 12, s, C_GOLD);
}

static void DrawMenu(boolean lit_download, boolean lit_own)
{
    Background();
    Title("Game data needed");
    Paragraph(FONT_BODY, 16, 40, 290,
              "Heretic needs a game data file (a WAD) to play, and "
              "none was found in sdmc:/3ds/heretic/.", C_TEXT);

    Card(&button_download, lit_download);
    Badge(button_download.x + 22, button_download.y + 31, "A", C_GREEN_HI);
    Text(FONT_BOLD, 54, button_download.y + 8, "Download Blasphemer", C_TEXT);
    Text(FONT_BODY, 54, button_download.y + 26,
         "Free replacement game, ready to play", C_DIM);
    Text(FONT_BODY, 54, button_download.y + 42,
         "7.6 MB from GitHub, needs Wi-Fi", C_DIM);

    Card(&button_own, lit_own);
    Badge(button_own.x + 22, button_own.y + 24, "B", C_BLUE);
    Text(FONT_BOLD, 54, button_own.y + 7, "Use my own Heretic files", C_TEXT);
    Text(FONT_BODY, 54, button_own.y + 25,
         "Shows where to put HERETIC.WAD", C_DIM);

    Hint(222, 218, "START", "Quit");
}

static void DrawOwnFiles(void)
{
    static const char *const files[][2] =
    {
        { "HERETIC.WAD", "full game (DOS v1.3)" },
        { "heretic1.wad", "shareware episode" },
        { "blasphem.wad", "Blasphemer" },
    };
    unsigned int i;
    int y;

    Background();
    Title("Your own Heretic files");
    y = Paragraph(FONT_BODY, 16, 40, 290,
                  "Copy one of these files into sdmc:/3ds/heretic/ on "
                  "the SD card, then start Heretic again:", C_TEXT);

    y += 4;
    for (i = 0; i < sizeof(files) / sizeof(*files); ++i)
    {
        Circle(24, y + LineHeight(FONT_BOLD) / 2, 3, C_GOLD);
        Text(FONT_BOLD, 34, y, files[i][0], C_TEXT);
        Text(FONT_BODY, 140, y + 1, files[i][1], C_DIM);
        y += LineHeight(FONT_BOLD) + 2;
    }

    Paragraph(FONT_BODY, 16, y + 8, 290,
              "The 2025 re-release's heretic.wad doesn't work; use the "
              "one from the DOS version.", C_DIM);

    Hint(16, 218, "B", "Back");
    Hint(222, 218, "START", "Quit");
}

static void ProgressBar(int y, s64 done, s64 total)
{
    int w = 288, fill;

    RoundRect(16, y, w, 16, 8, C_TRACK);

    if (total > 0 && done > 0)
    {
        fill = (int) (done * (w - 4) / total);
        if (fill > w - 4)
        {
            fill = w - 4;
        }
        if (fill >= 12)
        {
            RoundRect(18, y + 2, fill, 12, 6, C_GREEN);
            RoundRect(18, y + 2, fill, 5, 3, C_GREEN_HI);
        }
    }
}

// Animated "..." for steps without a known length.
static const char *Dots(void)
{
    static const char *const dots[] = { "", ".", "..", "..." };
    return dots[(osGetTime() / 400) % 4];
}

static void DrawWorking(void)
{
    char line[96];
    job_state_t state = job_state;
    s64 done = job_done, total = job_total;
    static u64 start_ms;
    static s64 start_done;

    Background();

    if (state == JOB_UNPACKING)
    {
        Title("Unpacking");
    }
    else
    {
        Title("Downloading Blasphemer");
    }

    switch (state)
    {
        case JOB_DOWNLOADING:
        {
            u64 now = osGetTime();

            if (start_ms == 0 || done < start_done)
            {
                start_ms = now;
                start_done = done;
            }

            snprintf(line, sizeof(line), "%.1f of %.1f MB",
                     done / 1e6, total / 1e6);
            Text(FONT_BOLD, 16, 70, line, C_TEXT);

            // Time left from the average speed so far.
            if (now - start_ms > 2000 && done > start_done)
            {
                double rate = (double) (done - start_done)
                            / (double) (now - start_ms);
                int secs = (int) ((total - done) / rate / 1000.0) + 1;

                if (secs >= 60)
                {
                    snprintf(line, sizeof(line), "about %d min %d s left",
                             secs / 60, secs % 60);
                }
                else
                {
                    snprintf(line, sizeof(line), "about %d s left", secs);
                }
                Text(FONT_BODY, 16, 90, line, C_DIM);
            }

            ProgressBar(116, done, total);
            break;
        }

        case JOB_VERIFYING:
            snprintf(line, sizeof(line), "Checking the download%s", Dots());
            Text(FONT_BOLD, 16, 70, line, C_TEXT);
            ProgressBar(116, 1, 1);
            break;

        case JOB_UNPACKING:
            snprintf(line, sizeof(line), "Unpacking blasphem.wad%s", Dots());
            Text(FONT_BOLD, 16, 70, line, C_TEXT);
            ProgressBar(116, done, total);
            break;

        default:
            start_ms = 0;
            snprintf(line, sizeof(line), "Connecting to GitHub%s", Dots());
            Text(FONT_BOLD, 16, 70, line, C_TEXT);
            ProgressBar(116, 0, 1);
            break;
    }

    Paragraph(FONT_BODY, 16, 150, 290,
              "Blasphemer " BLASPHEMER_VERSION " is free software by the "
              "Blasphemer team (BSD licence), downloaded from its official "
              "GitHub release.", C_DIM);

    if (state != JOB_UNPACKING && state != JOB_VERIFYING)
    {
        Hint(16, 218, "B", "Cancel");
    }
}

static void DrawResult(boolean ok)
{
    Background();

    if (ok)
    {
        Title("All set!");
        Circle(160, 112, 30, C_GREEN);
        // A tick.
        {
            int i;
            for (i = 0; i < 12; ++i)
            {
                RoundRect(144 + i, 110 + i, 5, 5, 2, C_TEXT);
            }
            for (i = 0; i < 22; ++i)
            {
                RoundRect(155 + i, 121 - i, 5, 5, 2, C_TEXT);
            }
        }
        TextCentred(FONT_BOLD, 156, "Blasphemer is ready.", C_TEXT);
        TextCentred(FONT_BODY, 178, "Starting the game...", C_DIM);
    }
    else
    {
        Title("Download failed");
        Circle(32, 66, 12, C_RED);
        Text(FONT_BOLD, 29, 58, "!", C_TEXT);
        Paragraph(FONT_BODY, 54, 54, 252, job_error, C_TEXT);
        Hint(16, 218, "A", "Try again");
        Hint(130, 218, "B", "Back");
    }
}

// --- Main loop --------------------------------------------------------------------------

typedef enum
{
    SCREEN_MENU,
    SCREEN_OWN_FILES,
    SCREEN_WORKING,
    SCREEN_FAILED,
    SCREEN_DONE,
} screen_t;

static void DrawTop(void)
{
    u16 *left = (u16 *) gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    u16 *right = (u16 *) gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);
    size_t eye = getwad_top_bin_size / 2;

    memcpy(left, getwad_top_bin, eye);
    memcpy(right, getwad_top_bin + eye, eye);
}

static boolean StartJob(void)
{
    s32 prio = 0x30;

    job_cancel = false;
    job_state = JOB_CONNECTING;
    job_done = job_total = 0;

    // Below the screens' priority, on the application core.
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    job_thread = threadCreate(Job, NULL, 0x20000, prio + 1, -2, false);

    if (job_thread == NULL)
    {
        snprintf(job_error, sizeof(job_error),
                 "Couldn't start the download.");
        job_state = JOB_FAILED;
        return false;
    }

    return true;
}

static void FinishJob(void)
{
    if (job_thread != NULL)
    {
        threadJoin(job_thread, U64_MAX);
        threadFree(job_thread);
        job_thread = NULL;
    }
}

// Take both screens over from the console.
static void UIBegin(void)
{
    P3DS_ConsoleOff();
    gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
    gfxSetDoubleBuffering(GFX_BOTTOM, true);
}

static void UIPresent(void)
{
    DrawTop();
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

// Blank the top screen (both buffers, so the picture doesn't linger
// behind the game) and give the bottom one back to the console.
static void UIEnd(void)
{
    int i;

    for (i = 0; i < 2; ++i)
    {
        memset(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), 0,
               getwad_top_bin_size / 2);
        memset(gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL), 0,
               getwad_top_bin_size / 2);
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    P3DS_ConsoleRestore();
}

boolean P3DS_GetGameData(void)
{
    static const u32 soc_size = 0x100000;
    u32 *soc_buf;
    screen_t screen = SCREEN_MENU;
    boolean net = false, result = false;
    u64 done_at = 0;

    printf("No game data found: showing the download screen.\n");

    soc_buf = memalign(0x1000, soc_size);
    if (soc_buf != NULL && R_SUCCEEDED(socInit(soc_buf, soc_size)))
    {
        net = true;
    }
    acInit();
    psInit();
    UIBegin();

    while (aptMainLoop())
    {
        u32 down;
        touchPosition touch = { 0, 0 };
        boolean tapped;

        hidScanInput();
        down = hidKeysDown();
        tapped = (down & KEY_TOUCH) != 0;
        if (tapped)
        {
            hidTouchRead(&touch);
        }

        if ((down & KEY_START) && screen != SCREEN_WORKING
         && screen != SCREEN_DONE)
        {
            break;
        }

        switch (screen)
        {
            case SCREEN_MENU:
                if ((down & KEY_A) || (tapped && Touched(&button_download,
                                                         &touch)))
                {
                    if (!net)
                    {
                        snprintf(job_error, sizeof(job_error),
                                 "Couldn't start the network service.");
                        screen = SCREEN_FAILED;
                    }
                    else if (StartJob())
                    {
                        screen = SCREEN_WORKING;
                    }
                    else
                    {
                        screen = SCREEN_FAILED;
                    }
                }
                else if ((down & KEY_B)
                      || (tapped && Touched(&button_own, &touch)))
                {
                    screen = SCREEN_OWN_FILES;
                }
                break;

            case SCREEN_OWN_FILES:
                if (down & (KEY_B | KEY_TOUCH))
                {
                    screen = SCREEN_MENU;
                }
                break;

            case SCREEN_WORKING:
                if ((down & KEY_B) && job_state != JOB_UNPACKING
                 && job_state != JOB_VERIFYING)
                {
                    job_cancel = true;
                }

                if (job_state == JOB_DONE || job_state == JOB_FAILED
                 || job_state == JOB_CANCELLED)
                {
                    job_state_t s = job_state;

                    FinishJob();
                    screen = s == JOB_DONE ? SCREEN_DONE
                           : s == JOB_FAILED ? SCREEN_FAILED : SCREEN_MENU;
                    done_at = osGetTime();
                }
                break;

            case SCREEN_FAILED:
                if (down & KEY_A)
                {
                    screen = StartJob() ? SCREEN_WORKING : SCREEN_FAILED;
                }
                else if (down & KEY_B)
                {
                    screen = SCREEN_MENU;
                }
                break;

            case SCREEN_DONE:
                break;
        }

        if (screen == SCREEN_DONE && osGetTime() - done_at > 1500)
        {
            result = true;
            break;
        }

        bot = (u16 *) gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);

        switch (screen)
        {
            case SCREEN_MENU:
                DrawMenu(false, false);
                break;
            case SCREEN_OWN_FILES:
                DrawOwnFiles();
                break;
            case SCREEN_WORKING:
                DrawWorking();
                break;
            case SCREEN_FAILED:
                DrawResult(false);
                break;
            case SCREEN_DONE:
                DrawResult(true);
                break;
        }

        UIPresent();
    }

    // Leaving: stop a download still running (HOME menu close).
    if (job_thread != NULL)
    {
        job_cancel = true;
        FinishJob();
    }

    psExit();
    acExit();
    if (net)
    {
        socExit();
    }
    free(soc_buf);
    UIEnd();

    if (result)
    {
        printf("Blasphemer %s downloaded to %s.\n", BLASPHEMER_VERSION,
               WAD_FINAL);
    }
    else
    {
        // START (or the HOME menu's close): leave quietly.
        exit(0);
    }

    return result;
}

// --- Game chooser ------------------------------------------------------------------------
//
// Shown when more than one IWAD is on the SD card.  The last choice is
// remembered in last_game.txt and highlighted next time.

#define LAST_GAME_FILE P3DS_DATA_DIR "/last_game.txt"
#define MAX_GAMES 4

static const char *BaseName(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

// A friendlier title and a one-line description for the known IWADs.
static void DescribeGame(const char *path, const char *fallback,
                         const char **title, const char **what)
{
    const char *base = BaseName(path);

    if (!strcasecmp(base, "heretic.wad"))
    {
        *title = "Heretic";
        *what = "Full game";
    }
    else if (!strcasecmp(base, "heretic1.wad"))
    {
        *title = "Heretic (shareware)";
        *what = "Episode 1";
    }
    else if (!strcasecmp(base, "blasphem.wad"))
    {
        *title = "Blasphemer";
        *what = "Free replacement game";
    }
    else
    {
        *title = fallback;
        *what = "";
    }
}

static void GameCardRect(int i, int count, rect_t *r)
{
    int h = count <= 2 ? 58 : count == 3 ? 44 : 34;

    r->x = 14;
    r->w = 292;
    r->h = h;
    r->y = 64 + i * (h + 6);
}

static void DrawChooser(int count, const char *const *paths,
                        const char *const *titles, const s64 *sizes,
                        int selected)
{
    int i;

    Background();
    Title("Choose a game");
    Text(FONT_BODY, 16, 40, "More than one game was found on the SD card.",
         C_TEXT);

    for (i = 0; i < count; ++i)
    {
        const char *title, *what;
        char line[128];
        rect_t r;
        int text_y;

        GameCardRect(i, count, &r);
        DescribeGame(paths[i], titles[i], &title, &what);
        Card(&r, i == selected);

        snprintf(line, sizeof(line), "%s%s%.1f MB", what,
                 what[0] ? ", " : "", sizes[i] / 1e6);

        // Centre the one or two lines in the card; the file name goes
        // after the title, leaving room for the A badge on the right.
        text_y = r.h >= 44 ? r.y + (r.h - 36) / 2 : r.y + (r.h - 18) / 2;
        Text(FONT_BOLD, 22, text_y, title, C_TEXT);
        Text(FONT_BODY, 22 + TextWidth(FONT_BOLD, title, strlen(title)) + 8,
             text_y + 2, BaseName(paths[i]), C_DIM);
        if (r.h >= 44)
        {
            Text(FONT_BODY, 22, text_y + 19, line, C_DIM);
        }

        if (i == selected)
        {
            Badge(r.x + r.w - 24, r.y + r.h / 2, "A", C_GREEN_HI);
        }
    }

    Hint(16, 218, "A", "Play");
    Text(FONT_BODY, 92, 218, "Up/Down: choose", C_DIM);
    Hint(222, 218, "START", "Quit");
}

int P3DS_ChooseGame(int count, const char *const *paths,
                    const char *const *titles)
{
    s64 sizes[MAX_GAMES];
    char last[64] = "";
    int selected = 0, chosen = -1, i;
    FILE *f;

    if (count > MAX_GAMES)
    {
        count = MAX_GAMES;
    }

    for (i = 0; i < count; ++i)
    {
        struct stat st;
        sizes[i] = stat(paths[i], &st) == 0 ? (s64) st.st_size : 0;
    }

    // Start on the game played last time.
    f = fopen(LAST_GAME_FILE, "r");
    if (f != NULL)
    {
        if (fgets(last, sizeof(last), f) != NULL)
        {
            last[strcspn(last, "\r\n")] = '\0';
        }
        fclose(f);
    }
    for (i = 0; i < count; ++i)
    {
        if (!strcasecmp(BaseName(paths[i]), last))
        {
            selected = i;
        }
    }

    printf("More than one IWAD: showing the game chooser.\n");
    UIBegin();

    while (chosen < 0 && aptMainLoop())
    {
        u32 down;

        hidScanInput();
        down = hidKeysDown();

        if (down & KEY_START)
        {
            break;
        }
        if (down & (KEY_DUP | KEY_CPAD_UP))
        {
            selected = (selected + count - 1) % count;
        }
        if (down & (KEY_DDOWN | KEY_CPAD_DOWN))
        {
            selected = (selected + 1) % count;
        }
        if (down & KEY_A)
        {
            chosen = selected;
        }
        if (down & KEY_TOUCH)
        {
            touchPosition t;

            hidTouchRead(&t);
            for (i = 0; i < count; ++i)
            {
                rect_t r;

                GameCardRect(i, count, &r);
                if (Touched(&r, &t))
                {
                    selected = chosen = i;
                }
            }
        }

        bot = (u16 *) gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        DrawChooser(count, paths, titles, sizes, selected);
        UIPresent();
    }

    UIEnd();

    if (chosen < 0)
    {
        // START (or the HOME menu's close): leave quietly.
        exit(0);
    }

    f = fopen(LAST_GAME_FILE, "w");
    if (f != NULL)
    {
        fprintf(f, "%s\n", BaseName(paths[chosen]));
        fclose(f);
    }

    printf("Chosen game: %s\n", paths[chosen]);
    return chosen;
}
