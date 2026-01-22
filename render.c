#include "render.h"
#include <stdio.h>
#include <string.h>

static const char* safe(const char* s){ return s ? s : ""; }

void render_index(char *out, size_t cap, const CardList *cards,
                  const char *sort_key, const char *order,
                  const char *tag, const char *rarity_min,
                  const char *imgfx)
{
    const char *fx = safe(imgfx);
    const char *img_class = "";
    if (strcmp(fx,"gray")==0) img_class = "fx-gray";
    else if (strcmp(fx,"sepia")==0) img_class = "fx-sepia";

    size_t n = 0;

    n += snprintf(out+n, cap-n,
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<title>Cat Cards</title>"
        "<style>"
        "body{font-family:system-ui,Segoe UI,Arial;max-width:1100px;margin:24px auto;padding:0 16px;}"
        ".bar{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px;}"
        "a.btn,button.btn{display:inline-block;padding:10px 12px;border:1px solid #ddd;border-radius:12px;background:#fff;text-decoration:none;color:#111;}"
        "a.btn:hover,button.btn:hover{background:#f6f6f6;}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:12px;}"
        ".card{border:1px solid #e6e6e6;border-radius:16px;overflow:hidden;box-shadow:0 1px 6px rgba(0,0,0,.06);background:#fff;}"
        ".card img{width:100%%;height:180px;object-fit:cover;display:block;}"
        ".fx-gray{filter:grayscale(100%%);}"
        ".fx-sepia{filter:sepia(85%%);}"
        ".p{padding:10px 12px;}"
        ".meta{color:#555;font-size:13px;margin-top:6px;}"
        "input,select{padding:10px 12px;border:1px solid #ddd;border-radius:12px;}"
        "form{display:flex;gap:8px;flex-wrap:wrap;align-items:center;}"
        "</style></head><body>"
        "<h1>Cat Cards</h1>"
        "<div class='bar'>"
        "<a class='btn' href='/?sort=cuteness&order=desc'>Sort cuteness</a>"
        "<a class='btn' href='/?sort=rarity&order=desc'>Sort rarity</a>"
        "<a class='btn' href='/?sort=fun&order=desc'>Sort fun</a>"
        "<a class='btn' href='/?sort=chaos&order=desc'>Sort chaos</a>"
        "<a class='btn' href='/?sort=title&order=asc'>Sort title</a>"
        "</div>"
        "<div class='bar'>"
        "<a class='btn' href='/?imgfx=none'>Img: none</a>"
        "<a class='btn' href='/?imgfx=gray'>Img: gray</a>"
        "<a class='btn' href='/?imgfx=sepia'>Img: sepia</a>"
        "</div>"
        "<h3>Filter</h3>"
        "<form method='GET' action='/'>"
        "<input name='tag' placeholder='tag (sleep/play/food...)' value='%s'>"
        "<input name='rarity_min' placeholder='rarity_min (e.g. 4)' value='%s'>"
        "<input name='sort' placeholder='sort key' value='%s'>"
        "<input name='order' placeholder='asc/desc' value='%s'>"
        "<input name='imgfx' placeholder='none/gray/sepia' value='%s'>"
        "<button class='btn' type='submit'>Apply</button>"
        "</form>",
        safe(tag), safe(rarity_min), safe(sort_key), safe(order), safe(imgfx)
    );

    n += snprintf(out+n, cap-n,
        "<h3>Add card</h3>"
        "<form method='GET' action='/add'>"
        "<input name='title' placeholder='title'>"
        "<input name='image' placeholder='image file (jpg)'>"
        "<input name='rarity' placeholder='rarity 1..5'>"
        "<input name='cuteness' placeholder='cuteness 1..10'>"
        "<input name='fun' placeholder='fun 1..10'>"
        "<input name='chaos' placeholder='chaos 1..10'>"
        "<input name='tag' placeholder='tag'>"
        "<button class='btn' type='submit'>Add</button>"
        "</form>"
    );

    n += snprintf(out+n, cap-n,
        "<h3>Craft</h3>"
        "<form method='GET' action='/craft'>"
        "<input name='id1' placeholder='id1'>"
        "<input name='id2' placeholder='id2'>"
        "<button class='btn' type='submit'>Craft</button>"
        "</form>"
        "<h3>Cards (%zu)</h3><div class='grid'>",
        cards->count
    );

    for (size_t i = 0; i < cards->count && n < cap; i++) {
        const Card *c = &cards->data[i];
        n += snprintf(out+n, cap-n,
            "<div class='card'>"
            "<img class='%s' src='/images/%s' alt='img'>"
            "<div class='p'>"
            "<b>#%d %s</b>"
            "<div class='meta'>rarity=%d • cuteness=%d • fun=%d • chaos=%d • tag=%s</div>"
            "</div></div>",
            img_class, c->image, c->id, c->title,
            c->rarity, c->cuteness, c->fun, c->chaos, c->tag
        );
    }

    snprintf(out+n, cap-n, "</div></body></html>");
}
//
// Created by Admin on 20.01.2026.
//
