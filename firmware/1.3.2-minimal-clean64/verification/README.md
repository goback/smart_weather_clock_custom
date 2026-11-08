# PC 검증 자료

`clean64-check/verify.cpp`와 `LittleFS.h`는 기존 Windows 작업 폴더에서 가져온 검증 코드입니다. `legacy_assets.h`를 실제 소스에서 가져와 파일 정리 동작을 시험합니다. 비밀번호 변경 검증에는 당시 시험용으로 복사한 함수와 모의 객체를 사용하므로 현재 펌웨어 전체의 실기 시험을 대신하지 않습니다.

`run_ota_test.py`는 이번에 추가한 실행 스크립트입니다. 원본 `source/tests/ota_harness.cpp`에 `source/src/main.cpp`의 현재 OTA·복구 카운터 구현을 끼워 넣어 C++17 컴파일러로 시험합니다. Python 3와 `cl`, `g++`, `clang++` 중 하나가 필요합니다. Windows에서 `cl`을 쓰려면 Visual Studio 개발자 명령 프롬프트를 사용합니다.

```sh
python verification/run_ota_test.py
```

위 명령은 이 버전 폴더의 `SDP-Minimal-Clean64.bin`을 검사합니다. 다른 바이너리를 지정하려면 경로를 인수로 넘깁니다. 이 PC 검사는 장치에 실제로 설치하거나 하드웨어 동작을 검증하지 않습니다.
