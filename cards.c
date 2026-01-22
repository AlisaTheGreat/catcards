#include "cards.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Инициализируем наш список-массив с карточками (начальное состояние)
void cards_init(CardList *list) {
    list->data = NULL;
    list->count = 0;
    list->capacity = 0;
}

// Освобождаем память, уничтожение / очистка
void cards_free(CardList *list) {
    free(list->data);
    list->data = NULL;
    list->count = 0;
    list->capacity = 0;
}

// Функция для выделения памяти только внутри файла
static void ensure_capacity(CardList *list) {
    if (list->count < list->capacity) return;

    size_t new_cap = (list->capacity == 0) ? 16 : list->capacity * 2;
    Card *new_data = realloc(list->data, new_cap * sizeof(Card));
    if (!new_data) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }
    list->data = new_data;
    list->capacity = new_cap;
}

// Загружаем файл на чтение
int cards_load_csv(CardList *list, const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("Cannot open cards.csv");
        return -1;
    }

    char line[512]; // буфер стандартного размера для чтения одной строки из файла

    // пропускаем заголовок id;title;image;rarity;cuteness;fun;chaos;tag
    fgets(line, sizeof(line), f);

    while (fgets(line, sizeof(line), f)) {
        Card c = {0};

        char *token = strtok(line, ";");
        if (!token) continue;
        c.id = atoi(token); // переводит строку в целое число

        token = strtok(NULL, ";");
        strncpy(c.title, token, sizeof(c.title) - 1);

        token = strtok(NULL, ";");
        strncpy(c.image, token, sizeof(c.image) - 1);

        token = strtok(NULL, ";");
        c.rarity = atoi(token);

        token = strtok(NULL, ";");
        c.cuteness = atoi(token);

        token = strtok(NULL, ";");
        c.fun = atoi(token);

        token = strtok(NULL, ";");
        c.chaos = atoi(token);

        token = strtok(NULL, ";\n");
        strncpy(c.tag, token, sizeof(c.tag) - 1);

        ensure_capacity(list); // проверяет хватает ли памяти
        list->data[list->count++] = c;
    }

    fclose(f);
    return 0;
}
// Печать для проверки что все карточки прочитали
void cards_print(const CardList *list) {
    for (size_t i = 0; i < list->count; i++) {
        const Card *c = &list->data[i];
        printf(
            "[%d] %s | %s | r=%d c=%d f=%d ch=%d | %s\n",
            c->id, c->title, c->image,
            c->rarity, c->cuteness, c->fun, c->chaos, c->tag
        );
    }
}

// СОРТИРОВКА

static const char *g_sort_key = NULL;
static int g_sort_desc = 1; // 1 = desc, 0 = asc

// Либо 1 либо 0 либо -1
static int cmp_int(int a, int b) { return (a > b) - (a < b); }

// qsort передаёт указатели на элементы массива как void*. Приводим их обратно к Card*.
static int card_cmp(const void *pa, const void *pb) {
    const Card *a = (const Card*)pa;
    const Card *b = (const Card*)pb;

    int r = 0;

    if (strcmp(g_sort_key, "cuteness") == 0) r = cmp_int(a->cuteness, b->cuteness);
    else if (strcmp(g_sort_key, "rarity") == 0) r = cmp_int(a->rarity, b->rarity);
    else if (strcmp(g_sort_key, "fun") == 0) r = cmp_int(a->fun, b->fun);
    else if (strcmp(g_sort_key, "chaos") == 0) r = cmp_int(a->chaos, b->chaos);
    else if (strcmp(g_sort_key, "title") == 0) r = strcmp(a->title, b->title);
    else r = cmp_int(a->id, b->id);

    // если равны по ключу - стабилизируем по id
    if (r == 0) r = cmp_int(a->id, b->id);

    // qsort: возвращаем <0 если a<b
    // для descending просто инвертируем
    if (g_sort_desc) r = -r;
    return r;
}

void cards_sort(CardList *list, const char *key, int descending) {
    if (!list || list->count == 0) return;
    if (!key) key = "id";
    g_sort_key = key;
    g_sort_desc = descending ? 1 : 0;
    qsort(list->data, list->count, sizeof(Card), card_cmp);
}

//ВИЗУАЛ
// сохранить список карточек обратно в csv
int cards_save_csv(const CardList *list, const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) return -1;

    fprintf(f, "id;title;image;rarity;cuteness;fun;chaos;tag\n");
    for (size_t i = 0; i < list->count; i++) {
        const Card *c = &list->data[i];
        fprintf(f, "%d;%s;%s;%d;%d;%d;%d;%s\n",
                c->id, c->title, c->image,
                c->rarity, c->cuteness, c->fun, c->chaos, c->tag);
    }
    fclose(f);
    return 0;
}

// Находит максимальный id и делает следующий.
static int next_id(const CardList *list) {
    int mx = 0;
    for (size_t i = 0; i < list->count; i++) if (list->data[i].id > mx) mx = list->data[i].id;
    return mx + 1;
}

int cards_add(CardList *list, Card c) {
    c.id = next_id(list);

    extern void cards_init(CardList *list);

    if (list->count >= list->capacity) {
        size_t new_cap = (list->capacity == 0) ? 16 : list->capacity * 2; // Считаем новый размер
        Card *new_data = realloc(list->data, new_cap * sizeof(Card)); // Расширяем память
        if (!new_data) return -1;
        list->data = new_data;
        list->capacity = new_cap;
    }

    list->data[list->count++] = c; // Записываем новую карточку в конец
    return 0;
}

static Card* find_by_id(CardList *list, int id) {
    for (size_t i = 0; i < list->count; i++) {
        if (list->data[i].id == id) return &list->data[i];
    }
    return NULL;
}

int cards_craft(CardList *list, int id1, int id2, Card *out_new) {
    if (id1 == id2) return -1;
    Card *a = find_by_id(list, id1);
    Card *b = find_by_id(list, id2);
    if (!a || !b) return -1;

    Card n = {0};
    n.id = next_id(list);

    snprintf(n.title, sizeof(n.title), "Craft: %s + %s", a->title, b->title);
    // берём картинку первой
    strncpy(n.image, a->image, sizeof(n.image)-1);

    int rmax = (a->rarity > b->rarity) ? a->rarity : b->rarity;
    n.rarity = rmax + 1; if (n.rarity > 5) n.rarity = 5;

    n.cuteness = (a->cuteness + b->cuteness) / 2;
    n.fun      = (a->fun > b->fun) ? a->fun : b->fun;
    n.chaos    = (a->chaos > b->chaos) ? a->chaos : b->chaos;

    strncpy(n.tag, "craft", sizeof(n.tag)-1);

    if (cards_add(list, n) != 0) return -1;
    if (out_new) *out_new = n;
    return 0;
}

//
// Created by Admin on 20.01.2026.
//
