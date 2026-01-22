#ifndef CARDS_H
#define CARDS_H

#include <stddef.h>

typedef struct {
    int id;
    char title[128];
    char image[128];
    int rarity;     // 1..5
    int cuteness;   // 1..10
    int fun;        // 1..10
    int chaos;      // 1..10
    char tag[64];
} Card;

typedef struct {
    Card *data;
    size_t count;
    size_t capacity;
} CardList;

void cards_init(CardList *list);
void cards_free(CardList *list);

int  cards_load_csv(CardList *list, const char *filename);
int  cards_save_csv(const CardList *list, const char *filename);

void cards_sort(CardList *list, const char *key, int descending);

int  cards_add(CardList *list, Card c);           // auto-id
int  cards_craft(CardList *list, int id1, int id2, Card *out_new);

#endif
