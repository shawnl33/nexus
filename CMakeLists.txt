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
    src/core/market/candle.c
    src/core/market/ring.c
    src/core/model/instrument.c
    src/core/model/time_us.c
    src/core/model/units.c
)
target_include_directories(core PUBLIC src)
target_compile_options(core PRIVATE ${TR_WARNINGS})

add_executable(trading-engine
    src/app/main.c
)
target_compile_definitions(trading-engine
    PRIVATE TRADING_ENGINE_VERSION="${PROJECT_VERSION}"
)
target_compile_options(trading-engine PRIVATE ${TR_WARNINGS})

enable_testing()

# 0개 테스트 실행을 통과로 간주하지 않는다(계획서 §25).
foreach(t IN ITEMS ring units model)
    add_executable(test_${t} tests/test_${t}.c)
    target_link_libraries(test_${t} PRIVATE core)
    target_compile_options(test_${t} PRIVATE ${TR_WARNINGS})
    add_test(NAME ${t} COMMAND test_${t})
endforeach()
