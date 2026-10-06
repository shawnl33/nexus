# Windows 빌드

명령 프롬프트(`cmd`)에서 저장소를 받아 `trading-engine.exe`까지 만드는 순서다. Visual Studio는 쓰지 않는다. 컴파일러는 MinGW-w64 GCC다.

## 1. Git, CMake, Ninja, 컴파일러, Node.js 설치

명령 프롬프트를 열고 한 줄씩 실행한다.

```bat
winget install -e --id Git.Git
winget install -e --id Kitware.CMake
winget install -e --id Ninja-build.Ninja
winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT
winget install -e --id OpenJS.NodeJS.LTS
```

Node.js는 빌드 자체에는 필요 없다. `traderctl up`이 엔진을 띄운 뒤 대시보드를 `node`로 실행하므로, 20 이상이 있어야 한다. 없으면 `error: node 를 찾지 못했습니다`로 끝난다.

설치가 끝나면 창을 닫고 새로 연다. 아래 명령이 모두 버전을 출력해야 한다.

```bat
git --version
cmake --version
ninja --version
gcc --version
g++ --version
node --version
```

`g++`도 필요하다. 정적 libzmq가 C++ 라이브러리다.

## 2. 저장소 받기

로컬 디스크에 둔다. `\\wsl.localhost\...` 같은 네트워크 경로에서는 MinGW가 오브젝트 파일을 만들지 못한다.

```bat
mkdir D:\dev
cd /d D:\dev
git clone https://github.com/shawnl33/nexus.git
cd nexus
```

`git clone`은 그 주소의 파일을 `D:\dev\nexus` 폴더로 복사하는 명령이다. 처음 빌드만 할 때는 브랜치나 커밋 명령은 필요 없다.

## 3. 빌드

SQLite, libzmq, libcurl, libwebsockets, mbedTLS는 `third_party\mingw-prefix`에 들어 있다. 따로 받지 않는다.

`D:\dev\nexus`에서 실행한다.

```bat
cmake --preset windows
cmake --build --preset windows
```

구성 로그에 다음 줄이 있으면 저장소 안의 라이브러리를 찾은 것이다.

```text
MinGW 의존성 접두: D:/dev/nexus/third_party/mingw-prefix
```

결과물은 다음 경로에 있다.

```text
D:\dev\nexus\build-windows\trading-engine.exe
D:\dev\nexus\build-windows\traderctl.exe
```

실행에 필요한 `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`은 빌드가 같은 폴더에 복사한다.

엔진과 대시보드를 함께 띄운다.

```bat
.\build-windows\traderctl.exe up --live 005930
```

처음 만든 exe는 Windows가 실행을 막을 수 있다. 확인 창이 뜨면 허용을 누른 뒤 같은 명령을 다시 실행한다.

테스트는 이렇게 실행한다.

```bat
ctest --preset windows
```

## 4. 다시 구성할 때

소스를 다른 경로로 옮겼거나 예전에 `\\wsl.localhost\...`에서 구성을 했다면, 그 캐시는 재사용하지 않는다.

```bat
cd /d D:\dev\nexus
rmdir /s /q build-windows
cmake --preset windows
cmake --build --preset windows
```
