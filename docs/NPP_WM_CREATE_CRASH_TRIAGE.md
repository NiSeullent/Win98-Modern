# Notepad++ 8.9.8 `WM_CREATE` 충돌 위치 측정

GDI 별칭 브리지가 설치된 Win98 SE 게스트에서, 쓰기 가능한
`C:\M98LAB\NPP`의 Notepad++ 8.9.8 x86가 로더 단계를 통과했다.
`APP_PROBE.EXE`의 [원본 실행 영수증](../build/gdi-alpha/guest-npp-writable.json)
(SHA-256 `2ff6392a56971622a258132a6ba6af5909bf9be756a6b4ae16d3d4af88da95fa`)
은 약 1.3초 뒤 클래스 `#32770`, 제목 `Exception On WM_CREATE` 창을
관측했고, 30초 뒤에도 자식 프로세스가 살아 있어 종료했다고 기록한다.
[화면](../build/gdi-alpha/npp-writable-visual.png)
(SHA-256 `92c79150f9b2bb5e98f8e0bb4d73f9e588c3f17ea714f1ad8d72489f4f57063e`)
에는 본문 `Access violation`이 보인다. 정상 편집기 창이나 문서 편집
성공은 관측되지 않았다. 이 실행으로 GDI 브리지가 정확한 픽셀을 만든다거나
어느 API가 충돌을 일으켰다고 판단할 수 없다.

[공식 v8.9.8 소스의 `NppBigSwitch.cpp`](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/v8.9.8/PowerEditor/src/NppBigSwitch.cpp#L2535-L2582)는
이 제목을 `WM_CREATE`의 `catch (std::exception&)`에서 만든다. `try`
범위에는 다크 모드의 제목 표시줄·메뉴·색상 서브클래스 설정,
`Notepad_plus::init(hwnd)`, 선택적 프레임 갱신,
`SetOSAppRestart()`와 `addClipboardListener()`가 들어 있다.
따라서 대화상자의 제목과 문구만으로 호출 지점이나 DLL을 고를 수 없다.
`Access violation` 문구가 실제 첫 번째 `0xC0000005` 예외와 일치하는지
조차 이 화면 영수증만으로는 알 수 없다. 아래의 별도 디버그 실행에서는
`0xC0000005`가 여러 번 기록되었다.

고정된 `notepad++.exe` 자체는 PE32 x86, SHA-256
`960ad7a8d443536ceeeafa18f99aecb70793fc7c4fb6ef23678a9d46bbf7bc78`,
`SizeOfImage=0x767000`, PE 시각표 `0x6A8B0FD6`이다. 직접 가져오기에는
`GdiAlphaBlend`, `SetLayeredWindowAttributes`, `ReplaceFileW`,
`CheckTokenMembership`, `SHOpenFolderAndSelectItems`, 두 클립보드 구독
함수가 있다. 앞선 [KernelEx 본문 감사](KERNELEX_STUB_AUDIT.md)는 일부
원본 KernelEx 대상이 명시적 또는 성공처럼 보이는 stub임을 확인했다.
하지만 *정적 가져오기*는 `WM_CREATE` 중 해당 API가 실제 호출됐다는
증거가 아니다. 프로젝트에서 교체한 라우트도 별도로 확인해야 한다.

## 별도 디버그 실행기

[소스](../tests/npp_seh_trace.c)와
[빌드 도구](../tools/build-npp-seh-trace.ps1)는 원본 애플리케이션이나
KernelEx 라우트를 수정하지 않는 경계형 디버그 실행기를 만든다.
`CreateProcessA(DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS)`로 대상 하나를
시작하고 `WaitForDebugEvent`를 관찰한다. 접근 위반을 받은 뒤에는 항상
`ContinueDebugEvent(..., DBG_EXCEPTION_NOT_HANDLED)`로 원래 예외 처리기에
넘긴다. 예외를 성공으로 바꾸거나 주소를 패치하지 않는다.
첫 번째·두 번째 예외 여부, 코드, 명령 주소, 읽기/쓰기 종류, 접근 주소,
`EIP/ESP/EBP`, 스택 상위 8개 DWORD와 적재된 이미지의 기준 주소·크기·
PE 시각표를 기록한다. 실행 시간은 20초이며 종료되지 않으면 대상만
종료하고 4초 동안 디버그 종료 이벤트를 회수한다. 디버거 연결 자체가
타이밍 또는 `IsDebuggerPresent` 경로를 바꿀 수 있으므로 기존
`APP_PROBE` 결과와 나란히 보아야 한다.

```powershell
./tools/build-npp-seh-trace.ps1
```

빌드된 `build/npp_seh_trace.exe` SHA-256은
`205381285ef3fd6808ba7454fef0ab4741086c9e34599c3cb9bb1a9a4487889c`이다.
[정적 검사기](../tests/check_npp_seh_trace_pe98.py)는 PE32 x86 콘솔
4.10, Windows 98 OEM 목록의 원본 `KERNEL32.DLL` 이름만 가져오는지,
TLS·지연 가져오기·CLR·ASLR/NX가 없는지 확인했다. 호스트 Windows에서
일부러 널 포인터에 쓰는 작은 임시 프로그램을 추적하여 첫 번째와 두
번째 `0xC0000005`, 쓰기 주소 0, 이미지 기준 주소와 상대 오프셋,
대상 종료 코드를 기록했다. **이것은 Win98의 디버그 API 동작 시험이
아니다.** 원본 CAB에 관련 API 이름이 있다는 사실도 게스트 성공을
보증하지 않는다.

통합 담당자가 실행기 SHA-256을 확인한 뒤 Win98 게스트의 쓰기 가능한
동일 폴더에서 실제로 호출했다. 업로드 영수증은
[`guest-npp-seh-tracer-put.json`](../build/gdi-alpha/guest-npp-seh-tracer-put.json),
원본 출력 영수증은
[`guest-npp-seh-v2.json`](../build/gdi-alpha/guest-npp-seh-v2.json)
(파일 SHA-256 `81032f3ce40728d9a2c8d4d3f61758024dea8acb5b674bca6b397beb8e63418b`)이다.
재사용하기 쉬운 축약 자료는
[`npp-seh-wm-create-v1.json`](../benchmarks/npp-seh-wm-create-v1.json)에
고정했다. 이 자료는 실행 시각 같은 변경 필드를 넣지 않고 원본 영수증,
호스트 PE 검사 및 배포 확인 자료의 해시와 핵심 사건만 담는다.

```text
C:\M98LAB\NPPSEH.EXE "C:\M98LAB\NPP\notepad++.exe"
```

## 게스트 예외와 모듈 식별

디버그 출력은 111개 이벤트, 첫 기회 접근 위반 4개, 첫 기회 C++ 예외
2개를 기록했다. 모든 접근 위반을 애플리케이션 처리기로 넘겼다.
20초 제한 뒤 대상 프로세스를 종료했으며 종료 코드 1460은 정상 종료나
편집 기능 성공을 뜻하지 않는다. 별도
[`APP_PROBE` 영수증](../build/gdi-alpha/guest-npp-v2-writable.json)과
[화면](../build/gdi-alpha/npp-v2-writable-visual.png)은 같은 쓰기 가능한
복사본에서 `Exception On WM_CREATE` 창이 30초 동안 남았음을 다시
확인했다. 두 실행은 별도 프로세스이며 동일한 예외 순서를 입증하지는
않는다.

| 순서 | 첫 기회 예외 | 지점 | 접근 주소·해석 |
| --- | --- | --- | --- |
| 1 | 읽기 `0xC0000005` | `KERNEL32.DLL+0x117E` (`BFF7117E`) | `FFFFFFFF`; 처리 여부만으로 영향 판단 불가 |
| 2 | 읽기 `0xC0000005` | `COMCTL32.DLL+0x5` (`BFE90005`) | `FFFFFFFF`; 아래의 서수 381 경로와 강하게 연결됨 |
| 3 | `0xE06D7363` | `notepad++.exe+0x368902` | C++ 예외 생성 지점 |
| 4 | 읽기 `0xC0000005` | `KERNEL32.DLL+0xA606` | `FFFFFFFF`; 선행 예외와 관계 불명 |
| 5 | 읽기 `0xC0000005` | `notepad++.exe+0x1AAD44` (`005AAD44`) | `FFFFFFFF`; 탭 닫기 아이콘 ID 벡터 읽기 |
| 6 | `0xE06D7363` | `notepad++.exe+0x368902` | 같은 C++ 예외 생성 지점 |

게스트 로드 이벤트의 `BFF70000 / SizeOfImage 0x73000 / stamp
0x3733EB91`은 고정 Windows 98 매체에서 추출한
`benchmarks/media/win98-iso-extracted/kernel32.dll`과,
`BFE90000 / 0x8C000 / 0x3738D939`는 같은 매체의
`benchmarks/media/win98-iso-extracted/comctl32.dll`과
각각 일치한다. 로드 이벤트에는 파일 이름 또는 해시가 없으므로
*게스트 파일의 바이트 동일성*까지 확인한 것은 아니다. 배포 확인 자료는
`M98GDI2.DLL` SHA-256 `c6db6ed0…d6b7a46cd4`와 `CORE.INI`
SHA-256 `f5971fee…0f0f4ef2044`를 보증한다. 추적 순간의 두 파일을
재읽어 해시를 확인하지는 않았다. 전체 해시와 자료 출처는 축약 JSON에
있다.

## 탭 닫기 아이콘 경로

고정된 EXE를 `llvm-objdump`로 역어셈블하면 `0x005AAC20`부터 시작하는
함수는 아이콘 ID 벡터를 만들고, `0x005AAD2B`에서 `ImageList_Create`를
호출한다. `0x005AAD44`의 실제 명령은 `movzx eax, word ptr [ebx]`이며,
그 뒤 `0x005AAD5F`가 IAT `0x007C90A8`을 호출한다. PE 가져오기 표에서
이 칸은 **`COMCTL32.dll` 서수 381**이다. 이어지는 분기는 `LoadImageW`,
`ImageList_ReplaceIcon`, `DestroyIcon`을 쓴다. 코드의 상수
`0x5FF`(1535), `0x60D`(1549)와 크기 11/16은
[v8.9.8 `resource.h`의 탭 닫기 아이콘 ID](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/v8.9.8/PowerEditor/src/resource.h#L373-L392)와 맞는다.
따라서 이 바이너리 함수가
[v8.9.8 `TabBarPlus::setCloseBtnImageList`](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/v8.9.8/PowerEditor/src/WinControls/TabBar/TabBar.cpp#L489-L535)에
대응한다는 근거가 강하다.

공식 소스에서 `TabBarPlus::init`는 닫기 버튼 이미지 목록을 만들고,
`setCloseBtnImageList`는 각 리소스 ID에 대해
`DPIManagerV2::loadIcon`을 호출한다. [그 함수의 v8.9.8 구현](https://github.com/notepad-plus-plus/notepad-plus-plus/blob/v8.9.8/PowerEditor/src/dpiManagerV2.cpp#L210-L216)은
먼저 `LoadIconWithScaleDown`을 부르고, 실패 값을 받으면 `LoadImage`로
대체한다. [Microsoft의 API 계약](https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-loadiconwithscaledown)은
`HINSTANCE, 리소스 이름, 너비, 높이, HICON*` 다섯 인자를 정의한다.
`COMCTL32+0x5` 예외 시 스택 첫 다섯 DWORD는
`00400000, 000005FA, 0000000B, 0000000B, 00E0CB70`이다. `0x5FA`는
`IDR_CLOSETAB`(1530)이고, 나머지도 다섯 인자의 형태에 맞는다.
소스와 PE의 이 일치는 `TabBarPlus::init → setCloseBtnImageList →
DPIManagerV2::loadIcon → COMCTL32 서수 381`이라는 호출 흐름을
지지한다. 서수 번호 자체를 다른 Windows 버전의 공식 API 번호로
일반화해서는 안 된다.

고정 Win98 `COMCTL32.DLL`의 PE export table은 시작 서수 2,
420개 함수를 선언하지만 **서수 381의 EAT RVA가 0**이다.
파일 첫 바이트 `4D 5A 90 00 03 00 00 00`를 32비트 명령으로 읽으면
DLL 기준 주소에서 `dec ebp; pop edx; nop; add [ebx],al; add [eax],al`
순서가 된다. 마지막 명령은 오프셋 `+0x5`에서 시작한다.
`pop edx`는 호출 복귀 주소를 소비하므로, 예외 때 스택 맨 위가
첫 인자 `00400000`인 모습 및 `EIP=BFE90005`와 맞는다.
**강한 추론:** Win98의 적재기가 이 빈 서수 칸을 DLL 시작 주소로
해결했고 Notepad++가 PE 헤더를 코드로 실행했다. 실행 중 IAT 칸의
실제 값을 따로 읽지는 않았으므로 이 부분은 직접 관측으로 표시하지
않는다. `LoadImage` fallback은 서수 호출 자체가 예외가 되면 도달할
수 없다.

나중 `0x005AAD44` 예외도 같은 함수의 벡터 반복문 안이지만,
이는 `LoadIconWithScaleDown` 호출 **직전**의 아이콘 ID 읽기다.
접근 주소는 `FFFFFFFF`였다. 현재 tracer는 `EBX`와 벡터의 세 포인터를
기록하지 않아 왜 잘못된 원소 주소가 생겼는지, 앞선 COMCTL32 예외와
인과관계가 있는지 판단할 수 없다. 두 번 보인 `0x00768902`는
`KERNEL32.RaiseException` 호출 직후, `0xE06D7363`을 인자로 전달하는
런타임 helper의 주소다. 이는 C++ 예외가 만들어진 위치이지 원래
불량 메모리 참조의 위치가 아니다.

다음 작은 진단은 Win98 안에서 IAT `0x007C90A8`의 실행 중 값을
읽고, 첫 `COMCTL32+0x5` 예외의 `EAX/EBX/EDX`와 선행 스택을
기록하는 것이다. 해결책 후보는 서수 381의 ABI 및 실패 의미를
가진 실제 아이콘 로더를 제공하는 것이지만, KernelEx의 서수
가져오기 라우팅 가능 여부와 좁은 리소스·크기 계약 시험을 먼저
확인해야 한다. 이 추적만으로 다른 API의 정상 동작이나 전체 앱
호환성을 주장할 수 없다.

Microsoft의 [디버거 예외 처리 설명](https://learn.microsoft.com/en-us/windows/win32/debug/debugger-exception-handling)과
[`ContinueDebugEvent` 계약](https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-continuedebugevent)은
첫 번째 예외를 `DBG_EXCEPTION_NOT_HANDLED`로 넘기면 애플리케이션의
예외 처리기 검색이 계속됨을 설명한다. 이 문서는 현재 Windows 문서이므로
Windows 98 구현은 위 직접 게스트 시험으로 별도 확인한다.
