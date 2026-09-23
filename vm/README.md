# Windows 98 SE 가속 시험 VM

## 재현 경로

이 호스트의 기본 실행 VM은 `Win98Modern-Accel-128`입니다. `setup-accelerated.ps1 -InstallationMedia <Windows 98 SE 설치 ISO 경로>`로 BIOS/IDE, 1 vCPU, RAM 128 MiB, PAE off, nested paging off, 네트워크 없음의 VirtualBox VM을 만들고 `run.ps1`로 시작합니다. 두 스크립트는 이미 등록된 VM이나 디스크를 덮어쓰지 않습니다. 설치 키는 매체의 OEM/소매 채널에 맞아야 하며 저장소에 기록하지 않습니다.

이 실험에서 쓴 설치 매체는 [Archive.org의 한국어 Windows 98 SE OEM 이미지](https://archive.org/details/X03-77968)이며, ISO SHA-256은 `4C1B148BFD8AA9FFA702D6756FFA32064C49BB78C00D074225DB5A0F302C0873`입니다. ISO는 Git에서 제외됩니다. 설치 프로그램은 실제 VM에서 실행되어 파일 복사, 제품 번호 승인, 하드웨어 설정과 재부팅을 거쳤습니다.

## 현대 CPU에서 설치 복구

첫 번째 256 MiB 설치 VM `Win98Modern-Accel`은 하드웨어 설정 중 `Msgsrv32`가 `MSVCRT.DLL`에서 오류를 내고 `VWIN32(05)+00002059` 치명 오류로 멈췄습니다. 재부팅 뒤에는 `Explorer`가 `EXPLORER.EXE` 주소 `0040A067`에서 오류를 냈고, `SHELL32.DLL`이 `SHLWAPI.DLL`의 내보내기를 찾지 못한다는 대화상자도 나왔습니다. RAM 128 MiB, PAE off, nested paging off로 바꾼 독립 복제 VM에서도 Explorer 오류가 재현됐습니다. 이 대화상자만으로 실제 DLL 파일 손상을 입증할 수는 없습니다.

[Patcher9x v0.9.91 공식 릴리스](https://github.com/JHRobotics/patcher9x/releases/tag/v0.9.91)의 `patcher9x-0.9.91-boot.ima`를 복제 VM의 플로피에 연결했습니다. 다운로드한 IMA의 SHA-256은 `CA4FDDEB6AD44F63F42F50C668B5E9E1587F9686B21D5A68C3BEAC1F5781ED03`입니다. 이 플로피는 복구 도구일 뿐이며 Git에 포함하지 않습니다. 공식 [사용 설명](https://github.com/JHRobotics/patcher9x#operation-modes)은 Windows 98 SE의 현대 CPU TLB 문제와 `-select tlb` 선택을 설명합니다.

전원을 끈 시험 VM에 플로피를 연결하고 플로피를 첫 부팅 장치로 지정한 뒤, `A:\>`에서 다음을 실행합니다.

```text
patch9x -select tlb
```

기본 경로 `C:\Windows\System`, 직접 파일 수정 방식 `2`를 선택하고 수정 확인에 `Y`를 입력했습니다. 도구는 `VMM32.VXD` 파일 1개를 수정한 뒤 `Patch applied successfully!`를 표시했습니다. 메모리·CPU 속도·4G resource 패치는 이 복구 시험에 섞지 않았습니다. 플로피를 제거하고 디스크로 부팅한 뒤 Windows 98 SE가 설치 마무리를 끝내고 바탕화면에 도달했습니다. `Win98Modern-Base`는 그 상태를 정상 종료한 후 복제한 보존용 VM입니다. 시험은 `Win98Modern-Accel-128`에서만 진행합니다.

## 가속과 증거

VirtualBox 7.2.18은 이 호스트에서 Windows Hypervisor Platform의 NEM 백엔드를 사용합니다. 패치 후 바탕화면에 도달한 실행의 `VBox.log`에는 `NEM: WHvCapabilityCodeHypervisorPresent is TRUE`와 `NEM: Created partition`이 있고 VM 상태는 `running`이었습니다. 로그의 `Snail execution mode`도 기록되어 있어 성능은 낮을 수 있습니다. `run.ps1`은 이미 실행 중인 VM에서도 **현재 로그**의 파티션 생성과 실행 상태를 확인합니다.

무시된 로컬 증거 파일: `accelerated-desktop-first.png`, `accelerated-desktop.png`, `accel-after-tlb-time.png`, `accel-after-tlb-restart.png`, `patcher-confirm.png`, `patcher-success.png`, `accel-crash.png`, `accel-shell32-error.png`. 제품 번호 화면 캡처는 공개하거나 추적하지 않습니다.

`run-tcg-baseline.ps1`은 QEMU 소프트웨어 에뮬레이션 비교 경로입니다. TCG는 사용자 요구의 VT-x/AMD-V 가속을 충족하지 않습니다. QEMU VM과 VirtualBox VM은 각각 독립된 디스크를 사용합니다.

## 게스트 시험 매체

최신 빌드 산출물을 `share` 폴더에 넣고 프로젝트 루트에서 `python tools/make_test_iso.py vm/share vm/test-media.iso`로 Joliet CD를 만듭니다. 필요한 Python 패키지는 `tools/requirements-test-media.txt`에 있습니다. 전원을 끈 시험 VM의 IDE 광학 드라이브에 시험 CD를 연결합니다. `app-profiles.ini` 같은 긴 파일명은 Joliet로 보존됩니다. 보존용 `Win98Modern-Base`에는 추가 프로그램을 설치하지 않습니다.

## KernelEx와 래퍼 시험

2026-09-24 현재 시험 VM은 수정한 UXTHEME KnownDLL과 KERNEL32 API 라이브러리, ADVAPI32 API 라이브러리를 적용한 뒤 **정상 종료와 콜드 부팅으로 GUI에 도달**했습니다. `GetTimeFormatEx`, `GetProductInfo`, `GetDateFormatEx`, `GetFinalPathNameByHandleW`, `FindFirstStreamW`, `GetLocaleInfoEx`, 앱 재시작 관련 API 3개, `RegGetValueW`의 게스트 정적 import 시험과 UXTHEME `GetThemeSysFont`의 여섯 시스템 글꼴 시험이 통과했습니다. 현재 `M98WRP9.DLL`의 SHA-256은 `d16c988a84071f93e3a75c2602252e425917160c2f20458aaea9ffd203da09b4`이고, 재시작 관련 API 직접 호출 및 46개 항목 표본 API 시험이 종료 코드 0으로 통과했습니다. 정상 종료와 재기동에 사용한 이번 VM 로그에서 WHPX/NEM 하드웨어 가속 백엔드를 확인했습니다. 앞선 버전의 `GetLocaleInfoEx`와 FAT32 파일 식별값·스트림 오류 87 직접 시험도 통과했습니다. Notepad++의 다음 확인된 로더 오류는 `KERNEL32.DLL!QueryFullProcessImageNameW`입니다 (`npp-after-restart-apis.png`). 이전에 두 변경을 함께 적용하고 따뜻한 재시작을 했을 때 VxD 예외와 VirtualBox triple fault가 한 번 발생했고 `vxd-after-product-theme-20260924` 스냅샷에 보존했습니다. 앞선 WRP4/UXTNEW2/M98ADV 구성에서는 정상 재시작 한 번이 GUI 복귀와 COM1 원격 응답까지 통과했습니다. 반복 재시작 안정성은 별도 검증이 필요합니다.

[KernelEx 4.5.2 공식 설치기](https://sourceforge.net/projects/kernelex/files/KernelEx/4.5.2/)의 SHA-256은 `B4D4E6475ECF5E3099C0807BA85340A07DABDF9AC0D77B9F03FA5C37312C321B`입니다. 처음 실행할 때 설치기는 Microsoft Layer for Unicode가 필요하다고 중단됐습니다. [Legacy Update의 Microsoft 배포 파일 기록](https://legacyupdate.net/download-center/download/4237/platform-software-development-kit-redistributable-microsoft-layer-for-unicode-on-windows-95-98-and-me-systems-1.1.3790.0)을 따라 [Internet Archive에 보존된 원래 Microsoft `unicows.exe`](https://web.archive.org/web/20041210000000id_/https%3A//download.microsoft.com/download/b/7/5/b75eace3-00e2-4aa0-9a6f-0b6882c71642/unicows.exe)를 받았습니다. SHA-1은 배포 기록과 일치하는 `6BBBE7E68B3630C5E6C73BDF6B9D7806EF079104`, SHA-256은 `96D46C673E265B0C11EC965C2C4E712EB0E2E3F740C688C2BF206AAD393ADE33`입니다. 별도 CD에서 게스트에 실행해 `C:\WINDOWS\SYSTEM\UNICOWS.DLL` 258,352바이트를 확인했습니다. 이 설치 파일과 CD는 Git에서 제외합니다.

시험 VM에서 KernelEx 설치가 완료됐고 재부팅 뒤 정보 대화상자에 `v4.5.120`, 활성 상태가 표시됐습니다. 설치 경로는 `C:\WINDOWS\KernelEx`입니다. 원본 `core.ini`를 `COREBAK.INI`로 백업한 뒤 `[DCFG1] contents`에만 `m98wrap`을 추가한 시험용 `core.ini`를 적용하고 같은 폴더에 `M98WRAP.DLL`을 복사했습니다. 게스트의 `fc` 비교에서 설치한 DLL과 설정 파일은 시험 CD의 사본과 일치했습니다. 보존용 Base VM은 수정하지 않았습니다.

가속 시험 VM에서 확인한 결과:

| 시험 | 게스트에서 관찰한 결과 |
| --- | --- |
| `C:\WINDOWS\KernelEx\SMOKE.EXE` | `PASS: 29 Win98 KernelEx API wrappers` (`smoke-result.png`) |
| `D:\NPP\SMOKE.EXE` with current app-local `M98WRAP.DLL` | `PASS: 37 Win98 KernelEx API wrappers` (`smoke-37-result.png`). This directly loads the new DLL; it does not prove the installed KernelEx `core.ini` resolves all 37 static imports. |
| `D:\IMPROBE.EXE` | `PASS: static KERNEL32 imports via KernelEx` (`import-probe-result.png`) |
| `D:\MEMPROBE.EXE` | 128 MiB VM의 `GlobalMemoryStatus.total_phys=0x07F85000` (`memprobe-128-result.png`) |
| `D:\MEMSTRESS.EXE 8 1` | 요청한 8 MiB, 2,048페이지 검증, `result_status=ALL_REQUESTED_PAGES_VERIFIED` (`memstress-128-result.png`) |
| `D:\CPUAPP.EXE C:\WINDOWS\NOTEPAD.EXE` | 메모장 실행, `modern` 프로필 선택, 자식 종료 코드 0 (`cpuapp-result.png`, `cpuapp-finished.png`) |

`IMPROBE.EXE`는 최신 API를 KERNEL32에서 정적으로 가져오므로 KernelEx 해석 경로까지 확인합니다. CPUAPP 결과는 프로필 선택과 자식 실행의 확인이며 CPUID·ISA·속도 변경을 입증하지 않습니다. 128 MiB 메모리 시험은 **4 GiB 동작 검증이 아닙니다**. 4 GiB 부팅이나 물리 페이지 검증은 아직 수행하지 않았습니다.

Notepad++ 8.9.8 x86 포터블 앱의 단계별 실행 시험에서는 `DBGHELP.DLL`, `DWMAPI.DLL` 누락에 이어 DWM 재배치 정보가 없는 빌드의 로더 오류를 확인했습니다. 재배치 정보를 추가한 DWMAPI와 새 BCRYPT는 Windows 98 게스트에서 각각 직접 호출 시험을 통과했습니다 (`dwm-reloc-guest-smoke.png`, `bcrypt-guest-smoke.png`). BCRYPT의 MD5/SHA256 및 HMAC pseudo-handle 시험도 게스트에서 통과했습니다 (`pseudo-guest-smoke.png`). 이후 Shell API 라이브러리의 세 함수에 대해 직접 호출과 정적 import 경로를 검증했고, Explorer가 선택 대상 파일을 표시하는 것도 확인했습니다 (`select-shell3-guest.png`). 이 단계에서 Notepad++ 로더는 `UXTHEME.DLL!DrawThemeTextEx` 누락까지 진행했습니다. 테마 브리지는 게스트 직접 호출 시험을 통과했지만 KernelEx의 UXTHEME KnownDLL 연결 때문에 앱 로컬 사본은 선택되지 않았습니다 (`npp-after-uxtheme.png`). 앱이 실행됐다는 증거는 없으며, 시험용 앱 파일과 ISO는 Git에서 제외합니다. 상세 내용은 `docs/TARGET_APPS.md`에 있습니다.

원격 시험은 가상 COM1을 16450 UART로 둔 이 시험 VM에서 이루어졌습니다. 16550A로 설정한 뒤 COM1 드라이버를 설치했을 때의 첫 재부팅은 VxD 예외로 멈췄고, UART를 끈 복구 뒤 16450에서 정상 GUI로 돌아왔습니다. 현재 부팅의 `VBox.log`에는 하드웨어 가속 백엔드의 `NEM: Created partition`이 있습니다. 원격 도구의 전송 묶음 3/3, KERNEL32·BCRYPT·DBGHELP·DWMAPI·SHELL32 직접 호출 묶음 5/5가 통과했으며, 새 `STOP` 명령은 게스트 콘솔을 정상 종료했습니다. 증거 JSON은 무시된 `build/remote/` 폴더에 보관합니다. 이 결과는 4 GiB, PAE, 멀티코어, AHCI/xHCI나 앱 구동을 입증하지 않습니다.

Windows 종료 대화상자의 `시스템 다시 시작(R)`으로 재부팅해 자동 GUI 바탕화면 복귀를 확인했습니다. 시작 과정에서 잠시 `C:\>` 프롬프트가 보였으나 그 뒤 자동으로 GUI가 열렸습니다. `C:\MSDOS.SYS`의 `BootGUI=1`을 확인했고 `C:\WINBOOT.INI`는 없습니다 (`auto-gui-after-restart.png`, `winboot-check.png`). 호스트 절전 때 VirtualBox 로그에 `HostSuspend`와 `HostResume`이 기록됐으며 VM은 이후 다시 실행 상태였습니다.

화면 표시는 아직 640×480, `표준 PCI 그래픽 어댑터(VGA)`, 16색입니다. 색 선택 목록에는 16색만 있어 추가 드라이버 없이 256색으로 바꿀 수 없었습니다 (`display-settings-final2.png`, `display-color-options.png`). 첫 바탕화면의 Welcome 창에는 팔레트 깨짐이 있었지만 아이콘과 작업 표시줄, 시험 프로그램 실행에는 영향을 주지 않았습니다. 디스플레이 드라이버 교체는 아직 시험하지 않았습니다.
