# 필수 앱 네 개: 로더와 API 우선순위

2026-09-24 현재의 **정적 PE 조사**다. 이 문서의 Chromium, Supermium, VLC, VS Code는 아직 Windows 98 게스트에서 실행해 보지 않았다. 실제 시작·화면 표시·기능 사용은 모두 미검증이다. Notepad++의 별도 게스트 관찰은 [대상 앱 문서](TARGET_APPS.md)에 있다. 아카이브 버전과 SHA-256도 그 문서에 고정되어 있다.

기준은 [원본 Windows 98 SE CAB 내보내기 목록](../benchmarks/win98se-ko-oem-native-exports-v1.json), [KernelEx 소스 선언 목록](../benchmarks/kernelex-source-declarations-v1.json), [선택 PE 10개의 가져오기 보고서](../benchmarks/pe-import-name-report-v1.json)다. `python tools/run_curated_import_report.py`로 마지막 보고서를 다시 만들 수 있다. 현재 보고서는 프로젝트 provider/API 표와 shim `.def` 소스 10개를 읽고, COMCTL32의 이름과 서수도 포함한다. 이름 일치에는 실제 게스트 설치·연결·호출 성공이나 동작 의미가 포함되지 않는다. 원본 CAB 목록에는 선택 설치 파일도 포함된다. 보고서의 4,365/4,980 (87.6506%)는 선택 PE의 **정적 import 이름**만의 분류값이며 Windows API 전체 호환률이나 앱 실행률이 아니다.

| 실행 순서 | 대상 및 검사한 PE | PE 기계 / 선언 OS·서브시스템 | 선택 PE의 일반·지연 가져오기와 이름 일치 | 처음 조사할 구조적 의존성 |
| --- | --- | --- | --- | --- |
| 1 | VLC 3.0.24: `vlc.exe`, `libvlc.dll`, `libvlccore.dll` | 세 파일 모두 x86 PE32 (`0x014C`), OS·서브시스템 버전 4.0 | 657 + 0; 657/657. 원본 CAB 505, 앱 동봉 138, KernelEx **소스 선언만** 14 | 실제 KernelEx 연결, GUI·파일 접근·코덱·오디오 플러그인 로드 |
| 2 | Supermium 144 R5: `chrome.exe`, `chrome.dll`, `chrome_elf.dll` | 세 파일 모두 x86 PE32, OS·서브시스템 버전 5.0 | 977 + 862; 1,607/1,839 (87.38%). 앱 동봉 DLL의 *내보내기 이름* 897개 포함 | 자체 래퍼 DLL의 `NTDLL`·`PSAPI` 의존성과 `SECUR32`·`WINTRUST` 직접 가져오기 |
| 3 | Chromium 150 snapshot r1639845: 같은 세 이름의 PE | 세 파일 모두 x86 PE32, OS·서브시스템 버전 10.0 | 967 + 903; 1,487/1,870 (79.52%). 미해결 383회 | PE 버전 로더 검사, 직접 가져오는 최신 `KERNEL32`/`NTDLL` 및 `DWRITE` |
| 4 | VS Code 1.138.0: `Code.exe` | x64 PE32+ (`0x8664`), OS·서브시스템 버전 10.0 | x86 보고서에서 제외. 0% 또는 100%로 환산하지 않음 | 32비트 Windows 98에는 x64 PE 로더와 x64 사용자 모드 실행 환경이 없음 |

## 1. VLC: 먼저 게스트의 실제 오류를 얻기

선택한 세 PE는 원본 CAB/동봉 DLL/KernelEx 소스와 가져오기 이름이 전부 맞지만, `vlc.exe`의 `GetProcessId`, `HeapSetInformation`, `OpenThread`, `RtlCaptureContext`, `libvlccore.dll`의 타이머 큐·`SetFilePointerEx`·`getaddrinfo` 등은 **KernelEx 소스 선언**에 의존한다. 현재 게스트 구성에서 각 항목이 실제로 제공되는지는 별도 검사해야 한다. 657/657은 VLC 실행 성공을 뜻하지 않는다.

보고서의 세 PE 이외에 실제 아카이브의 `plugins/gui/libqt_plugin.dll`을 조사했다. 일반 가져오기 653개 가운데 `KERNEL32.DLL!CheckRemoteDebuggerPresent`, `GetGeoInfoW`, `GetUserGeoID` 세 이름은 원본 CAB·KernelEx 선언·동봉 `libvlccore.dll`의 내보내기와 맞지 않았다. 같은 방법으로 본 `plugins/access/libfilesystem_plugin.dll`, `plugins/demux/libwav_plugin.dll`, `plugins/audio_output/libwaveout_plugin.dll`에서는 이 세 목록에 대한 미해결 일반 가져오기가 없었다. 이는 **선택한 네 플러그인의 정적 이름 비교**이며 다른 플러그인, 실행 중 로드, API 동작을 검증하지 않는다.

다음은 게스트에서 휴대용 배포본을 실행해 첫 로더 오류를 기록하고, GUI가 뜨면 작은 WAV 파일 열기와 waveOut 재생을 따로 검사한다. GUI 플러그인이 위 세 항목 때문에 멈춘다면 함수 계약과 Win98에서 가능한 결과를 Wine·ReactOS 양쪽 소스에서 확인한 뒤 정확한 래퍼를 만든다. 실패 위치를 모르는 상태에서 성공을 반환하는 스텁을 추가하지 않는다.

## 2. Supermium: 앱 자체 래퍼의 아래층부터 검사

`chrome.exe` 자체의 일반 가져오기 233개는 원본/동봉 DLL/KernelEx **이름**에 모두 일치하지만, 그중 221개가 동봉 `PWRP_K32.DLL`로 향한다. 선택한 세 PE 보고서는 그 보조 DLL들의 **가져오기 방향**을 점수에 넣지 않았다. 실제 아카이브의 `p_*.dll` 16개와 `pwrp_k32.dll`을 추가로 읽으면, 17개 모두가 `NTDLL.DLL!LdrGetProcedureAddress`를 일반 가져오기로 선언한다. 이 이름은 고정 원본 CAB 목록과 KernelEx 소스 선언에 없다. `pwrp_k32.dll`에는 이 항목을 포함해 이름이 맞지 않는 일반 가져오기 16개가 있고, 그중 `PSAPI.DLL`의 프로세스·모듈 조회 8개가 포함된다. 원본 ISO 목록에는 `PSAPI.DLL` 자체가 없다. 이는 게스트 설치본의 DLL 부재를 증명하지 않지만, 앱 내부 래퍼도 독립된 로더 대상임을 보여 준다.

`chrome.dll`에는 원본 CAB/KernelEx 이름과 맞지 않는 **일반** 가져오기 `SECUR32.DLL` 9개(자격 증명·SSPI·LSA)와 `WINTRUST.DLL` 6개(카탈로그 검사)가 있다. 나머지 미해결 232회 중 상당수는 지연 가져오기다. 우선 `LdrGetProcedureAddress`의 ABI와 로더 의미, `PSAPI` 대체 가능성, 보안 API의 실패·성공 계약을 조사한다. 그다음 동봉 DLL 전체가 게스트에서 로드되는지 확인하고 브라우저 창과 간단한 로컬 페이지 표시를 시험한다. 단순 내보내기 이름 추가만으로 이 단계가 해결되었다고 세지 않는다.

## 3. Chromium 150: 로더·NT 의미·그래픽 모두 필요

세 PE의 선언 OS/서브시스템은 10.0이므로 Windows 98 로더에서 어떤 오류가 나는지 **실제 게스트로 먼저 확인**해야 한다. 버전 필드만 바꿔도 실행된다는 근거는 없다. 선택 PE에서 미해결 일반 가져오기는 `KERNEL32.DLL` 85회, `NTDLL.DLL` 13회이며, `chrome.dll`은 `DWRITE.DLL!DWriteCreateFactory`를 일반 가져오기로 선언한다. 원본 ISO 내보내기 목록에 `DWRITE.DLL`은 없다. `CRYPT32`와 `WS2_32`의 최신 함수도 일반 가져오기에서 남아 있다. `DBGHELP`, `WINHTTP`, 미디어·그래픽·API-set DLL의 여러 항목은 지연 가져오기이므로 초기 로더 오류와 이후 기능 실패를 구분해야 한다.

SRW·InitOnce·FLS와 일부 조건 변수 이름은 현재 프로젝트 소스 선언과 일치한다. 이 이름 일치가 Chromium의 실제 실행이나 호출 의미를 검증하지는 않는다. `GetLogicalProcessorInformation`, 벡터 예외 핸들러, NT 객체·메모리 호출, DirectWrite 구현 등은 여전히 별도 과제다. 다음 래퍼 후보는 실제 게스트 로더 메시지와 호출 계약을 근거로 정한다. 브라우저의 렌더링·샌드박스·CPU 요구는 정적 이름 표 밖의 검증 항목이다.

## 4. VS Code 1.138: 별도 실행 아키텍처가 필요

공식 x64 아카이브의 `Code.exe`는 `0x8664`/PE32+다. 현재 x86 Windows 98와 KernelEx의 32비트 API 래퍼가 이 실행 파일을 직접 적재할 수 없다. 호스트의 VT-x/AMD-V 사용이나 앱별 CPU 프로필도 게스트 프로세스를 x64로 바꾸지 않는다. 이 고정 버전을 요구한다면 x64 PE 로딩, 64비트 코드 실행, 64비트 Windows 사용자 모드 API와 Electron 런타임을 다루는 **별도 실행 경로**를 설계·구현하고 게스트에서 검증해야 한다. 과거 32비트 VS Code로 바꿔서 이 필수 버전의 성공으로 계산하지 않는다.

## 재현과 판정

`tools/run_curated_import_report.py`의 `TARGETS`/`PEERS`가 보고서에 포함한 실제 파일을 명시한다. 각 PE의 기계 및 OS·서브시스템 선언은 `llvm-readobj --file-headers <PE>`로, 일반 가져오기는 `llvm-readobj --coff-imports <PE>`로 다시 확인할 수 있다. 이 문서의 VLC 플러그인과 Supermium 보조 DLL은 동일 아카이브의 추가 PE를 읽어 원본·KernelEx 선언 및 동봉 `libvlccore.dll` 내보내기와 비교했다. 지연 가져오기는 `tools/measure_pe_coverage.py`가 별도로 센다.

각 앱의 판정에는 **게스트 최초 로더 결과 → 앱 창 표시 → 의미 있는 기능 수행 → 반복 실행 안정성**을 서로 다른 기록으로 남긴다. 이 문서에서 네 앱의 현재 게스트 실행 결과는 모두 **미시험**이다. 이 표의 숫자를 Windows API 전체 표면의 호환률이나 필수 앱 구동률로 사용하지 않는다.
