#include "fault_alloc.h"

/* Keep allocations inside the CSS parser observable, including private
 * temporary structures that never reach the public image object. */
#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/cupidimage_svg_base.c"
#include "../src/cupidimage_svg.c"
#undef malloc
#undef calloc
#undef realloc
#undef free

int main(void) {
    static const char *fixtures[] = {
        "g.layer#root[data-mode='bright'][enabled] > rect.a.b + path[data-x=1] ~ circle,"
        "svg > g.item { fill: red; stroke: blue; opacity: 0.5; }",
        "@keyframes move { from { transform: translate(0,0); } 50%,to { transform: translate(4,0); }}"
        "#target { animation: move 2s infinite; }",
        "@keyframes dash { from { stroke-dashoffset: 0; } to { stroke-dashoffset: 8; }}"
        "@keyframes shade { from { stop-color: red; } to { stop-color: blue; }}",
        "g[x='a'][y='b'] > rect {} .empty { unknown: value; }"
    };
    size_t checks = 0, failures = 0;
    for (size_t fixture = 0; fixture < sizeof(fixtures) / sizeof(fixtures[0]); fixture++) {
        size_t total = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            allocation_count = 0;
            fail_at = failure;
            svg_css css;
            svg_css_init(&css);
            svg_css_parse(&css, fixtures[fixture]);
            svg_css_free(&css);
            if (failure == 0) total = allocation_count;
            checks++;
            if (live_count) {
                failures++;
                fprintf(stderr, "FAIL fixture %zu allocation %zu: %zu live allocations\n",
                        fixture, failure, live_count);
            }
            while (live_count) fault_free(live[0]);
        }
    }
    printf("SVG CSS allocation failures: %zu checks, %zu failures\n", checks, failures);
    return failures ? 1 : 0;
}
