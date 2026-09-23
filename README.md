# Windows 98 Shizuku's Second Edition — Modernization Lab

한국어 Windows 98 SE를 직접 설치해 검증하는 **실험용** 프로젝트입니다. 새 DOS 기반 후보인 ShizukuDOS와 Windows 98용 KernelEx API 라이브러리(`m98wrap.dll`)를 개발하고, Wine·ReactOS의 일부 사용자 모드 구현을 Win9x 환경에 맞게 이식합니다. Windows 11 앱 실행은 이 단계의 목표나 검증 결과가 아닙니다.

## 현재 구현

- `KERNEL32.DLL`의 37개 API 이름을 KernelEx용 DLL에 제공: 문자열 비교, CPU 그룹/NUMA 조회, 패키지 식별 조회, 펌웨어/DEP 상태, 64비트 틱, 정밀 시각 호환 호출, 임계 구역·SRW 잠금·일회성 초기화, 파일 정보 조회 등.
- Wine의 Unicode 대문자 매핑표와 문자열 비교 알고리즘, ReactOS의 임계 구역 인수 검사 로직을 선별 이식했습니다. 함수별 출처와 라이선스는 [THIRD_PARTY.md](THIRD_PARTY.md)에 있습니다.
- 호스트에서 32비트 DLL과 Windows 98 호환 시험 프로그램을 빌드하고 실행할 수 있습니다. 앱 로컬 `dbghelp.dll`, `dwmapi.dll`, `bcrypt.dll` 브리지도 포함합니다.

일부 API는 **부분 구현**입니다. `GetFileInformationByHandleEx`는 Basic/Standard 정보만 제공하며 변경 시각·실제 할당 크기를 추정하고 삭제 대기 상태를 알 수 없습니다. `GetTickCount64`는 DLL 로드 후의 32비트 틱 롤오버를 추적하지만, 49.7일 이상 켜진 시스템에 처음 로드될 때 이전 롤오버 횟수는 알 수 없습니다. `GetSystemTimePreciseAsFileTime`은 Windows 98의 시계 정밀도만 제공합니다. 자세한 상태는 [호환성 현황](docs/COMPATIBILITY.md)에 있습니다.

## 빌드와 시험

Windows 호스트의 32비트 LLVM MinGW(`i686-w64-mingw32-gcc`)와 Python이 필요합니다. 최초 한 번 `python -m pip install -r tests/requirements.txt`로 PE 검증 의존성을 설치합니다.

```powershell
.\build.ps1
cd build
.\smoke.exe
```

빌드 결과는 `build/m98wrap.dll`, `build/smoke.exe`, `build/import_probe.exe`, `build/memprobe.exe`, `build/memstress.exe`, `build/cpuapp.exe`와 `build/app-profiles.ini`입니다. DLL과 CPU 런처의 import는 Windows 98에 존재하는 `KERNEL32.DLL` 함수로 제한했습니다. 빌드 중 [PE98 정적 검사](tests/check_pe98.py)가 DLL의 32비트 형식, 로더 버전, 의존 DLL, 가져오기 함수, KernelEx 진입점을 확인합니다. `smoke.exe`는 API 테이블과 주요 동작을 검사하고, `import_probe.exe`는 최신 API 두 개를 KERNEL32에서 **정적으로 import**해 KernelEx 경로를 검사합니다. 두 시험은 직접 설치한 Windows 98 SE 게스트에서 각각 PASS를 확인했습니다. `memstress.exe`는 여러 프로세스의 페이지 읽기·쓰기를 측정하지만 [물리 RAM 4GB 인증 시험](memory/README.md)은 아닙니다. Windows 98 게스트에서의 실제 결과는 [VM 기록](vm/README.md)을 참조하세요.

추가 앱 로컬 DLL `build/dbghelp.dll`, `build/dwmapi.dll`, `build/bcrypt.dll`은 해당 앱의 폴더에서만 시험합니다. Windows 98 게스트에서 BCRYPT와 DWMAPI 직접 호출 시험을 통과했습니다. Notepad++ 8.9.8은 다음 단계인 `SHELL32.DLL`의 `SHCreateItemFromParsingName` 누락에서 중단됐습니다.

다음 이식 대상을 찾을 때는 `python tools/scan_imports.py 앱.exe`로 PE import를 확인합니다. `unknown_or_native`는 Windows 98 기본 API일 수도 있으므로 미지원 판정이 아닙니다.

## KernelEx 연결

[KernelEx 4.5.2](https://sourceforge.net/projects/kernelex/files/KernelEx/4.5.2/)를 Windows 98 게스트에 설치하기 전에 [Microsoft Layer for Unicode 1.1.3790.0](https://legacyupdate.net/download-center/download/4237/platform-software-development-kit-redistributable-microsoft-layer-for-unicode-on-windows-95-98-and-me-systems-1.1.3790.0)을 설치해 `C:\WINDOWS\SYSTEM\UNICOWS.DLL`을 준비합니다. 그런 다음 KernelEx를 설치하고 `m98wrap.dll`을 KernelEx 설치 폴더에 복사합니다. 같은 폴더의 `core.ini`에서 기본 설정의 `contents=std,kexbases,kexbasen`을 `contents=std,kexbases,kexbasen,m98wrap`으로 바꿉니다. 반드시 원본 `core.ini`를 백업하고 재부팅합니다. 호스트에서 설치본 설정을 편집할 때는 `tools/patch-core-ini.ps1 -InputPath ... -OutputPath ...`를 쓸 수 있습니다. 이 도구는 기존 인코딩과 줄바꿈을 보존하며, [인코딩 회귀 시험](tests/test_patch_core_ini.py)으로 확인했습니다. [integration/core.ini](integration/core.ini)는 제공된 KernelEx 소스의 설정을 바꾼 예시입니다. 설치본 설정이 다르면 통째로 덮어쓰지 말고 해당 줄만 수정하세요.

## 다음 범위

- 실제 앱의 PE import와 실행 오류를 기준으로 API를 하나씩 추가하고 게스트에서 다시 시험합니다.
- 새로 작성하는 [ShizukuDOS](shizukudos/)는 독립 부팅, FAT12 파일 읽기, 제한된 `.COM` 실행과 기본 DOS 인터럽트, 원래 하드디스크 부트 경로로의 체인로드를 가속 VM에서 검증했습니다. Windows 98의 `IO.SYS`를 대체해 GUI까지 부팅하는 기능은 아직 구현되지 않았습니다.
- [앱별 CPU 호환성 설계](cpu/)는 기종 프로필·시작 경로를 먼저 정의합니다. 명령어셋과 실행 속도를 앱별로 강제하려면 실제 명령어 중재/변환 계층이 추가로 필요합니다. Windows 98 GUI의 멀티코어 사용은 DOS만 바꿔서는 달성되지 않습니다.
- 새 완료 목표는 PAE를 통해 CSM 부팅 가능한 최신 시스템의 사용 가능한 물리 RAM 전체를 다루는 것입니다. 이는 처음 요청한 4 GiB를 포함하며, 32비트 주소 공간의 PCI/MMIO 구멍 위쪽 페이지까지 고려해야 합니다. 현재 Windows 98 메모리 관리자는 개조되지 않았고, `MaxPhysPage`로 인식량을 제한하는 설정은 이 목표를 달성하지 않습니다.
- Skylake급 CSM 환경의 AHCI/xHCI는 별도 Win9x 드라이버가 필요합니다. 현재 [drivers/](drivers/)에는 구현 계획과 안전한 레지스터 해석 코드가 있으며, 실제 드라이버는 아직 아닙니다. 시험 가능한 실기기 모델은 아직 제공되지 않아 하드웨어 지원 범위를 검증하지 못했습니다.

Microsoft ISO와 설치 키는 저장소에 포함하지 않습니다.

## 프로젝트 스킬

[win98-modern-lab](skills/win98-modern-lab/SKILL.md) 스킬은 API 이식, 실제 게스트 시험, ShizukuDOS·CPU·PAE·드라이버, 라이선스와 공개 절차를 하나의 재사용 가능한 작업 지침으로 담았습니다. 저장소의 `skills/win98-modern-lab` 폴더를 Codex 개인 스킬 폴더로 복사해 다른 작업에서도 사용할 수 있습니다. 이 PC에는 개인 스킬로도 설치했으며 두 사본 모두 형식 검사를 통과했습니다. 스킬 작성 자체는 기능 구현이나 앱 구동 증거가 아닙니다.

## 장기 호환성 목표

현재 완료 목표는 **Windows API 전체 표면의 100% 호환성**과 Chromium 150, Supermium, VLC, Notepad++, VS Code 필수 앱의 실제 구동입니다. 측정 기준과 앱별 검증 범위는 [대상 앱과 API 기준](docs/TARGET_APPS.md)에 기록합니다. 최신 DLL의 37개 래퍼는 Windows 98 게스트 직접 호출 시험을 통과했습니다. 설치된 KernelEx의 정적 KERNEL32 import 경로는 이전 29개 빌드에서만 확인됐습니다. [고정 SDK 10.0.28000.2705 목록](docs/SDK_INVENTORY.md)은 후보 레코드 151,330건의 재현 가능한 집계이며, 중복·데스크톱 적용 범위를 판정하기 전이므로 최종 API 분모나 호환성 점수가 아닙니다. [PE 가져오기 측정 도구](docs/PE_IMPORT_COVERAGE.md)는 앱별 누락 API를 찾는 보조 지표입니다.
