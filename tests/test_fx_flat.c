#include "core/functions/fx_flat_count_v2.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "fail %s\n", msg);
        fails++;
    }
}

int main(void) {
    tr_fxflat_out_t prev, out;
    memset(&prev, 0, sizeof(prev));
    tr_fxflat_step(0, 0, 10, 0, 11, 9, 11, 0, 0.25, 3, &out);
    expect(out.valid == 1, "first valid");
    expect(out.pos == 0, "first has no pair");
    prev = out;
    tr_fxflat_step(&prev, 0, 11, 10, 12, 10, 12, 11, 0.25, 3, &out);
    expect(out.pos == 1, "two closes above");
    prev = out;
    tr_fxflat_step(&prev, 0, 12, 11, 13, 11, 13, 12, 0.25, 3, &out);
    expect(out.pos == 2, "third close above");
    expect(out.slope == 2, "two up steps");
    if (fails) {
        return 1;
    }
    printf("ok\n");
    return 0;
}
