cmake_minimum_required(VERSION 3.24)

project(trading_engine VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# core는 외부 라이브러리에 의존하지 않아야 하므로(계획서 §4),
# 외부 의존성은 adapters 계층의 타겟에서만 링크한다.

add_executable(trading-engine
    src/app/main.c
)

target_compile_definitions(trading-engine
    PRIVATE TRADING_ENGINE_VERSION="${PROJECT_VERSION}"
)

# GCC/Clang과 MSVC 플래그를 혼용하지 않는다(계획서 §25).
if(MSVC)
    target_compile_options(trading-engine PRIVATE /W4 /utf-8)
else()
    target_compile_options(trading-engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 테스트는 아직 없다. core 테스트가 추가되면 0개 테스트 실행을 통과로 간주하지 않는다(계획서 §25).
enable_testing()
