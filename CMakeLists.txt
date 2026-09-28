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

# 저장소 어댑터: SQLite는 시스템 패키지를 사용한다.
# (의존성 관리 결정: vcpkg는 도입하지 않는다. Linux는 시스템 패키지,
#  Windows 독립 MinGW는 소형 라이브러리 벤더 + 중형은 소스 빌드 — PROJECT_STATE 기록.)
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    add_library(storage STATIC
        src/adapters/storage/store.c
    )
    target_link_libraries(storage PUBLIC core SQLite::SQLite3)
    target_include_directories(storage PUBLIC src)
    target_compile_options(storage PRIVATE ${TR_WARNINGS})
else()
    message(STATUS "SQLite3 not found: storage 어댑터와 store 테스트는 건성한다 (미검증 기록)")
endif()

# yyjson 벤더 (MIT, third_party/yyjson, 버전은 docs/THIRD_PARTY.md).
add_library(yyjson STATIC third_party/yyjson/yyjson.c)
target_include_directories(yyjson PUBLIC third_party/yyjson)
target_compile_options(yyjson PRIVATE -w) # 서드파티 코드에는 경고 플래그를 적용하지 않는다

# IPC 어댑터: ZeroMQ는 시스템 패키지를 사용한다.
find_library(ZMQ_LIBRARY NAMES zmq libzmq)
find_path(ZMQ_INCLUDE_DIR NAMES zmq.h)
if(ZMQ_LIBRARY AND ZMQ_INCLUDE_DIR)
    add_library(ipc STATIC
        src/adapters/ipc/ipc.c
        src/adapters/ipc/ipc_client.c
        src/adapters/ipc/ipc_msg.c
    )
    target_link_libraries(ipc PUBLIC core yyjson ${ZMQ_LIBRARY})
    target_include_directories(ipc PUBLIC src ${ZMQ_INCLUDE_DIR})
    target_compile_options(ipc PRIVATE ${TR_WARNINGS})
    set(TR_IPC_FOUND TRUE)

    add_executable(traderctl src/cli/traderctl.c)
    target_link_libraries(traderctl PRIVATE ipc)
    target_compile_options(traderctl PRIVATE ${TR_WARNINGS})
else()
    message(STATUS "libzmq not found: ipc 어댑터와 ipc 테스트는 건성한다 (미검증 기록)")
endif()

# 엔진 런타임 (core + adapters 조립).
if(TR_IPC_FOUND)
    add_library(runtime STATIC
        src/runtime/engine.c
        src/adapters/history/csv_ticks.c
    )
    target_link_libraries(runtime PUBLIC core ipc history)
    target_include_directories(runtime PUBLIC src)
    target_compile_options(runtime PRIVATE ${TR_WARNINGS})
endif()

if(TR_IPC_FOUND)
    add_executable(trading-engine
        src/app/main.c
    )
    target_compile_definitions(trading-engine
        PRIVATE TRADING_ENGINE_VERSION="${PROJECT_VERSION}"
    )
    target_compile_options(trading-engine PRIVATE ${TR_WARNINGS})
    target_link_libraries(trading-engine PRIVATE runtime)
else()
    message(STATUS "libzmq not found: trading-engine은 건성한다 (미검증 기록)")
endif()

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

# 엔진 런타임 테스트.
if(TR_IPC_FOUND)
    add_executable(test_engine tests/test_engine.c)
    target_link_libraries(test_engine PRIVATE runtime)
    target_compile_options(test_engine PRIVATE ${TR_WARNINGS})
    add_test(NAME engine COMMAND test_engine)
endif()

if(SQLite3_FOUND)
    add_executable(test_store tests/test_store.c)
    target_link_libraries(test_store PRIVATE storage)
    target_compile_options(test_store PRIVATE ${TR_WARNINGS})
    add_test(NAME store COMMAND test_store)
endif()

if(TR_IPC_FOUND)
    add_executable(test_ipc tests/test_ipc.c)
    target_link_libraries(test_ipc PRIVATE ipc)
    target_compile_options(test_ipc PRIVATE ${TR_WARNINGS})
    add_test(NAME ipc COMMAND test_ipc)

    add_executable(test_cli_client tests/test_cli_client.c)
    target_link_libraries(test_cli_client PRIVATE ipc)
    target_compile_options(test_cli_client PRIVATE ${TR_WARNINGS})
    add_test(NAME cli_client COMMAND test_cli_client)

    add_test(NAME cli_help COMMAND traderctl --help)
    add_test(NAME cli_version COMMAND traderctl --version)
    add_test(NAME cli_usage_error COMMAND traderctl frobnicate)
    set_tests_properties(cli_usage_error PROPERTIES WILL_FAIL TRUE)
endif()
