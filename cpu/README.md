# 응용 프로그램별 CPU 프로필

**현재 상태:** 프로필 선택 파일, Windows 98용 런처 소스, 호스트 WHPX 사전 점검만 있다. 런처는 선택한 프로필을 자식 프로세스의 환경 변수로 전달한다. **CPUID, 명령어 집합, 실행 속도는 아직 변경하지 않는다.** Windows 98 GUI의 멀티코어 실행도 구현되지 않았다.

| 파일 | 용도 |
| --- | --- |
| [app-profiles.ini](app-profiles.ini) | 응용 프로그램 실행 파일명 → i386/i486/Pentium MMX/Pentium II/Pentium III/Modern 선언 |
| [cpuapp.c](cpuapp.c) | Win98에서 해당 선언을 읽고 자식 프로그램을 실행하는 CRT 없는 C89 런처 소스 |
| [Check-VT.ps1](Check-VT.ps1) | **QEMU 사용 시에만** 해당 빌드가 WHPX를 포함하는지 검사. 없으면 실패하며 TCG로 우회하지 않음 |
| [ARCHITECTURE.md](ARCHITECTURE.md) | 실제 응용 프로그램별 ISA/CPUID/속도 적용과 Win98 SMP에 필요한 작업·시험 |

`CPUAPP.EXE "C:\WINDOWS\NOTEPAD.EXE"` 형태로 사용할 런처를 설계했다. 루트 `build.ps1`이 `build/cpuapp.exe`를 만들고 `app-profiles.ini`를 같은 폴더에 복사한다. **게스트에서 실행·검증된 `CPUAPP.EXE`는 아직 없다.** 파일명만으로 매핑하므로 이름이 같은 프로그램은 한 프로필을 공유한다.

Windows 98 자체의 설치 기준은 최소 486DX/66MHz·수치 보조 프로세서이므로, i386 프로필은 **Windows 98 VM 전체의 CPU를 386으로 바꾸는 설정이 아니라 응용 프로그램 대상 정책 선언**이다. `setup /nm`은 설치 검사를 건너뛰지만 OS 구동을 보증하지 않는다. [Microsoft KB189670](https://ftp.zx.net.nz/pub/archive/ftp.microsoft.com/MISC/KB/en-us/189/670.HTM)

QEMU의 `-cpu`, `-smp`, `-icount`는 VM 단위 설정이다. `-icount`는 정확한 MHz 에뮬레이션이 아니다. 지금 설치된 `C:\Program Files\qemu\qemu-system-i386.exe` 11.1.0의 `-accel help`에는 `tcg`만 표시되므로 **그 QEMU 실행 경로는** 하드웨어 가속 조건을 충족하지 않는다. VirtualBox에서 VT-x/AMD-V 또는 Windows Hypervisor Platform을 사용하는 경로는 별도로 허용하며 실제 VM 로그로 확인한다. [QEMU CPU 모델 문서](https://www.qemu.org/docs/master/system/qemu-cpu-models.html), [QEMU `-icount` 문서](https://www.qemu.org/docs/master/system/qemu-manpage.html), [Oracle VirtualBox Hyper-V 통합 문서](https://docs.oracle.com/en/virtualization/virtualbox/7.2/user/AdvancedTopics.html#using-hyper-v-with-oracle-virtualbox)

개발기에서 `cpuapp.c`를 CRT 없이 `-nostdlib`와 KERNEL32만 링크해 빌드한 PE는 **32비트 x86, OS/서브시스템 4.0, import DLL은 KERNEL32.DLL 하나**였다. 호스트 연기 시험에서는 프로필 환경 변수 전달, 자식 종료 코드 전달, 공백이 있는 앱 경로 실행을 확인했다. 이 결과는 Windows 98 게스트에서의 실행 검증을 대신하지 않는다. 빌드 시 별도 진입점 `cpuapp_entry`, `-ffreestanding -fno-builtin -nostdlib`, `-lkernel32`, PE OS/서브시스템 버전 4.0 지정, ASLR/NX/TS-aware 플래그 해제를 유지해야 한다.
