# Windows 98 SE용 설치 안내

이 ZIP에는 실험용 `m98wrap.dll`과 앱 로컬용 `dbghelp.dll`, `dwmapi.dll`, `bcrypt.dll`이 들어 있습니다.
Windows 98 SE용 KernelEx API 확장 시험을 위한 파일이며 최신 프로그램의 실행을
보장하지 않습니다. 설치는 Windows 98 SE **32비트 게스트**에서 수동으로 합니다.

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
4. 저장한 뒤 게스트를 재부팅합니다.

## 4. 앱 로컬 DLL 사용하기 (선택)

필요한 앱에 한해 `dbghelp.dll`, `dwmapi.dll`, `bcrypt.dll`을 실행 파일과 **같은 폴더**에 복사합니다. 예를 들어 Notepad++를 시험한다면 `notepad++.exe` 옆에 둡니다. 같은 이름의 파일이 이미 있으면 먼저 백업합니다. `C:\WINDOWS\SYSTEM`이나 KernelEx 폴더에는 복사하지 마세요. `dbghelp.dll`은 Windows 98의 `IMAGEHLP.DLL`에 있는 `ImageNtHeader`를 연결하고, `dwmapi.dll`은 합성 데스크톱 기능이 없음을 오류 코드로 알려줍니다. `bcrypt.dll`은 SHA-256·MD5·HMAC 해시 기능 일부를 제공합니다. 네 래퍼를 제공해도 Notepad++ 8.9.8은 게스트에서 `SHELL32.DLL`의 `SHCreateItemFromParsingName` 누락으로 중단됐습니다.

## 되돌리기

1. `core.ini`의 `[DCFG1]` `contents=` 줄에서 추가한 `m98wrap` 항목만 제거합니다. 설치 뒤 다른 수정이 없었다면 백업해 둔 원본 `COREBAK.INI`를 `core.ini`로 복원해도 됩니다.
2. KernelEx 폴더에 복사한 `M98WRAP.DLL`을 제거하거나, 이전 DLL을 백업했다면 복원합니다.
3. 프로그램 폴더에 복사한 `DBGHELP.DLL`, `DWMAPI.DLL`, `BCRYPT.DLL`도 제거하거나 이전 파일을 복원합니다.
4. 게스트를 재부팅합니다. 설정 편집에 문제가 생기면 백업한 원본 `core.ini`를 복원합니다.

## 소스와 라이선스

프로젝트 코드는 GPL-2.0-only이며 Wine에서 유래한 Unicode 표와 관련 알고리즘의 고지는 LGPL-2.1-or-later로 유지됩니다. 정확한 출처는 `THIRD_PARTY.md`, 라이선스 본문은 `LICENSE` 및 `licenses/Wine-LGPL-2.1.txt`를 참조하세요. 수정과 재빌드에 필요한 소스, `build-dlls.ps1`, `BUILD-SOURCE.md`가 ZIP에 들어 있습니다.
