#define _CRT_SECURE_NO_WARNINGS // для отключения “This function or variable may be unsafe”
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
    buf[0] = 0; // очистка выходного буфера
    if (!query) return NULL;
    size_t klen = strlen(key); // Запоминаем длину имени параметра

    const char *p = query; // Указатель на текущий сегмент
    while (*p) {
        const char *amp = strchr(p, '&'); // ищем конец текущего сегмента
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
                url_decode(buf, cap, tmp); // URL-декодирование
                return buf;
            }
        }
        // Переход к следующему сегменту
        if (!amp) break;
        p = amp + 1;
    }
    return NULL;
}

static void send_all(SOCKET s, const char *data, int len) { // пытается отправить все len байт по сокету, даже если send() отправляет их частями.
    int sent = 0;
    while (sent < len) {
        int r = send(s, data + sent, len - sent, 0);
        if (r <= 0) return;
        sent += r;
    }
}

static void send_text(SOCKET s, const char *status, const char *ctype, const char *body) { // формирует и отправляет ответ с текстом по сокету.
    char header[256];
    int blen = (int)strlen(body); // читаем количество байт, которые отправим
    int hlen = snprintf(header, sizeof(header), // возвращает сколько байт реально записано
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        status, ctype, blen
    );
    send_all(s, header, hlen); // гарантируем, что все заголовки уйдут
    send_all(s, body, blen); // отправляем ровно столько байт, сколько указали
}

static void send_file(SOCKET s, const char *path, const char *ctype) { // отправляем всё что не текст
    FILE *f = fopen(path, "rb");
    if (!f) { send_text(s, "404 Not Found", "text/plain; charset=utf-8", "Not found"); return; }

    // Узнаём размер файла
    fseek(f, 0, SEEK_END); // прыгнуть в конец файла
    long sz = ftell(f); // узнать текущую позицию = размер файла
    fseek(f, 0, SEEK_SET); // вернуться обратно в начало, чтобы начать читать

    // Формируем HTTP-заголовки
    char header[256];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n",
        ctype, sz
    );
    // Отправляем и гарантируем, что заголовки уйдут целиком.
    send_all(s, header, hlen);

    char buf[8192]; // Буфер для чтения кусков файла
    size_t n; // сколько байт реально прочитали
    // Читаем файл кусками и отправляем
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        send_all(s, buf, (int)n);
    }
    fclose(f);
}

// Нам нужен нормальный редирект, а не просто окно с надписью редирект
static void send_redirect(SOCKET s, const char *location) {
    char header[256];
    int hlen = snprintf(header, sizeof(header),
        // Смотри заголовок Location
        "HTTP/1.1 302 Found\r\n"
        // отправляем ему / + метод
        "Location: %s\r\n"
        "Connection: close\r\n\r\n",
        // Подставляется вместо %s
        location
    );
    // Отправка заголовков
    send_all(s, header, hlen);
}

static int clamp(int v, int lo, int hi) { if (v < lo) return lo; if (v > hi) return hi; return v; } // защита от слишком больших и маленьких чисел для параметров

int run_server(CardList *cards, const char *csv_path, int port) { // озвращает 0 (успех) или -1 (ошибка запуска)
#ifdef _WIN32 // сервер реализован только под виндоус
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return -1; // вызов всастартап для пользования сокетами под виндовс, выбираем версию мейкворд 2.2

    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // сокет, который слушает входящие подключения
    if (srv == INVALID_SOCKET) return -1;

    // Настройка на каком порту слушать
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) != 0) return -1;
    if (listen(srv, 16) != 0) return -1;

    printf("Server: http://localhost:%d\n", port); // пишем в консоль куда заходить

    for (;;) { // бесконечный цикл для жизни сервера
        // Принять клиента и привязать к нему трубу-клиентский сокет
        SOCKET cli = accept(srv, NULL, NULL);
        if (cli == INVALID_SOCKET) continue;

        // Получить запрос в буфер
        char req[4096] = {0};
        int r = recv(cli, req, sizeof(req)-1, 0);
        if (r <= 0) { closesocket(cli); continue; }
        req[r] = 0;

        // Парсинг первой строки HTTP вытаскиваем GET и делим на путь что слать надо (url + method)
        char method[8], url[1024];
        if (sscanf(req, "%7s %1023s", method, url) != 2) { closesocket(cli); continue; }
        // Проверка что только метод GET
        if (strcmp(method, "GET") != 0) { send_text(cli, "405 Method Not Allowed", "text/plain", "GET only"); closesocket(cli); continue; }

        // ищем query string
        char *qmark = strchr(url, '?');
        // Подготовка буферов
        char path[1024] = {0};
        char query[2048] = {0};
        // Если ? есть то делим URL /add?title=Cat&rarity=3
        if (qmark) {
            size_t plen = (size_t)(qmark - url);
            memcpy(path, url, plen); path[plen] = 0;
            strncpy(query, qmark + 1, sizeof(query)-1);
        // Если ? нет то весь URL это путь GET /images/cat.jpg HTTP/1.1
        } else {
            strncpy(path, url, sizeof(path)-1);
        }

        // route: images побайтно читаем изображения
        if (strncmp(path, "/images/", 8) == 0) {
            char fp[1200];
            snprintf(fp, sizeof(fp), "images/%s", path + 8);
            // можно только жпг
            send_file(cli, fp, "image/jpeg");
            closesocket(cli);
            continue;
        }

        // route: add
        if (strcmp(path, "/add") == 0) {
            // Буферы под параметры
            char b_title[256], b_image[256], b_tag[128];
            char b_r[32], b_c[32], b_f[32], b_ch[32];

            // Вытаскивание параметров из query string
            qs_get(query, "title", b_title, sizeof(b_title));
            qs_get(query, "image", b_image, sizeof(b_image));
            qs_get(query, "tag", b_tag, sizeof(b_tag));
            qs_get(query, "rarity", b_r, sizeof(b_r));
            qs_get(query, "cuteness", b_c, sizeof(b_c));
            qs_get(query, "fun", b_f, sizeof(b_f));
            qs_get(query, "chaos", b_ch, sizeof(b_ch));

            // Создание карточки
            Card c = {0};
            // Копирование строковых полей в структуру
            strncpy(c.title, b_title, sizeof(c.title)-1);
            strncpy(c.image, b_image, sizeof(c.image)-1);
            strncpy(c.tag, b_tag, sizeof(c.tag)-1);

            // Преобразование чисел atoi + ограничение диапазона clamp
            c.rarity = clamp(atoi(b_r), 1, 5);
            c.cuteness = clamp(atoi(b_c), 1, 10);
            c.fun = clamp(atoi(b_f), 1, 10);
            c.chaos = clamp(atoi(b_ch), 1, 10);

            // Добавить и сохранить если имеется минимум нужных данных
            if (c.title[0] && c.image[0]) {
                cards_add(cards, c);
                cards_save_csv(cards, csv_path);
            }

            // redirect to /
            send_redirect(cli, "/");
            closesocket(cli);
            continue;
        }

        // route: craft
        if (strcmp(path, "/craft") == 0) {
            // Буферы под параметры
            char b1[32], b2[32];
            // Вытаскиваем параметры из query
            qs_get(query, "id1", b1, sizeof(b1));
            qs_get(query, "id2", b2, sizeof(b2));
            // Переводим id в числа
            int id1 = atoi(b1), id2 = atoi(b2);
            // Готовим переменную под результат крафта
            Card n;
            // Пытаемся скрафтить
            if (cards_craft(cards, id1, id2, &n) == 0) {
                cards_save_csv(cards, csv_path);
            }
            // опять редирект
            send_redirect(cli, "/");
            closesocket(cli);
            continue;
        }

        // index: apply sort/filter ПРИМЕР /?sort=rarity&order=asc&tag=cute&rarity_min=3
        char b_sort[64], b_order[16], b_tag[64], b_rmin[16], b_imgfx[16];
        const char *sort = qs_get(query, "sort", b_sort, sizeof(b_sort));
        const char *order = qs_get(query, "order", b_order, sizeof(b_order));
        const char *tag = qs_get(query, "tag", b_tag, sizeof(b_tag));
        const char *rmin = qs_get(query, "rarity_min", b_rmin, sizeof(b_rmin));
        const char *imgfx = qs_get(query, "imgfx", b_imgfx, sizeof(b_imgfx));

        // apply sort
        const char *skey = (sort && *sort) ? sort : "id"; // Выбор ключа сортировки
        int desc = 1;
        // Выбираем сортировку по убыванию или возрастанию
        if (order && strcmp(order, "asc") == 0) desc = 0;
        // Вызвали сортировку
        cards_sort(cards, skey, desc);

        // Создаёт временный CardList view, который будет содержать только карточки для показа на странице.
        CardList view = {0};
        // Выделение памяти под карточки
        view.data = malloc(cards->count * sizeof(Card));
        // В этот массив можно положить до cards->count карточек
        view.capacity = cards->count;
        // Обнуление счётчика элементов
        view.count = 0;

        // Подготовка rarity_min как числа
        int rminv = (rmin && *rmin) ? atoi(rmin) : 0;

        // Перебор всех карточек
        for (size_t i = 0; i < cards->count; i++) {
            // Берём текущую карточку, копируем в локальную переменную c
            Card c = cards->data[i];
            // подходит ли /?tag=cute&rarity_min=4
            int ok = 1;
            if (tag && *tag) ok = (strcmp(c.tag, tag) == 0);
            if (ok && rminv > 0) ok = (c.rarity >= rminv);
            // Если подходит - добавляем в “витрину”
            if (ok) view.data[view.count++] = c;
        }

        // Буфер под HTML
        char html[200000];
        // Генерация страницы
        render_index(html, sizeof(html), &view, skey, order, tag, rmin, imgfx);

        // Освобождение временной памяти
        free(view.data);

        // Отправка ответа браузеру
        send_text(cli, "200 OK", "text/html; charset=utf-8", html);
        closesocket(cli);
    }

    // конец бесконечного цикла
    closesocket(srv);
    WSACleanup();
    return 0;

// подавление предупреждений компилятора в случаях else
#else
    (void)cards; (void)csv_path; (void)port;
    return -1;
#endif
}
