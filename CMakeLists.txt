cmake_minimum_required(VERSION 3.24)

project(trading_engine VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# GCC/Clang과 MSVC 플래그를 혼용하지 않는다(계획서 §25).
if(MSVC)
    set(TR_WARNINGS /W4 /utf-8)
else()
    set(TR_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# core는 외부 라이브러리에 의존하지 않아야 하므로(계획서 §4),
# 외부 의존성은 adapters 계층의 타겟에서만 링크한다.
add_library(core STATIC
    src/core/market/bar_aggregator.c
    src/core/market/bar_builder.c
    src/core/market/candle.c
    src/core/market/ring.c
    src/core/market/session.c
    src/core/indicators/atr.c
    src/core/indicators/auto_session_adx_v1.c
    src/core/indicators/daily_align_v2.c
    src/core/indicators/daily_linreg_trend_v1.c
    src/core/indicators/daily_market_profile_v1.c
    src/core/indicators/daily_trend_link_v1.c
    src/core/indicators/gap_regime_v1.c
    src/core/indicators/htf_curve_predict.c
    src/core/indicators/linreg.c
    src/core/indicators/linreg_predict_v4.c
    src/core/indicators/linreg_v3.c
    src/core/indicators/market_profile.c
    src/core/indicators/memory_lines.c
    src/core/indicators/orderbook_dir_v2.c
    src/core/indicators/past_prediction.c
    src/core/indicators/score_1m.c
    src/core/model/civil_time.c
    src/core/model/instrument.c
    src/core/model/time_us.c
    src/core/model/units.c
)
target_include_directories(core PUBLIC src)
target_compile_options(core PRIVATE ${TR_WARNINGS})
# 지표 계산은 double을 사용한다(계획서 §6). MSVC는 libm이 없다.
if(NOT MSVC)
    target_link_libraries(core PUBLIC m)
endif()

# adapters 계층: core 인터페이스와 외부 세계를 연결한다.
add_library(history STATIC
    src/adapters/history/replay.c
)
target_link_libraries(history PUBLIC core)
target_include_directories(history PUBLIC src)
target_compile_options(history PRIVATE ${TR_WARNINGS})

add_executable(trading-engine
    src/app/main.c
)
target_compile_definitions(trading-engine
    PRIVATE TRADING_ENGINE_VERSION="${PROJECT_VERSION}"
)
target_compile_options(trading-engine PRIVATE ${TR_WARNINGS})

enable_testing()

# 0개 테스트 실행을 통과로 간주하지 않는다(계획서 §25).
foreach(t IN ITEMS ring units model civil_time session bar_builder aggregator linreg htf_curve lp4 obd2 lr3 past_prediction memory_lines market score1m dlrt1 dalign2 gap1 dtl1 dmp1 adx1)
    add_executable(test_${t} tests/test_${t}.c)
    target_link_libraries(test_${t} PRIVATE core)
    target_compile_options(test_${t} PRIVATE ${TR_WARNINGS})
    add_test(NAME ${t} COMMAND test_${t})
endforeach()

add_executable(test_replay tests/test_replay.c)
target_link_libraries(test_replay PRIVATE history)
target_compile_options(test_replay PRIVATE ${TR_WARNINGS})
add_test(NAME replay COMMAND test_replay)
