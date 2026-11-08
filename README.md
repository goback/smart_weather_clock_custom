# SmallTV 장치 커스터마이징

GeekMagic SmallTV-Ultra로 판매된 SD PRO 계열 장치를 원하는 정보를 표시하는 240×240 화면으로 바꾸는 프로젝트입니다. 대화와 문서에서는 기기를 **장치**라고 부릅니다.

## 현재 상태

2026-11-08의 기존 작업 기록 기준으로, SmartClock-OSS v1.3.2 기반 최소 펌웨어 `SDP-Minimal-Clean64.bin`을 장치에 적용했고 불필요한 파일 64개 삭제를 확인했습니다. Wi-Fi·밝기 설정과 장치 로그인 비밀번호는 유지했습니다.

현재 남긴 기능은 `BOOT OK`·경과 시간 표시, Wi-Fi 설정, 밝기 즉시 적용·자동 저장, 관리 페이지 로그인·비밀번호 변경, 파일 목록 조회, 웹 펌웨어 업데이트와 복구 모드입니다.

사용자가 제공한 기존 `.bin`을 배포 기록의 SHA-256과 대조한 뒤 [버전별 펌웨어](firmware/README.md)로 보관합니다. 현재 버전은 `1.3.2-minimal-clean64`이며 Git 태그는 `v1.3.2-minimal-clean64`입니다. **커스텀 펌웨어 소스는 아직 가져오지 않았으므로, 현재 저장소만으로 빌드할 수 없습니다.**

## 이어서 읽기

- [확인된 장치 상태와 설정 보존 조건](docs/device-state.md)
- [펌웨어 다운로드와 버전 관리](firmware/README.md)
- [기존 소스·바이너리 위치와 개발 재개 순서](docs/handoff.md)
- [삭제를 검토했던 64개 파일의 경로·크기](docs/evidence/legacy-assets.json)

다음 기능으로 제목·큰 숫자·한 줄 메시지를 웹에서 편집하는 화면을 제안한 상태입니다. 어떤 기능을 구현할지는 아직 결정하지 않았습니다.

## 기반 프로젝트

- [SmartClock-OSS](https://github.com/michaeldale/smartclock-oss): 기존 커스텀 펌웨어의 기반. 사용 버전 v1.3.2.
- [SD PRO 제조사 공개 자료](https://github.com/JUZIPi-tech/SD_PRO)

소스를 가져올 때 기반 프로젝트의 `LICENSE`와 저작권 고지를 함께 보존합니다.
