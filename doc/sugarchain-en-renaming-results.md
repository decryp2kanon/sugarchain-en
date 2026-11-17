# Sugarchain En 이름 변경 결과

검증일: 2026년 11월 18일

제품 이름을 **Sugarchain En**, GitHub 저장소와 로컬 주 폴더 이름을 **Sugarchain-En**으로 변경했다. 코드·패키징·문서뿐 아니라 기존 Qt 설정의 최초 이관, 결제 IPC 호환, 로컬 빌드·IBD 도구 경로를 함께 처리했다.

## 최종 위치와 실행 방법

| 항목 | 최종 값 |
|---|---|
| GitHub | https://github.com/decryp2kanon/Sugarchain-En |
| 저장소 ID | `1390440223`, 이름 변경 전과 동일 |
| 기본 브랜치 | `core31` |
| 로컬 주 폴더 | `/home/ak/git/Sugarchain-En` |
| origin | `https://github.com/decryp2kanon/Sugarchain-En.git` |
| GUI | `/home/ak/git/Sugarchain-En/build/bin/sugarchain-qt` |
| daemon | `/home/ak/git/Sugarchain-En/build/bin/sugarchaind` |
| CLI | `/home/ak/git/Sugarchain-En/build/bin/sugarchain-cli` |
| 빌드 명령 | `sugarchain-build` 또는 `sugarchain-build en` |
| 이전 빌드 보관 | `/home/ak/git/Sugarchain-En/build-before-en-20261118` |

현재 `build`는 검증된 실행파일의 설치 위치다. 실제 clean configure·컴파일과 시험은 등록된 NVMe 임시 작업 공간에서 수행했다. 다음 빌드도 관리 래퍼를 사용하면 소스 폴더와 임시 결과물을 분리할 수 있다. `sugarchain-build core31`은 호환 별칭으로 새 En 빌드를 선택하고, 별도 `legacy` 분기는 구형 저장소를 유지한다.

SSH는 이 PC의 호스트 키 확인에서 실패하여 저장소 한정으로 GitHub CLI의 기존 인증을 사용하는 HTTPS Git 경로를 설정했다. SSH 호스트 키 검증을 비활성화하지 않았고 전역 Git 인증 설정도 변경하지 않았다.

## 구현 범위

- CMake 제품명·프로젝트명, GUI 이름, Linux 자동실행 표시, 서비스 설명, 현행 문서·이슈 안내와 7개 manpage를 변경했다.
- Qt는 구 프로필을 보존한 채 최초 실행에서 새 이름으로 이관한다. 기존 새 프로필은 덮어쓰지 않으며 실패 중 저널을 정상 새 프로필로 간주하지 않는다. 기본 프로필과 네트워크별 프로필을 나눠 처리한다.
- 새 결제 IPC를 우선하고 구버전 GUI로의 전달을 지원한다. 연결 또는 부분 전송 이후 결과가 불확실하면 재전송하지 않는다. 네트워크 datadir의 별도 GUI 시작 잠금으로 시작 경합을 막으며 노드의 기존 데이터 잠금은 유지한다.
- Windows 신규 설치명·등록·제거 정보·바로가기를 변경했다. 기존 설치는 기존 설치 폴더에서 안전하게 업그레이드하도록 처리했다. 구 설치 경로를 보존하는 것은 호환성 예외다.
- macOS 앱은 `Sugarchain En.app`, bundle·알림 ID는 `org.sugarchain.SugarchainEn`으로 변경했다. 내부 실행파일 `Sugarchain-Qt`는 유지하고 공백 경로를 배포·ZIP·서명 도구에서 함께 처리했다.
- 새 배포 이름과 Guix·서명 도구의 참조를 맞췄다. 실제 생성 경로를 CMake 출력 manifest로 전달해 서로 다른 버전 변수 추측을 피했다.
- 7개 OS 빌드 안내의 낡은 clone 주소와 브랜치를 수정했다. 두 설치 안내에 남은 잘못된 Bitcoin mainnet 설명은 별도 커밋으로 현행 Sugarchain mainnet 설명에 맞췄다.
- 외부 빌드 래퍼와 IBD 도구의 기본 CLI 경로·제품명 fixture를 변경했다. IBD 저장소의 기존 미완료 수정은 보존하고 이번 3줄만 별도 커밋으로 분리했다.

금액 계산·지갑 형식·기존 datadir·config·주소·코인 단위·genesis·포트·합의·PoW·기존 IBD 로직은 변경하지 않았다. 해당 프로토콜·지갑 소스는 변경 전 기준 `bea59560651651f6a29fe637ca3dcbaa6dc75d52`와 동일함을 확인했다. P2P 사용자 에이전트 `Satoshi`도 유지했다.

## 실제 검증 결과

| 검증 | 결과와 범위 |
|---|---|
| Linux 전체 통합 빌드 | GUI·IPC·시험을 포함한 빌드 성공. 새 IPC 시험의 Qt Network 연결 누락을 통합 단계에서 수정 |
| 새 소스 경로 clean configure·빌드 | 새 CMake 빌드 디렉터리에서 성공. 기존 절대 경로 캐시를 수정해서 재사용하지 않음 |
| 설치된 주 실행파일 7개 | 모두 실행 가능하고 버전 첫 줄에 Sugarchain En 표시 |
| 설치된 wrapper | `sugarchain gui --version`, `sugarchain node --version` 정상 |
| 설치된 GUI 전체 시작·종료 | 분리된 regtest·XDG 환경에서 RPC 준비, GUI 시작 잠금 유지, 정상 종료 코드 0·잠금 해제 확인 |
| 공개 새 주소 fresh clone | 게시된 HEAD를 새 URL에서 받아 GUI·IPC 포함 CMake 구성 성공, 프로젝트명 SugarchainEn 확인 |
| 설정 이관 회귀검사 | QtTest 집계 9개 통과. 기존·빈 신규 프로필 보존, 5개 네트워크, 재시도·손상 저널 처리 |
| 결제 IPC 회귀검사 | QtTest 집계 8개 통과. 새 서버 우선·구 서버 전달·경합·부분 전달 후 재전송 방지 |
| 실제 Linux Native QSettings | 분리된 XDG 환경에서 구 프로필 최초 복사·구 프로필 보존·새 프로필 덮어쓰기 방지 확인 |
| 핵심 CTest 대상 8개 | amount·args·Base58·Bech32·serialize·settings·URL·multiprocess 시험 통과 |
| regtest 지갑 | 분리된 시험 데이터로 102블록 생성, 송금·수신, 노드 재시작과 지갑 재열기 통과 |
| 설치 후 재열기 | 설치된 daemon·CLI로 같은 시험 지갑의 높이 102와 수신 잔액을 확인 |
| Windows 설치파일 생성 | 독립된 두 검사에서 NSIS 3.10 `-WX`, 종료 0·경고 0. 실제 CMake deploy도 합성 payload로 성공 |
| macOS 패키징 | native/cross CMake 분기 staging, plist·실행파일 일치, 공백 경로 복사·ZIP 생성 확인 |
| 기존 worktree | 기록 보존 및 실제 존재하는 worktree의 HEAD·공통 Git 경로 확인. 기존 경로가 없는 기록은 정리하지 않음 |
| 사용자 파일 | 기존 비추적 PNG의 SHA256 보존 확인 |
| 외부 빌드 래퍼 | 기본·en·En·Sugarchain-En·core31·legacy 6개 선택 시험과 셸 문법 통과 |

시험은 운영 지갑과 분리했다. 실제 개인 지갑 DB를 시험에 열지 않았다. 로컬 Qt 프로필 4개의 datadir 경로 키만 별도 확인한 결과, 이전 소스 폴더 안을 가리키는 선택 경로는 없었다. 운영 프로필 자체를 미리 수정하지 않고 첫 정상 실행에서 이관하도록 유지했다.

### 전체 시험에서 남아 있는 기존 실패

이름 변경 전 바이너리와 비교하여 다음 결과를 구분했다. 전체 시험이 전부 통과했다고 주장하지 않는다.

- 전체 Qt CTest는 기존과 동일한 실패 3개가 있다. `URITests`는 과거 Bitcoin URI fixture, `RPCNestedTests`는 Bitcoin genesis 기대값, `WalletTests`는 기존 기대값 불일치다. 새 설정·IPC 검사 17개는 통합 Qt 실행에서도 모두 통과했다.
- 기존 `amount_tests`의 두 수수료 표시 assertion이 BTC를 기대하던 문제는 별도 시험 커밋으로 SUGAR 출력에 맞췄다. 금액 계산 코드는 바꾸지 않았다.
- IBD 도구의 24개 시험 중 23개가 통과했다. 변경 전·후 모두 `2000/s`와 `2000.0/s` 표시 차이로 같은 1개가 실패한다. 이번 제품명 fixture의 관련 시험은 별도로 확인했다. 기존 진행 중 수정은 덮어쓰지 않았다.

### 플랫폼 검증 한계

Windows 실제 설치·업그레이드·제거, Windows 레지스트리 기반 설정 이관, macOS 실제 앱 실행·CFPreferences 이관·서명·공증은 해당 운영체제에서 추가 확인해야 한다. 합성 payload와 대체 Mach-O 분석 도구를 사용한 패키징 시험을 실제 플랫폼 실행 시험으로 취급하지 않는다.

구버전 결제 IPC에는 응용 계층 ACK가 없다. 구 서버 호환은 전체 데이터 전송 완료까지 확인하며 사용자가 화면에서 처리했다는 확인까지 보장하지 않는다. 불확실한 전송 후 다른 서버로 재제출하지 않는다.

저작권·역사적 증거 링크·구 설정과 설치를 찾는 호환 코드에는 옛 이름이 남는다. 이유는 [브랜딩 잔존 감사 기준](sugarchain-en-branding-audit.md)에 기록했다.

## 복구 자료와 되돌리는 순서

로컬 도구 원본·이전 Git 설정·worktree 기록·설치 위치 기록은 다음 보호된 상태 폴더에 보관한다.

`/home/ak/.local/state/nana-config-backups/Sugarchain-En-rename-20261117-234440`

1. 관련 GUI·노드·빌드가 실행 중이면 정상 종료 시점을 먼저 정한다. 강제 종료하지 않는다.
2. 코드 변경은 기능별 revert로 되돌린다. 공유 이력을 reset하거나 force push하지 않는다.
3. 실행파일 복원이 필요하면 새 설치 폴더를 별도로 보관하고 `build-before-en-20261118`을 원래 `build` 위치로 복원한다. 지갑 DB를 덮어쓰지 않는다.
4. 로컬 주 폴더를 이전 이름으로 되돌릴 경우 `git worktree repair`를 다시 실행하고 외부 래퍼·IBD 경로를 보관된 원본과 함께 복구한다. 다른 worktree를 삭제하거나 prune하지 않는다.
5. GitHub 저장소까지 되돌릴 경우 같은 저장소를 rename하고 origin을 함께 조정한다. 이름 충돌과 외부 참조를 먼저 확인한다.
6. 구 Qt 프로필은 남아 있으므로 구버전은 원래 설정을 사용할 수 있다. 새 프로필을 구 프로필에 역복사하지 않는다.

옛 저장소 이름을 재사용해 새 저장소를 만들면 GitHub redirect가 깨질 수 있으므로 사용하지 않는다. 과거 태그·릴리스·저작권 고지는 새 이름으로 소급 수정하지 않는다.
