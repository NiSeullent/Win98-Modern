# Windows 98 SE용 설치 안내

이 ZIP에는 실험용 KernelEx API 라이브러리 `m98wrap.dll`, `m98shell.dll`, `m98adv.dll`, KernelEx용 결합 테마 후보 `UXTHEME.DLL`, 안전 범위를 제한한 전환 도구 `KSWITCH.EXE`, 앱 로컬용 `dbghelp.dll`, `dwmapi.dll`, `bcrypt.dll`이 들어 있습니다.
Windows 98 SE용 KernelEx API 확장 시험을 위한 파일이며 최신 프로그램의 실행을
보장하지 않습니다. 설치는 Windows 98 SE **32비트 게스트**에서 수동으로 합니다.
이 미리보기의 `m98wrap.dll`에는 KERNEL32 이름 89개가 등록돼 있습니다. 이는
등록된 함수 이름의 수이며 전체 Windows API 호환률이나 앱 구동률이 아닙니다.
그중 `GetFinalPathNameByHandleW`, `FindFirstStreamW`, `GetLocaleInfoEx`,
`GetApplicationRestartSettings`, `QueryFullProcessImageNameA/W`의
지원 범위와 시험 결과는 각각 `docs/FINALPATH_PORT.md`,
`docs/FIRSTSTREAM_PORT.md`, `docs/LOCALEINFO_PORT.md`,
`docs/NPP_RESTART_PORT.md`, `docs/NPP_PROCESSPATH_PORT.md`에 있습니다.
`CompareStringEx`·`LCMapStringEx`와 스레드풀 작업 함수 다섯 개의 제한 및
시험 단계는 `docs/NLS_EX_PORT.md`와 `docs/THREADPOOL_WORK_PORT.md`에 있습니다.
일회 초기화 기능은 `InitOnceInitialize`, `InitOnceBeginInitialize`,
`InitOnceComplete`, `InitOnceExecuteOnce` 네 함수가 하나의 상태를 공유합니다.
동시 초기화, 실패 후 재시도와 context 처리의 지원 범위는
`docs/INITONCE_PORT.md`에 있습니다. 잘못된 ExecuteOnce 사용의 예외 처리에는
차이가 있습니다. 비동기 초기화 중인 객체나 정렬되지 않은 callback context를
전달하면 이 미리보기는 실패를 반환하지만, 원본 최신 Windows는 해당 독립
시험을 `0xC00000F0` 또는 `0xC00000F1`로 종료했습니다.
이 이름들의 추가만으로 앱 시작이나 전체 Windows API 호환성이 보장되지는 않습니다.

## 1. 호스트에서 내려받고 확인하기

1. 릴리스의 ZIP과 같은 이름의 `.zip.sha256` 파일을 함께 받습니다.
2. 최신 Windows 호스트의 PowerShell에서 `Get-FileHash .\win98-modern-버전-x86.zip -Algorithm SHA256`을 실행합니다. 출력된 해시를 `.zip.sha256` 첫 열과 비교합니다.
3. ZIP을 풀어 이 문서와 DLL을 읽습니다. ZIP 안의 `SHA256SUMS.txt`에는 각 파일의 SHA-256이 있습니다. 파일을 게스트로 옮긴 뒤 해당 DLL의 해시도 비교할 수 있습니다.

## 2. 필수 구성 요소 설치하기

1. 사용자가 보유한 정식 Windows 98 SE를 게스트에 설치합니다.
2. [Microsoft Layer for Unicode 1.1.3790.0](https://legacyupdate.net/download-center/download/4237/platform-software-development-kit-redistributable-microsoft-layer-for-unicode-on-windows-95-98-and-me-systems-1.1.3790.0)을 별도로 받아 먼저 설치합니다. 설치 후 `C:\WINDOWS\SYSTEM\UNICOWS.DLL`이 있는지 확인합니다.
3. [KernelEx 4.5.2](https://sourceforge.net/projects/kernelex/files/KernelEx/4.5.2/)를 별도로 받아 설치하고 재부팅합니다. 시험한 설치 위치는 `C:\WINDOWS\KernelEx`입니다. 실제 게스트의 설치 폴더를 기준으로 진행합니다.

위 설치 파일, Windows 설치 매체와 제품 키는 이 ZIP에 없습니다.

## 3. `m98wrap.dll` 연결하기

1. KernelEx 폴더의 원본 `core.ini`를 같은 폴더에 `COREBAK.INI`로 **먼저 복사**합니다. 이 이름이 이미 있으면 덮어쓰지 말고 새 백업 이름을 정해 기록합니다. 이전 `M98WRAP.DLL`도 있으면 별도로 백업합니다.
2. 이 ZIP의 `m98wrap.dll`을 KernelEx 폴더에 복사합니다.
3. **설치된** `core.ini`를 열고 `[DCFG1]` 섹션의 `contents=` 한 줄 끝에 `,m98wrap`을 한 번만 추가합니다. 기본 설정은 다음과 같습니다.

   ```ini
   contents=std,kexbases,kexbasen,m98wrap
   ```

   다른 라이브러리 이름이 이미 있으면 그대로 보존하고 `m98wrap`만 추가합니다. 제공된 KernelEx 소스의 `core.ini` 전체로 설치본을 덮어쓰지 마세요.
4. `[DCFG1.names.98]`, `[DCFG1.names.Me]`, `[WINXP.names]` 세 섹션에
   `porting/runtime-routes.json`의 여섯 기능 묶음(38개 이름)을 모두 연결합니다.
   일부 기본 함수만 이전 공급자에 남으면 FLS 종료 콜백이 누락될 수 있습니다.
   호스트로 복사한 설정 파일에는 ZIP의 도구로 114개 연결을 함께 적용할 수 있습니다.
   3단계의 `contents=` 등록을 먼저 반영한 파일을 입력으로 사용하세요.

   ```powershell
   python remote/patch_core_family.py CORE-BEFORE.INI CORE-READY.INI --library m98wrap --family all
   ```

   출력은 아직 존재하지 않는 새 파일이어야 합니다. 도구는 기존의 관련 없는
   설정·줄바꿈을 보존하고 충돌·중복 연결을 거부합니다. 결과를 검토한 뒤
   백업한 게스트 설정을 `CORE-READY.INI` 내용으로 교체합니다.
   `integration/core.ini`는 연결 예시이며 설치본 전체를 덮어쓰는 파일이 아닙니다.

   버전 이름을 쓰는 이전 공급자에서 업그레이드할 때는 `contents=`에 공급자를
   중복 등록하지 말고, 먼저 별도 출력 파일로 기존 연결 전체를 이동합니다.
   다음은 이전 이름이 `m98wrp18`인 경우의 예시입니다. 실제 설치 이름으로 바꿉니다.

   ```powershell
   python remote/patch_core_wrapper_upgrade.py CORE-BEFORE.INI CORE-MIGRATED.INI --old m98wrp18 --new m98wrap
   python remote/patch_core_family.py CORE-MIGRATED.INI CORE-READY.INI --library m98wrap --family all
   ```

5. 저장한 뒤 게스트를 **정상 종료하고 완전히 껐다가 다시 켭니다**.
   새 DLL의 함수 이름 89개 등록만으로 각 함수의 동작이 검증되는 것은
   아닙니다. 설치한 KernelEx를 통한 정적 import와 실제 앱 동작을 각각
   확인하세요.

`guest-tests` 폴더에는 설치한 KernelEx 연결을 확인할 정적 import 시험
`initonce_import_probe.exe`와 `threadpool_guest_import_smoke.exe`가 있습니다.
스레드풀 시험에는 같은 폴더의 `TPMARK.DLL`이 필요하므로 세 파일을 함께 옮기고
그 폴더에서 실행하세요. 이 스레드풀 시험은 미지원 custom environment의 실패도
검사하므로 최신 Windows 호스트가 아닌 설치된 Win98 provider용입니다.
각 시험의 실제 게스트 결과와 DLL 식별 정보는 해당 기능 문서에서 확인하세요.
시험 파일의 포함이나 호스트 통과 자체를 게스트 검증 완료로 해석하면 안 됩니다.

## 4. Shell API 라이브러리 연결하기 (선택)

1. `m98shell.dll`을 KernelEx 폴더에 복사합니다. 기존 파일이 있거나 사용 중이면 먼저 백업한 뒤 게스트를 정상 종료한 상태에서 교체합니다.
2. **설치된** `core.ini`의 `[DCFG1]` `contents=` 항목 끝에 `,m98shell`을 한 번 추가합니다. `m98wrap` 등 기존 항목은 보존합니다.
3. `[DCFG1.names.98]`, `[DCFG1.names.Me]`, `[WINXP.names]` 각각에 다음 두 줄을 넣거나 해당 함수의 기존 연결을 수정합니다. 같은 이름의 설정이 중복되지 않게 확인합니다.

   ```ini
   SHELL32.SHOpenFolderAndSelectItems=m98shell.0
   SHELL32.SHParseDisplayName=m98shell.0
   ```

4. 기존 설정을 백업하고 재부팅합니다. 이 라이브러리의 세 Shell 함수는 시험 VM에서 직접 호출과 정적 import 시험을 통과했으나 전체 Shell API를 대신하지는 않습니다.

## 5. ADVAPI 레지스트리 API 연결하기 (선택)

1. `m98adv.dll`을 KernelEx 폴더에 복사합니다. 기존 파일이 있다면 백업합니다. 이 DOS 8.3 파일명은 시험 게스트에서 사용한 `m98adv` 설정명에 맞춥니다.
2. **설치된** `core.ini`의 `[DCFG1]` `contents=` 끝에 `,m98adv`를 한 번 추가합니다. 앞의 `m98wrap`, `m98shell` 등은 보존합니다.
3. `[DCFG1.names.98]`, `[DCFG1.names.Me]`, `[WINXP.names]` 각각에 다음 연결 두 줄을 넣거나 기존 연결을 수정합니다. 같은 함수 설정이 중복되지 않게 확인합니다.

   ```ini
   ADVAPI32.RegGetValueA=m98adv.0
   ADVAPI32.RegGetValueW=m98adv.0
   ```

4. 변경한 `core.ini`를 백업하고 **정상 종료 후 콜드 부팅**합니다. `RegGetValueA/W`의 직접 호출과 정적 ADVAPI32 import 시험은 Windows 98 SE 시험 게스트에서 통과했습니다. 전체 ADVAPI32 호환성이나 앱 실행 성공을 뜻하지는 않습니다.

## 6. KernelEx 테마 KnownDLL 후보 시험하기 (선택, 실험용)

`UXTHEME.DLL`은 원본 KernelEx 함수 이름 48개를 보존하고 여섯 함수를 더한 결합 후보입니다. KernelEx는 UXTHEME import를 자체 KnownDLL로 연결하므로, 프로그램 폴더에 같은 이름의 DLL을 복사하는 방식으로는 이 후보가 선택되지 않았습니다. 원본 파일을 덮어쓰지 않고 별도 파일명으로 시험합니다.

1. 게스트 VM 스냅샷과 KernelEx 폴더의 원본 `UXTHEME.DLL` 백업을 먼저 만듭니다. 레지스트리 `HKEY_LOCAL_MACHINE\Software\KernelEx\KnownDLLs`의 문자열 값 `UXTHEME`이 `UXTHEME.DLL`인지 기록합니다.
2. ZIP의 `UXTHEME.DLL`을 KernelEx 폴더에 **`UXTNEW.DLL` 이름으로** 복사합니다. 기존 `UXTNEW.DLL`이 있으면 중단하고 백업·식별한 뒤 진행합니다. 원본 `UXTHEME.DLL`은 유지합니다.
3. `KSWITCH.EXE`를 임시 폴더에 복사하여 DOS 명령 프롬프트에서 `KSWITCH.EXE inspect`를 실행합니다. `VALUE=UXTHEME.DLL`일 때만 `KSWITCH.EXE switch`를 실행합니다. 이 도구는 지정된 두 값만 허용하고 변경 직후 읽어 확인합니다. 예상 값이 아니거나 오류가 나면 다른 값으로 강제 편집하지 않습니다.
4. Windows 98을 정상 종료한 뒤 VM을 **완전히 껐다가** 켭니다. 따뜻한 재시작에서 예외가 한 차례 관찰되어 이 절차는 콜드 부팅으로 검증했습니다.

시험 게스트에서 결합 후보가 `DrawThemeTextEx` 정적 import를 통과하고, 테마 글꼴 여섯 사례의 정적 import 시험도 통과했습니다. Notepad++ 8.9.8은 이후에도 다른 KERNEL32 import 누락으로 중단됐습니다. 이 DLL은 Windows XP/Vista 테마 서비스를 구현하지 않으며 앱 전체 구동을 보장하지 않습니다.

## 7. 앱 로컬 DLL 사용하기 (선택)

필요한 앱에 한해 `dbghelp.dll`, `dwmapi.dll`, `bcrypt.dll`을 실행 파일과 **같은 폴더**에 복사합니다. 예를 들어 Notepad++를 시험한다면 `notepad++.exe` 옆에 둡니다. 같은 이름의 파일이 이미 있으면 먼저 백업합니다. `C:\WINDOWS\SYSTEM`이나 KernelEx 폴더에는 복사하지 마세요. `dbghelp.dll`은 Windows 98의 `IMAGEHLP.DLL`에 있는 `ImageNtHeader`를 연결하고, `dwmapi.dll`은 합성 데스크톱 기능이 없음을 오류 코드로 알려줍니다. `bcrypt.dll`은 SHA-256·MD5·HMAC 해시 기능 일부를 제공합니다.

## 되돌리기

1. KnownDLL 후보를 전환했다면 `KSWITCH.EXE inspect`로 `VALUE=UXTNEW.DLL`을 확인한 뒤 `KSWITCH.EXE restore`를 실행합니다. `VALUE=UXTHEME.DLL`이 다시 표시되는지 확인하고 **정상 종료 후 콜드 부팅**합니다. 그 뒤에만 후보 `UXTNEW.DLL`을 제거합니다. 값이 예상과 다르면 기록한 스냅샷과 백업을 사용합니다.
2. `core.ini`의 `[DCFG1]` `contents=` 줄에서 추가한 `m98wrap`, `m98shell`, `m98adv` 항목과 3·4·5단계의 로캘·Shell·ADVAPI 연결 줄만 제거합니다. 설치 뒤 다른 수정이 없었다면 백업해 둔 원본 `COREBAK.INI`를 `core.ini`로 복원해도 됩니다.
3. KernelEx 폴더에 복사한 `M98WRAP.DLL`, `M98SHELL.DLL`, `M98ADV.DLL`을 제거하거나 이전 DLL을 백업했다면 복원합니다.
4. 프로그램 폴더에 복사한 `DBGHELP.DLL`, `DWMAPI.DLL`, `BCRYPT.DLL`도 제거하거나 이전 파일을 복원합니다.
5. 게스트를 콜드 부팅합니다. 설정 편집에 문제가 생기면 백업한 원본 `core.ini`를 복원합니다.

## 소스와 라이선스

프로젝트 코드는 GPL-2.0-only이며 Wine에서 유래한 Unicode 표와 관련 알고리즘의 고지는 LGPL-2.1-or-later로 유지됩니다. 결합 UXTHEME에는 GPL-2.0-only KernelEx 코드와 LGPL-2.1-or-later `metric.c`가 포함됩니다. 정확한 출처와 원본 파일 해시는 `THIRD_PARTY.md` 및 `KERNELEX-UXTHEME-NOTICES.md`, 라이선스 본문은 `LICENSE` 및 `licenses/Wine-LGPL-2.1.txt`를 참조하세요. 수정과 재빌드에 필요한 소스, `build-dlls.ps1`, `BUILD-SOURCE.md`가 ZIP에 들어 있습니다.

## 이번 API 묶음 검증

M98WRP19 기준으로 SList 7개, 스레드풀 work/callback 12개, InitOnce 4개, NLS 2개, FLS·스레드·파이버 13개의 제한된 계약 시험이 실제 설치된 Win98에서 정적 import로 통과했습니다. KERNEL32 표의 89개 등록 이름 전체가 완전 호환이라는 뜻은 아닙니다. 통합 회귀 시험 10개가 통과했으며 시스템 MSVCRT의 스레드 종료 콜백도 확인했습니다. Notepad++ 8.9.8은 현재 USER32.RemoveClipboardFormatListener 누락에서 멈춥니다.

FLS/lifecycle 13개는 공통 백엔드에 함께 연결해야 합니다. 예제 `integration/core.ini`와 `porting/runtime-routes.json`의 해당 기능 묶음을 기존 설정에 병합하고 정상 종료 후 다시 부팅합니다. `CreateThread`와 종료 함수 일부만 이전 공급자에 남기면 콜백 처리가 누락될 수 있습니다. 역방향 파이버 변환, raw/강제 종료, 일부 플래그와 내부 스레드풀 종료 정리는 아직 미완료입니다. 정리 콜백이 일시 중단된 파이버를 삭제하려는 요청은 ERROR_BUSY로 거부합니다.
