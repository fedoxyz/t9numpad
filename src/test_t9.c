/*
 * tests/test_t9.c — minimal unit tests for the T9 prediction engine
 *
 * Build:  make -C ..  &&  cc -I../include -o test_t9 test_t9.c ../build/t9.o ../build/log.o
 * Run:    ./test_t9
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "t9.h"
#include "config.h"

static void test_sequence_builds(void)
{
    t9numpad_config_t cfg;
    config_init_defaults(&cfg);
    cfg.dict_path[0] = '\0'; /* no dict — just test key routing */

    t9_ctx_t *ctx = t9_create(&cfg);
    assert(ctx != NULL);

    t9_result_t r;
    /* KP4 → digit 4 → letters ghi */
    int rc = t9_handle_key(ctx, KEY_KP4, 1, &r);
    assert(rc == 0);

    /* KP0 → backspace the sequence */
    rc = t9_handle_key(ctx, KEY_KP0, 1, &r);
    assert(rc == 0);
    assert(r.action == T9_NOOP); /* had sequence, so just shortened it */

    /* KP0 again — sequence now empty → real backspace */
    rc = t9_handle_key(ctx, KEY_KP0, 1, &r);
    assert(rc == 0);
    assert(r.action == T9_BACKSPACE);

    t9_destroy(ctx);
    printf("PASS: test_sequence_builds\n");
}

int main(void)
{
    test_sequence_builds();
    printf("All tests passed.\n");
    return 0;
}
