# Sugarchain En 이름 변경 계획

작성일: 2026년 11월 17일
상태: 2026년 11월 18일 구현 및 Linux 검증 완료. [실행 결과와 한계](sugarchain-en-renaming-results.md) 참고
대상: `decryp2kanon/sugarchain-komorebi`의 `core31` 브랜치

## 목표와 확정 명칭

**제품 이름을 Sugarchain En으로 통일하고, GitHub 저장소와 로컬 주 작업 폴더를 모두 Sugarchain-En으로 바꾼다. 기존 지갑·체인 데이터·개인 설정과 개발 작업은 보존한다.**

사용자가 지정한 대소문자와 구분자를 그대로 적용한다. `En`, `Sugarchain En`, `Sugarchain-En`을 용도에 맞게 구분하며 저장소를 임의로 소문자 이름으로 만들지 않는다.

| 용도 | 현재 | 변경 후 |
|---|---|---|
| 제품과 GUI 표시 | Sugarchain Komorebi | **Sugarchain En** |
| 제품 단축명 | Komorebi | **En** |
| GitHub 저장소 | `decryp2kanon/sugarchain-komorebi` | **`decryp2kanon/Sugarchain-En`** |
| 저장소 웹 주소 | `https://github.com/decryp2kanon/sugarchain-komorebi` | `https://github.com/decryp2kanon/Sugarchain-En` |
| SSH origin | `git@github.com:decryp2kanon/sugarchain-komorebi.git` | `git@github.com:decryp2kanon/Sugarchain-En.git` |
| 로컬 주 작업 폴더 | `/home/ak/git/sugarchain-core31` | **`/home/ak/git/Sugarchain-En`** |
| CMake 프로젝트 식별자 | `SugarchainKomorebi` | `SugarchainEn` |
| Qt 조직 | `Sugarchain` | 유지 |
| Qt 앱 설정 이름 | `Sugarchain Komorebi`와 네트워크별 접미사 | `Sugarchain En`과 동일한 네트워크별 접미사 |
| macOS bundle 식별자 | `org.sugarchain.SugarchainKomorebi` | `org.sugarchain.SugarchainEn` |
| macOS 사용자용 앱 폴더 | `Sugarchain-Qt.app` | `Sugarchain En.app` |
| Windows 제품명·바로가기·신규 설치 폴더 | Sugarchain Komorebi | Sugarchain En |
| 새 배포 압축파일·설치파일 접두사 | 현재 배포 규칙 | `Sugarchain-En-<version>-<platform>` 형식으로 통일 |
| 현재 기본 브랜치 | `core31` | 유지 |

`core31`은 기반 버전을 나타내므로 브랜치까지 불필요하게 변경하지 않는다. 실행파일 `sugarchaind`, `sugarchain-cli`, `sugarchain-qt` 등과 코인명 Sugarchain은 Komorebi 제품명과 별개이므로 유지한다. 사용자가 지정한 새 폴더와 저장소 이름은 예외 없이 적용한다.

## PR 1과 현재 코드의 차이

기준 PR은 [PR 1](https://github.com/decryp2kanon/sugarchain-komorebi/pull/1)이다. 2026년 9월 27일 병합됐으며, 병합 커밋은 `813e6ab41e46848fd3fafde4bcb448fdd6982832`이다. 변경 규모는 173개 파일, 커밋 1개였다. 당시 Bitcoin Core v31.1을 바탕으로 제품명·통화 표시·실행파일·기본 경로·패키징을 Sugarchain Komorebi로 바꿨다. 그 PR 자체에는 슈가체인 네트워크·합의 이식이 포함되지 않았다.

**이번 작업은 PR 1을 그대로 역치환하는 작업이 아니다.** 현재 기준 커밋은 `bea59560651651f6a29fe637ca3dcbaa6dc75d52`이며, 이후 병합된 체인·성능 변경을 보존해야 한다. 예를 들어 `src/kernel/chainparams.cpp:84`에는 실제 Sugarchain mainnet 합의 설정이 있고, 같은 파일의 기본 P2P 포트는 34230이다. 반면 `doc/README.md:6`에는 아직 Bitcoin 네트워크·합의 기준이라는 옛 설명이 남아 있다. 이 문서 오류는 현행 코드에 맞춰 별도 수정한다.

현황 수치는 조사 시점의 기준이다. 실행 직전에 HEAD·브랜치·로컬 변경 상태를 다시 기록한다.

| 조사 항목 | 확인 결과 | 작업에 미치는 영향 |
|---|---|---|
| 추적 텍스트의 `Komorebi` 또는 `木漏れ日` | 70개 파일, 일치 행 150개 | GUI뿐 아니라 패키징·문서·시험까지 검토 필요 |
| 일치 행 세부 | 저작권 고지 30행, 저장소 링크 4행, 나머지 116행 | 역사적 고지와 현행 이름을 구분해야 함 |
| 이름이 포함된 추적 파일 경로 | 0개 | 추적 파일명 일괄 변경보다 내용과 외부 폴더 이전이 중심 |
| Git worktree 기록 | 주 작업 폴더 포함 15개, 그중 기존 경로가 없는 기록 1개 | 주 폴더 이동 뒤 연결 복구 필요, 이번에 임의 정리하지 않음 |
| 기존 경로와 프로젝트명이 들어간 CMake 캐시 | 19개 | 캐시 문자열을 수정해서 재사용하지 않고 새 빌드 생성 |
| 운영 중 GUI | 기존 주 폴더의 `build/bin/sugarchain-qt`에서 실행 중 | 개발·검증은 분리, 최종 경로 전환 때 정상 종료와 재실행 필요 |
| GitHub 저장소 ID | `1390440223` | 새 저장소를 만드는 대신 같은 저장소의 이름을 변경 |
| GitHub Pages | 비활성 | 전환 직전 다시 확인 |
| GitHub Actions API에 등록된 workflow | 0개 | 로컬 CI 파일 존재만으로 원격 자동 검증 성공을 가정하지 않음 |
| 열린 PR | 0개 | 전환 직전 다시 확인 |
| 새 폴더·새 원격 이름 | 폴더 미존재, 원격 조회 404 | 이름 예약을 보장하지 않으므로 전환 직전 재확인 |

## 바꿀 범위와 보존할 범위

### 제품과 운영체제 등록 정보

| 영역 | 실제 변경 지점 | 변경과 검증 |
|---|---|---|
| 제품명과 빌드 메타데이터 | `CMakeLists.txt:29`, `:53`, `:55` | 제품명, 프로젝트 식별자, 설명, 생성 설정 반영 |
| Qt 설정 이름 | `src/qt/guiconstants.h:49`부터, `src/qt/bitcoin.cpp:555`, `:610` | 새 이름과 구 설정의 최초 이관을 함께 구현 |
| GUI 표시 | About·도움말·에러·번역·설정·종료 안내 | 사용자에게 보이는 현행 Komorebi 표시 변경 |
| Linux 자동실행 | `src/qt/guiutil.cpp:643`, `:645` | 생성 desktop 이름과 실제 실행 경로 검증 |
| 결제 IPC | `src/qt/paymentserver.cpp:45` | 브랜드를 포함한 서버 이름과 구 GUI 감지·URI 전달 처리 |
| macOS | `share/qt/Info.plist.in:37`, `:46`, `src/qt/macnotificationhandler.mm:16` | bundle ID·알림 ID·앱 폴더·내부 실행파일 참조를 일치시킴 |
| macOS 배포 | `cmake/module/Maintenance.cmake:55`, `:75` 부근 | 앱과 ZIP 이름, 배포·서명·검증 경로를 함께 수정 |
| Windows 설치 | `share/setup.nsi.in:13`, `:55`, `:104`부터, `:157` | 제품명·설치경로·레지스트리·시작 메뉴·제거 항목과 업그레이드 이관 |
| 서비스 설명 | `contrib/init/sugarchaind.*` | 설명은 En으로 변경, 실행파일과 기존 서비스 식별자는 호환 유지 |
| 문서와 manpage | README·SECURITY·빌드 문서·`doc/files.md`·`doc/man/sugarchain*.1` | 새 주소와 명칭, 설정 이관·사용법 반영 및 manpage 재생성 |
| GitHub 안내 | `.github/ISSUE_TEMPLATE/` 등 | 사용자 안내와 현행 링크 수정 |
| 시험·벤치마크 | 이름·버전 출력 fixture와 설치·경로 검사 | 새 출력에 맞춰 기대값 수정, 동작 검증은 유지 |

macOS 앱 내부 실행파일은 실제 빌드 출력에 맞는 이름을 유지할 수 있다. 사용자용 앱 폴더를 바꾸면서 `CFBundleExecutable`과 실제 파일이 어긋나면 안 된다. 새 bundle ID에 따른 서명·알림 권한 변화도 시험한다. 아이콘 안에 옛 제품명이 들어 있으면 해당 이름만 교체하고, 이번 작업에 새 로고 디자인이나 기능 변경을 섞지 않는다.

Windows 신규 설치는 Sugarchain En 경로를 쓴다. 기존 설치를 감지한 경우 설정·제거 정보·바로가기를 보존한 정상 업그레이드 경로를 마련한다. 구 설치 폴더를 무조건 삭제하거나 서로 다른 제거 프로그램이 같은 데이터를 지우게 만들지 않는다.

### 로컬 개발 경로와 외부 도구

다음은 저장소 안의 소스 수정만으로는 해결되지 않는 항목이다. 실제 파일이 관리되는 위치에 맞춰 별도 패치·백업·검증 기록을 남긴다.

| 위치 | 필요한 변경 |
|---|---|
| `/home/ak/git/sugarchain-core31` | `/home/ak/git/Sugarchain-En`으로 주 폴더 이전 |
| `/home/ak/git/pr11-review-split` 등 연결 worktree | 주 저장소 이동 뒤 `.git` 연결과 공통 Git 디렉터리 복구 |
| `/home/ak/.local/bin/sugarchain-build` | 관리 래퍼로 연결되는 링크와 새 기본 동작 검증 |
| `/home/ak/.local/lib/enikk-temp/sugarchain/build-core31.sh:9` | CMake 소스 경로를 새 폴더로 변경 |
| 같은 폴더의 `sugarchain-build` 래퍼 | En용 경로·작업 표시·로그와 기존 `core31` 호출 호환 검토 |
| `/home/ak/tools/ibd_tester/sync_speed_test.sh:4` | 기본 CLI 실행 경로 변경 |
| `/home/ak/tools/ibd_tester/README.md:94` | 경로 예시 변경 |
| `/home/ak/tools/ibd_tester/test_ibd_terminal.py:99` | 제품명 출력 fixture 변경 |
| desktop·자동시작·서비스·IDE 설정·수동 바로가기 | 전환 직전 재검색하고 실제 존재하는 참조만 변경 |

`/home/ak/git/sugarchain`은 `master-v0.16.3` 계열의 별도 레거시 저장소다. 이 폴더와 관리 도구의 `legacy` 분기는 보존한다. 7개 OS 빌드 문서에 남아 있는 `decryp2kanon/sugarchain.git --branch core31-branding-rename`는 이번 제품의 설치 안내이므로 새 저장소와 실제 브랜치로 고친다. 같은 문자열이라도 레거시 저장소 자체나 역사적 인용을 가리키면 보존한다.

### 데이터와 프로토콜 호환성

다음은 제품명 변경 대상이 아니다.

- 기존 `.sugarchain` 데이터 폴더, `sugarchain.conf`, PID 파일의 기존 규칙, 지갑 DB·키·주소·블록·chainstate.
- Sugarchain 코인명, SUGAR 단위, 소수 자릿수, 금액 계산과 직렬화.
- genesis, 네트워크 magic, P2P·RPC 포트, DNS seed, 주소 prefix·Bech32 HRP, `sugarchain:` 결제 URI.
- 합의·YespowerSugar·SugarShield·난이도·보상과 기존 IBD 최적화.
- 실행파일·RPC 이름과 공개 사용법의 기존 호환성.
- `src/clientversion.cpp:24`의 현재 P2P 사용자 에이전트 이름 `Satoshi`. 제품 표시 이름과 별개이므로 이번에 변경하지 않는다.
- Bitcoin에서 상속된 내부 target·클래스·파일명, 기존 저작권자·라이선스, 과거 커밋·태그·릴리스·서명된 배포 파일.

`Komorebi`를 포함한 현행 브랜드와 경로는 전수 검토하되, 저작권 고지·과거 기록·구 설정을 읽는 이관 코드에는 옛 문자열이 필요할 수 있다. 각 잔존 위치와 이유를 허용 목록으로 남긴다. 짧은 문자열 `en`을 전역 치환해서 영문 번역 코드나 다른 단어를 손상시키지 않는다.

## 설정과 실행 호환 처리

### Qt 설정 최초 이관

Qt는 조직·앱 이름을 기준으로 설정을 찾으므로 이름만 바꾸면 기존 사용자의 설정이 초기화된 것처럼 보일 수 있다. [Qt QSettings 문서](https://doc.qt.io/qt-6/qsettings.html)에 따라 운영체제별 저장 위치를 다루고, 설정 파일을 직접 문자열 치환하지 않는다.

1. 첫 기본 프로필을 읽기 전, 기존 `Sugarchain Komorebi` 프로필과 새 `Sugarchain En` 프로필을 명시적으로 구분한다.
2. 새 프로필이 없는 최초 실행에서만 구 프로필을 복사한다. 이미 새 프로필이 있으면 그대로 사용하며 구 값으로 덮어쓰지 않는다.
3. 기본 datadir 선택 전에 필요한 기본 설정과, 네트워크 선택 후 필요한 네트워크별 설정의 이관 시점을 나눈다. mainnet·testnet·testnet4·signet·regtest를 섞지 않는다.
4. 원본 설정은 남긴다. 이관 중 상태와 정상적인 새 프로필을 구분하고, 임시 이관 공간에서 값·타입과 쓰기 성공을 검증한 뒤 활성화한다. 재실행 결과까지 확인한 뒤 완료 표시를 기록해 반복 복사를 막는다. 실패한 부분 복사본을 정상 새 설정으로 간주해 다음 실행에서 건너뛰지 않으며, 원본을 보존하고 복구·재시도 방법을 표시한다.
5. 구 프로필과 새 프로필이 모두 없는 신규 설치는 기본값으로 시작한다. 언어·창 위치·datadir 선택·GUI 옵션이 유지되는지 가상 프로필로 시험한다.
6. 로그에는 이관 단계와 성공 여부만 남기고 설정 값·RPC 인증·개인정보를 덤프하지 않는다.

### 결제 URI와 중복 실행

새 IPC 이름은 `SugarchainEn`과 기존 datadir 구분 규칙을 조합한다. 동일 datadir의 구 GUI가 살아 있으면 구 IPC로 URI를 전달할 수 있도록 호환 경로를 두고, 전송 성공 여부를 확인한다. 새 GUI가 이미 실행 중이면 새 IPC로 한 번만 전달한다. 수신할 수 없는 경우 명확한 안내를 표시하고 지갑을 중복 실행하지 않는다.

신규·구버전 사이의 URI 중복 전달, 다른 네트워크로의 전달, 동일 datadir 중복 열기를 시험한다. 기존 데이터 잠금을 제거하거나 우회하지 않는다.

## 기능별 커밋과 검증 순서

소스·저장소 밖 도구·원격 설정 변경을 한 덩어리로 묶지 않는다. 한 커밋은 하나의 검토 가능한 기능에 한정한다. 아래 커밋은 구현을 시작할 때의 분리 기준이며, 이번 계획서 작성 단계에서는 커밋·푸시하지 않는다.

| 순서 | 커밋 또는 운영 단위 | 완료 조건 |
|---|---|---|
| 1 | 명칭 대응표와 잔존 허용 목록 | 새 이름과 유지 항목을 기계적으로 검사할 기준 확보 |
| 2 | 제품 표시와 CMake 메타데이터 | 도움말·버전·About에 Sugarchain En 표시, 기본 빌드 성공 |
| 3 | Qt 프로필 최초 이관 | 모든 네트워크의 구 설정 보존, 새 설정 덮어쓰기 없음 |
| 4 | 결제 IPC 이름과 구버전 호환 | URI가 한 번만 전달되고 중복 지갑 실행 없음 |
| 5 | Windows 설치 업그레이드 | 새 설치·기존 설치 전환·제거와 데이터 보존 검증 |
| 6 | macOS 앱과 패키지 이름 | 앱·bundle·알림·서명 대상·ZIP 경로의 일치 |
| 7 | Linux 자동실행과 서비스 표시 | 새 명칭으로 표시, 실행파일·자동시작 경로 정상 |
| 8 | 현행 문서·man·GitHub 안내 정리 | 새 clone·빌드·설치 안내 성공, 과거 고지 보존 |
| 9 | 낡은 네트워크 안내 문서 수정 | 현행 chainparams와 문서 설명 일치 |
| 10 | 외부 빌드 래퍼와 IBD 도구 경로 이전 | 새 폴더 기준 동작과 레거시 분기 보존 |
| 별도 | GitHub 저장소 rename와 로컬 주 폴더 이동 | 같은 저장소 ID·Git 이력·worktree·사용자 파일 보존 |

코드가 분리된 GUI·플랫폼 패키징·문서는 병렬로 조사·수정할 수 있다. Git 인덱스·최종 빌드·커밋·푸시·GitHub rename·폴더 이전은 주 작업자가 순서대로 조율한다. 같은 CPU·메모리를 많이 쓰는 빌드와 브라우저 시험을 무조건 겹치지 않는다.

## 실제 이전 절차

### 사전 기록과 분리 구현

1. 현재 HEAD·브랜치·원격·모든 worktree의 HEAD·로컬 변경과 비추적 파일을 기록한다. 현재 있는 사용자 PNG 등 비추적 파일은 임의 커밋·삭제하지 않는다.
2. 실행 중 지갑의 실행파일·시작 시각·실행 사용자·작업 경로를 확인한다. PID만 믿거나 사용자 앱을 시험 목적으로 종료하지 않는다.
3. 별도 등록 작업 폴더에서 구현·검증한다. 새 작업·빌드·로그는 NVMe의 `enikk-temp` 경로를 쓰고, 완료 결과만 자동 보관 대상으로 표시한다. Git 저장소·지갑 데이터는 자동 보관 대상으로 등록하지 않는다.
4. 기존 빌드와 새 빌드의 체인 식별·RPC·명령 인터페이스를 비교할 기준을 만든다. 실제 지갑을 읽기 위해 운영 DB를 동시 개방하지 않는다.
5. 변경 코드의 검증이 끝나기 전에는 원격 저장소와 운영 폴더 이름을 바꾸지 않는다.

### GitHub 저장소 이름 변경

1. 전환 직전에 새 이름 충돌, 저장소 ID, 기본 브랜치·보호 규칙·ruleset·권한·Pages·workflow·웹훅·릴리스·외부 의존을 다시 확인한다. 비밀 값을 로그로 출력하지 않는다.
2. 새 저장소를 생성하거나 이력을 복사하지 않고 기존 저장소를 **`Sugarchain-En`으로 rename**한다.
3. 이름 변경 후 저장소 ID가 `1390440223`으로 같은지, 브랜치·태그·PR·이슈·릴리스가 유지되는지 대조한다.
4. 모든 실제 clone의 origin을 새 주소로 변경한다. 연결 worktree는 공통 Git 설정을 쓰므로 저장소별로 한 번 변경하고 각 worktree에서 확인한다. 별도 clone은 각각 확인한다.
5. README·clone 명령·배지·릴리스 링크·외부 스크립트를 새 주소로 맞춘다. 새 주소로 `ls-remote`와 별도 clone을 시험한다.
6. GitHub 일반 URL과 Git 명령의 redirect만 믿지 않는다. Pages URL과 외부 `uses: OWNER/REPO/...` action 경로는 별도 검증 대상이다. 옛 이름으로 새 저장소를 만들지 않는다. [GitHub 공식 rename 지침](https://docs.github.com/en/repositories/creating-and-managing-repositories/renaming-a-repository)

GitHub 푸시로 수행되는 자동화가 있다면 해당 자동화에 맡긴다. 자동화가 진행 중인 상태에서 수동 배포를 겹치지 않는다. API에 workflow가 없다는 현재 결과만으로 모든 배포 자동화가 없다고 단정하지 않는다.

### 로컬 주 폴더와 개발 환경 전환

주 worktree는 `git worktree move`로 이동할 수 없다. 운영 앱과 관련 작업을 정상 종료할 전환 시점을 정한 뒤 일반 파일시스템 이동과 Git의 공식 `repair` 절차를 사용한다. [Git worktree 공식 문서](https://git-scm.com/docs/git-worktree)

1. 사용자 앱 전환 전에 새 코드와 빌드 결과를 검증하고, 작업 중인 에이전트·셸·빌드가 기존 폴더를 변경하지 않는지 확인한다.
2. 운영 지갑의 정상 저장·종료를 확인한다. 강제 종료하지 않으며, 데이터 디렉터리 이름을 바꾸지 않는다.
3. `/home/ak/git/Sugarchain-En`이 없는지 다시 확인하고, 같은 파일시스템 안에서 기존 주 폴더를 새 이름으로 이동한다. 파일 내용·권한·Git 객체를 복사본으로 덮어쓰지 않는다.
4. 새 주 폴더에서 `git worktree repair`를 수행한다. 연결 worktree도 이동한 경우 해당 새 경로를 명시한다. worktree마다 공통 Git 디렉터리·HEAD·브랜치·변경 파일 상태를 이전 기록과 비교한다.
5. 기존 경로가 없는 worktree 기록은 이번 변경을 이유로 prune하지 않는다. 다른 작업자가 쓰는 worktree나 브랜치를 정리하지 않는다.
6. 외부 래퍼·IBD 도구·바로가기의 실제 참조를 새 폴더로 바꾼다. 과거 `core31` 명령 별칭이 필요하면 새 경로로 연결하되 주 작업 폴더 자체를 옛 이름의 영구 symlink로 숨기지 않는다.
7. 19개 기존 CMake 캐시는 원본 그대로 보존한다. 새 소스 경로에서 새 빌드 디렉터리를 만들고 clean configure·build·install 검증을 수행한다. `CMakeCache.txt`의 경로 문자열만 바꿔 재사용하지 않는다.
8. 검증된 새 실행파일로 재실행한다. 기존 datadir·지갑·GUI 설정이 이어지는지 확인하고 중복 프로세스와 중복 자동시작이 없는지 점검한다.

최종 빌드가 끝나기 전까지 구 빌드·설정 원본·외부 도구 백업을 남긴다. 사용 중인 앱의 저장소 폴더를 진행 중에 이동하는 방식으로 무중단 이전을 주장하지 않는다.

## 검증 계획과 완료 기준

검증용 데이터·Qt 설정은 운영 환경과 완전히 분리한다. 실제 개인 키를 도구 출력·fixture·업로드에 넣지 않는다. 지갑 호환은 우선 새 시험 지갑으로 검증하며 기존 지갑 복사본이 필요한 경우 정상 백업 절차와 저장소 접근 범위를 별도로 확인한다. 동작 중인 지갑 DB 파일을 단순 복사해서 신뢰할 수 있는 백업으로 취급하지 않는다.

| 시험 | 합격 기준 |
|---|---|
| 잔존 이름 검사 | 현행 제품명·문서·설치물·생성 파일에 의도치 않은 Komorebi가 없고, 남긴 역사·이관 문자열은 허용 목록과 일치 |
| 새 주소 clone | 새 URL로 가져온 깨끗한 소스에서 문서대로 설정·빌드 가능 |
| Linux GUI·daemon·CLI·IPC | 버전·도움말·About·RPC·종료가 정상, 표시명은 Sugarchain En |
| Qt 구 설정만 있음 | 첫 실행에서 설정 보존, 재실행 때 다시 복사하지 않음 |
| Qt 새 설정이 이미 있음 | 구 값으로 덮어쓰지 않음 |
| Qt 네트워크 분리 | mainnet·testnet·testnet4·signet·regtest가 기존과 동일하게 분리 |
| 결제 URI | 구·신 GUI 각각의 전달, 정확히 한 번 처리, 다른 datadir·네트워크와 혼선 없음 |
| 데이터 잠금 | 같은 datadir의 중복 실행 차단이 유지되고 잠금 삭제·우회 없음 |
| 체인 불변 | genesis·주소 형식·포트·합의·금액 단위·지갑 직렬화·기존 IBD 설정이 기준과 동일 |
| 시험 지갑 | 별도 regtest 환경의 생성·재실행·송수신·잔액 및 RPC 결과 정상 |
| Windows 설치·업그레이드·제거 | 등록 정보·바로가기 정상, 사용자 데이터와 설정 보존 |
| macOS 앱·설치물 | 앱 이름·bundle·실행파일·알림·서명 대상 일치, 실행과 설정 이관 정상 |
| 로컬 폴더와 worktree | 주 경로는 정확히 `/home/ak/git/Sugarchain-En`, 연결된 실제 worktree의 이력·수정 내용 보존 |
| 빌드·IBD 래퍼 | 새 경로와 fixture 동작, 별도 legacy 저장소의 동작 보존 |
| 원격 이력 | 저장소 표시 이름은 정확히 Sugarchain-En, 동일 ID·브랜치·태그·PR·릴리스 유지 |

플랫폼별로 실제 실행 시험과 정적 검토를 구분해 보고한다. Windows/macOS 실행 환경이 없으면 그 항목을 정적 검토만으로 합격 처리하지 않는다. 아직 검증하지 않은 플랫폼은 남은 조건으로 명시한다.

검증 중 의도적 충돌·메모리 부족·운영체제 오류 알림이 생길 시험이 필요하면 먼저 알린다. 일반 이름 변경 검증에 불필요한 충돌 실험은 넣지 않는다. 시험 로그는 요약과 비공개 파일로 나누며 대화에 전체 소스·로그를 중계하지 않는다.

**완료는 새 이름이 화면에 보이는 것만으로 판정하지 않는다.** 새 저장소 주소, 정확한 로컬 폴더, 설정 연속성, 지갑·체인 호환, worktree와 외부 빌드 도구까지 이어져야 한다. 코드 커밋과 원격·로컬 운영 변경의 결과를 각각 남긴다.

## 되돌리기

| 문제가 난 단계 | 복구 방법 |
|---|---|
| 코드 검증 전 | 해당 기능 커밋만 revert하고 기존 작업·이력 보존. 강제 reset·force push 금지 |
| Qt 설정 이관 | 보존한 구 프로필로 구버전 실행. 새 프로필을 구 프로필에 역복사하지 않음 |
| Windows/macOS 패키징 | 검증된 이전 설치물과 등록 정보 백업으로 복원. 지갑 데이터 삭제 금지 |
| 외부 래퍼 수정 | 기록된 이전 파일·링크 복원 후 기존 경로로 동작 확인 |
| 로컬 폴더 전환 | 관련 앱 정상 종료 후 주 폴더를 원래 경로로 되돌리고 `git worktree repair`, 래퍼·origin 재점검 |
| GitHub rename | 이름 충돌·외부 참조를 확인한 뒤 같은 저장소의 이름을 되돌림. 원격 origin과 문서도 함께 복원 |

실행 파일과 앱 이름의 롤백을 위해 지갑이나 chainstate를 덮어쓰지 않는다. 이번 작업은 데이터 형식 변경을 포함하지 않으므로 기존 DB 원본을 보존하는 방향으로 복구한다.

## 실행 전 마지막 확인

- [ ] 제품 Sugarchain En, 폴더와 저장소 Sugarchain-En의 대소문자 일치
- [ ] 최신 HEAD·변경 파일·worktree·실행 앱·원격 설정 다시 기록
- [ ] 이름이 같은 기존 로컬 폴더와 GitHub 저장소가 없는지 확인
- [ ] 기능별 커밋과 설정 이관·IPC·패키징 검증 준비
- [ ] 검증된 구 빌드와 개인 설정·외부 도구의 복구 자료 확보
- [ ] 사용자 앱의 정상 종료·전환 시점 확정
- [ ] 코드 검증 → 원격 rename → 로컬 이전·repair → 새 빌드·재실행 순서 준수

이 문서의 현황 조사는 2026년 11월 17일 계획 수립 당시 기준이다. 이후 사용자의 실행 지시에 따라 이름 변경을 진행했으며, 실제 완료 범위와 플랫폼별 검증 한계는 실행 결과 문서에 기록했다.
