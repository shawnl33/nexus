#include "test_util.h"

#include <string.h>

#include "runtime/broker_profile.h"

static bool key_is(const char *name, void *ctx, const char *a, const char *b) {
    (void)ctx;
    return strcmp(name, a) == 0 || strcmp(name, b) == 0;
}

static bool keys_live_only(const char *name, void *ctx) {
    return key_is(name, ctx, "LS_APP_KEY", "LS_SECRET_KEY");
}

static bool keys_paper_only(const char *name, void *ctx) {
    return key_is(name, ctx, "LS_PAPER_APP_KEY", "LS_PAPER_SECRET_KEY");
}

static bool keys_both_ls(const char *name, void *ctx) {
    return keys_live_only(name, ctx) || keys_paper_only(name, ctx);
}

static bool keys_nh_live(const char *name, void *ctx) {
    return key_is(name, ctx, "NH_APP_KEY", "NH_SECRET_KEY");
}

static bool keys_ls_and_nh(const char *name, void *ctx) {
    return keys_both_ls(name, ctx) || keys_nh_live(name, ctx);
}

static bool keys_none(const char *name, void *ctx) {
    (void)name;
    (void)ctx;
    return false;
}

static void test_ports(void) {
    const tr_broker_profile_t *paper = tr_broker_profile_find("ls-paper");
    const tr_broker_profile_t *live = tr_broker_profile_find("ls-live");
    TR_CHECK(paper != 0 && live != 0);
    TR_CHECK(strstr(paper->rest_base, ":8080") != 0);
    TR_CHECK(strstr(live->rest_base, ":8080") != 0);
    TR_CHECK(strstr(paper->ws_url, ":29443/") != 0);
    TR_CHECK(strstr(live->ws_url, ":9443/") != 0);
    TR_CHECK(strcmp(paper->env, "paper") == 0);
    TR_CHECK(strcmp(live->env, "live") == 0);
}

static void test_choose(void) {
    const tr_broker_profile_t *p = tr_broker_profile_choose(0, keys_both_ls, 0);
    TR_CHECK(p != 0 && strcmp(p->id, "ls-paper") == 0);

    p = tr_broker_profile_choose("ls-live", keys_both_ls, 0);
    TR_CHECK(p != 0 && strcmp(p->id, "ls-live") == 0);

    p = tr_broker_profile_choose("missing", keys_both_ls, 0);
    TR_CHECK(p != 0 && strcmp(p->id, "ls-paper") == 0);

    p = tr_broker_profile_choose(0, keys_live_only, 0);
    TR_CHECK(p != 0 && strcmp(p->id, "ls-live") == 0);

    p = tr_broker_profile_choose("ls-live", keys_paper_only, 0);
    TR_CHECK(p != 0 && strcmp(p->id, "ls-paper") == 0);

    p = tr_broker_profile_choose(0, keys_ls_and_nh, 0);
    TR_CHECK(p != 0 && strcmp(p->id, "nh-live") == 0);

    p = tr_broker_profile_choose(0, keys_nh_live, 0);
    TR_CHECK(p != 0 && strcmp(p->id, "nh-live") == 0);
    p = tr_broker_profile_choose("ls-live", keys_ls_and_nh, 0);
    TR_CHECK(p != 0 && strcmp(p->id, "ls-live") == 0);
    TR_CHECK(tr_broker_profile_choose(0, keys_none, 0) == 0);
    TR_CHECK(tr_broker_profile_find("nope") == 0);
}

int main(void) {
    test_ports();
    test_choose();
    TR_TEST_SUMMARY();
}
