# 펌웨어 소스 확보 기록

2026-11-08에 기존 PC에서 찾은 `1.3.2-minimal-clean64` 프로젝트를 [소스 폴더](../firmware/1.3.2-minimal-clean64/source/)에 보관했습니다. `src/`, `include/`, `web/`, `scripts/`, `tests/`, `platformio.ini`, `LICENSE`는 원본과 바이트 단위로 일치합니다. `.pio` 빌드 캐시와 개인 설정은 포함하지 않았습니다. 새 `.gitignore`는 캐시와 `secrets.ini`의 커밋을 방지하고, `.gitattributes`는 원본의 줄바꿈을 보존합니다.

기존 프로젝트의 빌드 폴더에 남아 있던 `firmware.bin`은 368,816바이트이며, SHA-256 `bb9006cbd35b5251c191eab4e8f1a4f09ce9d2566eb4b96446606ccf7acce22c`가 저장소의 [바이너리](../firmware/1.3.2-minimal-clean64/SDP-Minimal-Clean64.bin)와 일치합니다. 발견된 바이너리의 일치 확인이며, 소스에서 동일한 바이너리를 재현했다는 뜻은 아닙니다.

## 빌드

PlatformIO Core를 설치한 뒤 저장소 루트에서 다음 명령을 실행합니다.

```sh
cd firmware/1.3.2-minimal-clean64/source
SMARTCLOCK_RELEASE=1 pio run -e esp12e
```

PowerShell에서는 해당 소스 폴더에서 `$env:SMARTCLOCK_RELEASE = '1'`을 설정한 다음 `pio run -e esp12e`를 실행합니다. 릴리스 설정은 개인 `secrets.ini`의 Wi-Fi 기본값을 빌드에 포함하지 않습니다. 빌드 시각·Git 정보가 포함되므로 새 빌드의 해시는 기존 바이너리와 달라질 수 있습니다.

이번 작업에서 의존성을 갖춘 상태로 재빌드를 시도했지만, 현재 실행 환경에서 Xtensa 컴파일러가 Windows DLL 재배치 오류로 시작되지 않아 빌드 성공은 확인하지 못했습니다. [PC 검증 자료](../firmware/1.3.2-minimal-clean64/verification/)를 이용한 OTA·복구 카운터 검사와 파일 정리·비밀번호 관련 검사는 통과했습니다. 장치에 새 펌웨어를 설치하거나 실기 재시험은 하지 않았습니다.

기존 태그 `v1.3.2-minimal-clean64`와 소스 확보 전에 작성된 안내문은 당시 기록으로 유지합니다. 그 안내문에서 소스가 없다고 한 부분은 현재 저장소 상태와 다릅니다.
