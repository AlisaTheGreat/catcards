#include <stdio.h>
#include "src/cards.h"
#include "src/server.h"

int main(void) {
    CardList cards;
    cards_init(&cards);

    if (cards_load_csv(&cards, "cards.csv") != 0) {
        printf("Failed to load cards.csv\n");
        return 1;
    }

    // сайт на localhost:8080
    run_server(&cards, "cards.csv", 8080);

    cards_free(&cards);
    return 0;
}
