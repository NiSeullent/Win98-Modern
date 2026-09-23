# ShizukuDOS, Win98 VMM, 응용 프로그램별 CPU 정책

## 경계

새로 작성하는 **ShizukuDOS**는 부트·DOS 서비스·진단 계층의 후보다. Windows 98 SE의 GUI가 시작된 뒤 응용 프로그램 스케줄링, 보호 모드 전환, 인터럽트·메모리·드라이버 관리는 Windows 98의 VMM과 그 주변 구성 요소가 담당한다. ShizukuDOS를 다중코어로 바꾸는 작업만으로 Win98 GUI에 SMP가 생기지 않는다. Microsoft는 Windows 98/98SE가 다중 프로세서 컴퓨터에서도 하나만 사용한다고 명시했다. [Microsoft KB Q193645 원문 아카이브](https://helparchive.huntertur.net/document/106764)

[FreeDOS 커널](https://github.com/FDOS/kernel)의 공개 설명은 DOS 커널이 프로그램 로드, 파일·장치 I/O, DOS API를 구현하는 범위를 보여 주는 **비교 자료**다. 이 프로젝트의 부트 기반으로 FreeDOS 코드를 사용하거나 FreeDOS를 수정한다는 뜻이 아니다. ShizukuDOS가 Win98의 기존 DOS 부트 경로와 어떻게 호환될지는 별도 부트 트랙에서 입증해야 한다.

## 요청한 CPU 프로필의 의미

| 프로필 | 요청된 앱 관찰 동작 | 현재 구현 |
| --- | --- | --- |
| i386 | 해당 앱에 386 수준 ISA·CPUID/타이밍을 보임 | 선언만 가능 |
| i486 | 486DX 수준 | 선언만 가능 |
| Pentium MMX | MMX 포함 Pentium 수준 | 선언만 가능 |
| Pentium II | Pentium II 수준 | 선언만 가능 |
| Pentium III | Pentium III/SSE 수준 | 선언만 가능 |
| Modern | 검증된 현대 x86 기능 | 선언만 가능 |

프로필은 VM 전체를 재부팅하거나 QEMU의 `-cpu`를 바꾸는 방식으로 적용하지 않는다. QEMU가 제공하는 `486`, `pentium`, `pentium2`, `pentium3` 등은 VM 시작 시 선택하는 **가상 CPU 모델**이다. 설치된 QEMU의 모델 목록에 정확한 `i386` 또는 `Pentium MMX` 모델도 없다. `pentium,+mmx` 같은 조합을 쓰더라도 가상 머신 전체 설정일 뿐, Win98 안의 한 앱에 적용되는 기능이 아니다. [QEMU CPU 모델 문서](https://www.qemu.org/docs/master/system/qemu-cpu-models.html)

`speed_hint_mhz`는 향후 동작 목표를 저장하는 숫자다. 현재 런처가 MHz를 제어하거나 측정하지 않는다. QEMU의 `-icount`는 VM 전체의 가상 시간과 명령 수를 연결하며 사이클 정확도나 실제 MHz를 제공하지 않는다고 공식 문서가 설명한다. [QEMU `-icount` 문서](https://www.qemu.org/docs/master/system/qemu-manpage.html)

## 강제 적용에 필요한 별도 구현

1. **프로세스 식별:** Win98 게스트의 로더·VMM에서 현재 앱과 자식 프로세스의 정체를 일관되게 추적하고, 프로필 변경·상속·종료를 동기화한다. 현재 `CPUAPP.EXE`는 환경 변수 `W98MOD_CPU_PROFILE`, `W98MOD_CPU_SPEED_HINT_MHZ`를 전달하는 시작점일 뿐이다.
2. **CPUID 응답:** 앱별 가상 기능 목록을 실제 CPUID 결과에 적용하려면 guest-side 가로채기 또는 하이퍼바이저 수정과 guest PID/실행 문맥 전달이 필요하다. Intel VMX 문서는 CPUID로 VM exit가 발생할 수 있는 기구를 설명하지만, 표준 QEMU `-cpu` 옵션은 앱별 정책 인터페이스가 아니다. [Intel SDM VM-exit 표](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3d-part-4-manual.pdf)
3. **명령어 실행 제한:** CPUID 비트를 숨기는 것만으로 MMX/SSE 등 실제 명령어의 실행을 앱별로 거부하거나 에뮬레이트할 수 없다. 실제 명령어 경계를 추적하고 예외를 올리거나 번역하는 별도 엔진이 필요하다. WHPX 가속과 공존시키는 구현·성능은 연구 과제다.
4. **속도 정책:** 앱별 스케줄링/타이머 또는 명령 계측으로 목표 체감 속도를 정의해야 한다. 이를 CPU 클럭의 정확한 복제로 표시하지 않는다. 게임별 프레임·타이머·I/O 결과와 호스트 부하를 함께 측정한다.
5. **Win98 멀티코어:** 일반 GUI 프로세스를 여러 CPU에 스케줄하려면 VMM의 CPU별 상태, AP 초기화, 인터럽트 분배, 잠금·메모리 일관성, 레거시 VxD의 동시 실행 안전성을 설계·검증해야 한다. ShizukuDOS의 DOS 모드 병렬 작업이 성공해도 이 게이트를 통과한 것은 아니다.

## 하드웨어 가속 필수 조건

호스트 VT-x/AMD-V 기반의 하드웨어 가속을 사용해야 한다. **QEMU 경로라면** Windows Hypervisor Platform 백엔드인 `-accel whpx`를 지정하고 초기화 실패 시 중단한다. [QEMU WHPX 문서](https://www.qemu.org/docs/master/system/whpx.html) **VirtualBox 경로라면** 네이티브 VT-x/AMD-V 또는 Windows Hypervisor Platform을 사용하는 Hyper-V 엔진을 허용하고, VM 로그에서 실제 가속 초기화를 확인한다. [Oracle VirtualBox Hyper-V 통합 문서](https://docs.oracle.com/en/virtualization/virtualbox/7.2/user/AdvancedTopics.html#using-hyper-v-with-oracle-virtualbox) QEMU의 TCG 같은 소프트웨어 전용 실행으로 자동 전환해서는 안 된다.

`Check-VT.ps1`은 **QEMU 바이너리에 WHPX 백엔드가 들어 있는지** 확인하는 QEMU 전용 1차 점검이며, VirtualBox 경로의 적합성이나 운영체제 기능·BIOS VT-x/AMD-V·실제 VM 초기화까지 보증하지 않는다. 2026-09-23 현재 설치된 `C:\Program Files\qemu\qemu-system-i386.exe` 11.1.0의 `-accel help`에는 `tcg`뿐이므로 현재 QEMU 실행 파일은 게이트를 통과하지 못한다. 이는 VirtualBox 가속 경로의 성공·실패와 별개다.

## 검증 단계

| 단계 | 실제 시험 | 통과 기준 |
| --- | --- | --- |
| C0 선언 | INI 매핑을 바꾸고 런처로 두 Win98 앱을 각각 실행 | 앱별 선택 프로필과 자식 환경 변수 값이 일치. ISA·속도 변경을 주장하지 않음 |
| C1 호스트 가속 | QEMU라면 `Check-VT.ps1` 통과 후 `-accel whpx`, VirtualBox라면 NEM/WHv 또는 네이티브 VT-x/AMD-V 경로로 Win98 게스트 직접 부팅 | VM 로그에서 선택한 하드웨어 가속 초기화와 실제 게스트 실행을 확인, 소프트웨어 전용 fallback 0건 |
| C2 CPUID 관찰 | 앱별 probe가 CPUID 출력, 명령어 예외, 현재 프로필을 기록 | 두 앱을 동시에 실행해도 각각 올바른 앱 ID로 기록, 기존 동작과 회귀 없음 |
| C3 CPUID 정책 | 첫 두 프로필에 한해 앱별 응답 변경 | 같은 게스트에서 앱 A/B가 서로 다른 CPUID 결과를 확인하고 앱 종료·재시작 후 누출 없음 |
| C4 ISA 정책 | 선택 앱의 지원/미지원 명령어 실험 | 금지된 명령어는 결정적인 예외 또는 에뮬레이션 결과, 다른 앱의 명령 실행은 영향 없음 |
| C5 속도 정책 | 기준 앱의 반복 루프·타이머·게임 프레임 계측 | 정의한 허용 오차와 호스트 부하 범위에서 재현, 실제 MHz라고 표시하지 않음 |
| C6 SMP VMM | 2+ vCPU Win98 보호 모드 부팅과 병렬 앱 실행 | 서로 다른 CPU에서 Win98 GUI 스레드가 동시에 실행됨을 추적 로그로 증명, 장치 I/O·종료·재부팅 반복 회귀 통과 |

C0 외의 게이트는 이 폴더에서 아직 통과하지 않았다. `CPUAPP.EXE`의 Win98 게스트 실행 검증도 남아 있다.
