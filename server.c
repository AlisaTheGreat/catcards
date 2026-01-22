#define _CRT_SECURE_NO_WARNINGS
#include "server.h"
#include "render.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib,"ws2_32.lib")
#endif

static void url_decode(char *dst, size_t cap, const char *src) { // Декодер, чтобы на сайте смотрелось норм
    size_t di = 0;
    for (size_t si = 0; src[si] && di + 1 < cap; si++) {
        if (src[si] == '+') dst[di++] = ' '; // для данных HTML-форм.
        else if (src[si] == '%' && src[si+1] && src[si+2]) { // надо по правилам декодировать два символа после
            char hex[3] = { src[si+1], src[si+2], 0 }; // Создаём строку из двух символов + '\0'
            dst[di++] = (char)strtol(hex, NULL, 16); // Преобразование hex в число через strtol а потом через чар
            si += 2; // Пропускаем два символа
        } else dst[di++] = src[si]; // копируем символ как есть
    }
    dst[di] = 0;
}

static const char* qs_get(const char *query, const char *key, char *buf, size_t cap) { // функция извлечения значения параметра из query string
    buf[0] = 0;
    if (!query) return NULL;
    size_t klen = strlen(key);

    const char *p = query;
    while (*p) {
        const char *amp = strchr(p, '&');
        size_t seglen = amp ? (size_t)(amp - p) : strlen(p);

        const char *eq = memchr(p, '=', seglen);
        if (eq) {
            size_t nlen = (size_t)(eq - p);
            if (nlen == klen && strncmp(p, key, klen) == 0) {
                char tmp[512] = {0};
                size_t vlen = seglen - nlen - 1;
                if (vlen >= sizeof(tmp)) vlen = sizeof(tmp)-1;
                memcpy(tmp, eq + 1, vlen);
                tmp[vlen] = 0;
                url_decode(buf, cap, tmp);
                return buf;
            }
        }

        if (!amp) break;
        p = amp + 1;
    }
    return NULL;
}

static void send_all(SOCKET s, const char *data, int len) {
    int sent = 0;
    while (sent < len) {
        int r = send(s, data + sent, len - sent, 0);
        if (r <= 0) return;
        sent += r;
    }
}

static void send_text(SOCKET s, const char *status, const char *ctype, const char *body) {
    char header[256];
    int blen = (int)strlen(body);
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        status, ctype, blen
    );
    send_all(s, header, hlen);
    send_all(s, body, blen);
}

static void send_file(SOCKET s, const char *path, const char *ctype) {
    FILE *f = fopen(path, "rb");
    if (!f) { send_text(s, "404 Not Found", "text/plain; charset=utf-8", "Not found"); return; }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char header[256];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n",
        ctype, sz
    );
    send_all(s, header, hlen);

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        send_all(s, buf, (int)n);
    }
    fclose(f);
}

static int clamp(int v, int lo, int hi) { if (v < lo) return lo; if (v > hi) return hi; return v; }

int run_server(CardList *cards, const char *csv_path, int port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return -1;

    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) != 0) return -1;
    if (listen(srv, 16) != 0) return -1;

    printf("Server: http://localhost:%d\n", port);

    for (;;) {
        SOCKET cli = accept(srv, NULL, NULL);
        if (cli == INVALID_SOCKET) continue;

        char req[4096] = {0};
        int r = recv(cli, req, sizeof(req)-1, 0);
        if (r <= 0) { closesocket(cli); continue; }
        req[r] = 0;

        // first line: GET /path?query HTTP/1.1
        char method[8], url[1024];
        if (sscanf(req, "%7s %1023s", method, url) != 2) { closesocket(cli); continue; }
        if (strcmp(method, "GET") != 0) { send_text(cli, "405 Method Not Allowed", "text/plain", "GET only"); closesocket(cli); continue; }

        char *qmark = strchr(url, '?');
        char path[1024] = {0};
        char query[2048] = {0};
        if (qmark) {
            size_t plen = (size_t)(qmark - url);
            memcpy(path, url, plen); path[plen] = 0;
            strncpy(query, qmark + 1, sizeof(query)-1);
        } else {
            strncpy(path, url, sizeof(path)-1);
        }

        // route: images
        if (strncmp(path, "/images/", 8) == 0) {
            char fp[1200];
            snprintf(fp, sizeof(fp), "images/%s", path + 8);
            // basic content-type
            send_file(cli, fp, "image/jpeg");
            closesocket(cli);
            continue;
        }

        // route: add
        if (strcmp(path, "/add") == 0) {
            char b_title[256], b_image[256], b_tag[128];
            char b_r[32], b_c[32], b_f[32], b_ch[32];

            qs_get(query, "title", b_title, sizeof(b_title));
            qs_get(query, "image", b_image, sizeof(b_image));
            qs_get(query, "tag", b_tag, sizeof(b_tag));
            qs_get(query, "rarity", b_r, sizeof(b_r));
            qs_get(query, "cuteness", b_c, sizeof(b_c));
            qs_get(query, "fun", b_f, sizeof(b_f));
            qs_get(query, "chaos", b_ch, sizeof(b_ch));

            Card c = {0};
            strncpy(c.title, b_title, sizeof(c.title)-1);
            strncpy(c.image, b_image, sizeof(c.image)-1);
            strncpy(c.tag, b_tag, sizeof(c.tag)-1);
            c.rarity = clamp(atoi(b_r), 1, 5);
            c.cuteness = clamp(atoi(b_c), 1, 10);
            c.fun = clamp(atoi(b_f), 1, 10);
            c.chaos = clamp(atoi(b_ch), 1, 10);

            if (c.title[0] && c.image[0]) {
                cards_add(cards, c);
                cards_save_csv(cards, csv_path);
            }

            // redirect to /
            send_text(cli, "302 Found", "text/plain", "Redirect");
            closesocket(cli);
            continue;
        }

        // route: craft
        if (strcmp(path, "/craft") == 0) {
            char b1[32], b2[32];
            qs_get(query, "id1", b1, sizeof(b1));
            qs_get(query, "id2", b2, sizeof(b2));
            int id1 = atoi(b1), id2 = atoi(b2);
            Card n;
            if (cards_craft(cards, id1, id2, &n) == 0) {
                cards_save_csv(cards, csv_path);
            }
            send_text(cli, "302 Found", "text/plain", "Redirect");
            closesocket(cli);
            continue;
        }

        // index: apply sort/filter (in-place sort for now)
        char b_sort[64], b_order[16], b_tag[64], b_rmin[16], b_imgfx[16];
        const char *sort = qs_get(query, "sort", b_sort, sizeof(b_sort));
        const char *order = qs_get(query, "order", b_order, sizeof(b_order));
        const char *tag = qs_get(query, "tag", b_tag, sizeof(b_tag));
        const char *rmin = qs_get(query, "rarity_min", b_rmin, sizeof(b_rmin));
        const char *imgfx = qs_get(query, "imgfx", b_imgfx, sizeof(b_imgfx));

        // apply sort (default)
        const char *skey = (sort && *sort) ? sort : "id";
        int desc = 1;
        if (order && strcmp(order, "asc") == 0) desc = 0;
        cards_sort(cards, skey, desc);

        // very simple filtering for display: we copy into temp
        CardList view = {0};
        view.data = malloc(cards->count * sizeof(Card));
        view.capacity = cards->count;
        view.count = 0;

        int rminv = (rmin && *rmin) ? atoi(rmin) : 0;

        for (size_t i = 0; i < cards->count; i++) {
            Card c = cards->data[i];
            int ok = 1;
            if (tag && *tag) ok = (strcmp(c.tag, tag) == 0);
            if (ok && rminv > 0) ok = (c.rarity >= rminv);
            if (ok) view.data[view.count++] = c;
        }

        char html[200000];
        render_index(html, sizeof(html), &view, skey, order, tag, rmin, imgfx);

        free(view.data);

        send_text(cli, "200 OK", "text/html; charset=utf-8", html);
        closesocket(cli);
    }

    // never reached
    closesocket(srv);
    WSACleanup();
    return 0;
#else
    (void)cards; (void)csv_path; (void)port;
    return -1;
#endif
}
//
// Created by Admin on 20.01.2026.
//
