# SmallTV 장치 커스터마이징

GeekMagic SmallTV-Ultra로 판매된 SD PRO 계열 장치를 원하는 정보를 표시하는 240×240 화면으로 바꾸는 프로젝트입니다. 대화와 문서에서는 기기를 **장치**라고 부릅니다.

## 현재 상태

2026-11-08의 기존 작업 기록 기준으로, SmartClock-OSS v1.3.2 기반 최소 펌웨어 `SDP-Minimal-Clean64.bin`을 장치에 적용했고 불필요한 파일 64개 삭제를 확인했습니다. Wi-Fi·밝기 설정과 장치 로그인 비밀번호는 유지했습니다.

현재 남긴 기능은 `BOOT OK`·경과 시간 표시, Wi-Fi 설정, 밝기 즉시 적용·자동 저장, 관리 페이지 로그인·비밀번호 변경, 파일 목록 조회, 웹 펌웨어 업데이트와 복구 모드입니다.

사용자가 제공한 기존 `.bin`과 Windows PC에서 찾은 [커스텀 펌웨어 소스](firmware/1.3.2-minimal-clean64/source/)를 보관합니다. 원래 빌드 폴더에 남아 있던 `firmware.bin`의 SHA-256은 저장소의 바이너리와 일치합니다. 현재 실행 환경에서는 ESP8266 컴파일러 오류로 새 빌드까지 검증하지는 못했습니다. 자세한 내용과 빌드 명령은 [소스 확보 기록](docs/source-import.md)을 참고하세요. 기존 Git 태그 `v1.3.2-minimal-clean64`는 소스 추가 전 바이너리 보관 시점을 가리킵니다.

## 이어서 읽기

- [확인된 장치 상태와 설정 보존 조건](docs/device-state.md)
- [소스 확보 기록과 빌드 방법](docs/source-import.md)
- [펌웨어 다운로드와 버전 관리](firmware/README.md) — 소스 확보 전 기록
- [기존 소스·바이너리 위치와 개발 재개 순서](docs/handoff.md) — 소스 확보 전 기록
- [삭제를 검토했던 64개 파일의 경로·크기](docs/evidence/legacy-assets.json)

다음 기능으로 제목·큰 숫자·한 줄 메시지를 웹에서 편집하는 화면을 제안한 상태입니다. 어떤 기능을 구현할지는 아직 결정하지 않았습니다.

## 기반 프로젝트

- [SmartClock-OSS](https://github.com/michaeldale/smartclock-oss): 기존 커스텀 펌웨어의 기반. 사용 버전 v1.3.2.
- [SD PRO 제조사 공개 자료](https://github.com/JUZIPi-tech/SD_PRO)

가져온 소스에 포함된 기반 프로젝트의 [LICENSE](firmware/1.3.2-minimal-clean64/source/LICENSE)를 그대로 보존합니다.
