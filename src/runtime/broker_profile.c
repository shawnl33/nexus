#include "runtime/broker_profile.h"

#include <string.h>

/* LS REST는 실전·모의 예제가 모두 :8080 이다. 실시간만 포트가 갈린다.
 * 공식 howto-sample: 9443 이 실전, 29443 이 모의투자.
 * 어느 서버인지는 앱키가 정한다. 실전 키로 모의 포트에 붙지 않는다.
 * NH 주소는 docs/nh-futures-rest-api.md. 시세는 chart-minute 과 체인 조회만 쓴다. */
static const tr_broker_profile_t PROFILES[] = {
    {"ls-paper", "LS 모의", "ls", "paper", "https://openapi.ls-sec.co.kr:8080",
     "wss://openapi.ls-sec.co.kr:29443/websocket", "LS_PAPER_APP_KEY", "LS_PAPER_SECRET_KEY",
     true},
    {"ls-live", "LS 실전", "ls", "live", "https://openapi.ls-sec.co.kr:8080",
     "wss://openapi.ls-sec.co.kr:9443/websocket", "LS_APP_KEY", "LS_SECRET_KEY", true},
    {"nh-paper", "NH 모의", "nh", "paper", "https://apidemo.futures.co.kr",
     "wss://apidemo.futures.co.kr/trade/ws-stream", "NH_PAPER_APP_KEY", "NH_PAPER_SECRET_KEY",
     true},
    {"nh-live", "NH 실전", "nh", "live", "https://api.futures.co.kr",
     "wss://api.futures.co.kr/trade/ws-stream", "NH_APP_KEY", "NH_SECRET_KEY", true},
};

size_t tr_broker_profile_count(void) {
    return sizeof(PROFILES) / sizeof(PROFILES[0]);
}

const tr_broker_profile_t *tr_broker_profile_at(size_t index) {
    if (index >= tr_broker_profile_count()) {
        return 0;
    }
    return &PROFILES[index];
}

const tr_broker_profile_t *tr_broker_profile_find(const char *id) {
    if (id == 0 || id[0] == '\0') {
        return 0;
    }
    for (size_t i = 0; i < tr_broker_profile_count(); i++) {
        if (strcmp(PROFILES[i].id, id) == 0) {
            return &PROFILES[i];
        }
    }
    return 0;
}

bool tr_broker_profile_usable(const tr_broker_profile_t *profile,
                              bool (*key_present)(const char *env_name, void *ctx), void *ctx) {
    if (profile == 0 || !profile->market_data || key_present == 0) {
        return false;
    }
    return key_present(profile->app_key_env, ctx) && key_present(profile->secret_env, ctx);
}

const tr_broker_profile_t *tr_broker_profile_choose(
    const char *saved_id, bool (*key_present)(const char *env_name, void *ctx), void *ctx) {
    const tr_broker_profile_t *saved = tr_broker_profile_find(saved_id);
    if (tr_broker_profile_usable(saved, key_present, ctx)) {
        return saved;
    }
    const tr_broker_profile_t *nh_live = tr_broker_profile_find("nh-live");
    if (tr_broker_profile_usable(nh_live, key_present, ctx)) {
        return nh_live;
    }
    const tr_broker_profile_t *paper = 0;
    const tr_broker_profile_t *any = 0;
    for (size_t i = 0; i < tr_broker_profile_count(); i++) {
        if (!tr_broker_profile_usable(&PROFILES[i], key_present, ctx)) {
            continue;
        }
        if (any == 0) {
            any = &PROFILES[i];
        }
        if (paper == 0 && strcmp(PROFILES[i].env, "paper") == 0) {
            paper = &PROFILES[i];
        }
    }
    return paper != 0 ? paper : any;
}
