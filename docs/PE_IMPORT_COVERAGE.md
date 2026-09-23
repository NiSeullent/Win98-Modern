# 32비트 앱의 PE 가져오기 이름 측정

`tools/measure_pe_coverage.py`는 Python 3.10 이상으로 대상 EXE/DLL의 **정적 PE 가져오기 이름**이 원본 Windows 98 SE, 앱에 포함된 DLL, KernelEx 소스 선언, 프로젝트 래퍼 선언 중 어디에 있는지 센다. 일반 가져오기, 지연 가져오기, 이름 가져오기, `#번호` 서수 가져오기를 포함한다. 결과는 **앱 실행 성공률이나 Windows API 전체 호환률이 아니다.** [Microsoft PE 형식 명세](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)

## 확인된 Windows 98 SE 원본 기준

제공된 한국어 OEM ISO의 `/WIN98` CAB 76개를 추출해 PE32 x86 내보내기 **이름과 서수만** 저장했다. 각 CAB의 SHA-256을 ISO 안의 원본 CAB와 대조한 [원본 이름 목록](../benchmarks/win98se-ko-oem-native-exports-v1.json)은 866개 PE 모듈의 내보내기 식별자 79,445개다. 식별자 수에는 같은 함수의 이름과 서수가 모두 포함될 수 있다. ISO에는 선택 설치 구성요소도 있으므로 이 목록은 *해당 ISO가 가진 원본 모듈 후보*이며, 게스트에 설치되어 로드되는 DLL 집합을 증명하지 않는다.

| 증거 | SHA-256 |
| --- | --- |
| `vm/Win98-SE-ko-OEM.iso` | `4c1b148bfd8aa9ffa702d6756ffa32064c49bb78c00d074225db5a0f302c0873` |
| ISO 안의 `WIN98_30.CAB` | `8aa71e968ba5a05456255487b7a624a06ba623b292f475c68e88adf0e37f2d8c` |
| `WIN98_30.CAB`에서 추출한 `KERNEL32.DLL` (471,040바이트) | `6771ab74633e9de1359864bd4306a2ed669bec50de7ac9bb9978c05bf04563ca` |

원본 `KERNEL32.DLL` PE 내보내기 표에서 `DeleteCriticalSection`, `EnterCriticalSection`, `GetExitCodeProcess`, `GetFileInformationByHandle`, `GetLastError`, `GetSystemTimeAsFileTime`, `GetTickCount`, `GlobalMemoryStatus`, `InitializeCriticalSection`, `LeaveCriticalSection`, `SetLastError` **11개 전부 확인**했다. 이는 `tests/check_pe98.py`의 현재 KERNEL32 허용 목록에 해당한다. CAB 파일 기록 6,223개가 가리키는 고유 파일 6,048개 가운데 6,047개가 추출되었다. 남은 `GM16.DLS`는 PE DLL/EXE가 아닌 [DLS 사운드 파일](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/dls-download-support)이므로 이 PE API 목록에 들어가지 않는다. 다중 CAB 추출에 사용한 `cabextract`는 이 파일의 압축 오류로 종료 코드 1을 반환했으므로, 누락 파일 검사는 별도로 수행했다. [libmspack/cabextract](https://github.com/kyz/libmspack/tree/master/cabextract)

KernelEx의 [고정 소스 선언 목록](../benchmarks/kernelex-source-declarations-v1.json)은 커밋 [`31cdfc3560fc116637ee8ed7be31b12f3aacf5d1`](https://github.com/metaxor/KernelEx/tree/31cdfc3560fc116637ee8ed7be31b12f3aacf5d1)의 `kexbases`·`kexbasen` API 표 25개 파일에서 추출한 1,029개 선언이다. `CORE.INI`의 기본 `DCFG1`은 `std,kexbases,kexbasen`을 나열하지만 Windows 98용 override에서 일부 이름을 `none` 또는 `std`로 바꾼다. 따라서 이 목록은 **소스상 후보 이름의 상한**이며 설치·활성화·호출 동작의 증거가 아니다. 보고서는 원본 PE와 KernelEx 소스의 일치를 별도 열에 둔다.

## 입력 준비

1. 재현용 미디어는 무시된 `benchmarks/media/`에 둔다. Python 의존성은 `pip install -r tools/requirements-test-media.txt`로 설치한다. `python tools/extract_win98_iso_cabs.py --iso vm/Win98-SE-ko-OEM.iso --manifest benchmarks/win98se-ko-oem-native-exports-v1.json --out-dir benchmarks/media/win98-iso-cabs`로 CAB 76개를 ISO에서 복사하고 해시를 검증한다. WSL/Linux의 `cabextract` 1.11에서 `benchmarks/media/win98-iso-cabs`로 이동한 뒤 `mkdir -p ../win98-iso-extracted`를 실행하고 `cabextract -q -d ../win98-iso-extracted WIN98_22.CAB`, `cabextract -q -d ../win98-iso-extracted MINI.CAB`, `cabextract -q -d ../win98-iso-extracted CHL99.CAB`를 차례로 실행한다. 첫 명령의 `GM16.DLS` 오류는 예상된다. 그 뒤 `python tools/build_win98_iso_baseline.py --iso vm/Win98-SE-ko-OEM.iso --cab-dir benchmarks/media/win98-iso-cabs --extracted-dir benchmarks/media/win98-iso-extracted --out benchmarks/win98se-ko-oem-native-exports-v1.json`을 실행한다. 스크립트는 모든 CAB를 ISO와 다시 대조하고 고유 파일의 누락 목록을 검사한다. 추출 파일 바이트 자체의 재추출 비교는 하지 않으므로 추출 과정의 출처도 보존한다.
2. `python tools/build_kernelex_source_manifest.py --source-root third_party/KernelEx --out benchmarks/kernelex-source-declarations-v1.json`으로 KernelEx 소스 목록을 다시 만든다. 실제 게스트를 측정할 때에는 설치된 KernelEx 빌드와 활성 구성 모드를 따로 확인한다. `kexbases`와 `kexbasen` 소스를 무조건 결합한 값을 게스트 제공 API로 취급할 수 없다.
3. `--pe`는 PE 내보내기를 읽고, `--source`는 KernelEx의 `DECL_TAB`/`DECL_API`(이름·서수), 프로젝트의 `M98_API`, `.def` 같은 **소스 선언**을 읽는다. `DLL=파일` 표기는 DLL 이름을 명시적으로 지정한다. 다른 ISO 또는 설치 게스트 DLL로 `manifest` 명령을 사용할 수도 있다.

```powershell
python tools/measure_pe_coverage.py manifest `
  --pe 'KERNEL32.DLL=C:\guest-dlls\KERNEL32.DLL' `
  --pe 'USER32.DLL=C:\guest-dlls\USER32.DLL' `
  --provenance 'Win98 SE guest build ..., captured YYYY-MM-DD, SHA256 ...' `
  --out 'build\win98-guest-native-exports.json'
```

여기서 `C:\guest-dlls`는 예시다. 위에서 제공한 원본 ISO 목록은 게스트 설치본이 아니다. 결과 JSON은 다음 구조다.

```json
{
  "schema": "w98mod.export-manifest.v1",
  "provenance": "설치 버전과 수집 근거",
  "dlls": {
    "KERNEL32.DLL": ["CreateFileA", "#123"]
  }
}
```

래퍼는 `--wrapper-source src/m98wrap.c`로 KernelEx에 등록하는 논리 API 이름을 읽거나, `--wrapper-pe DLL=실제.dll`로 일반 PE 내보내기 이름을 읽는다. `M98WRAP.DLL` 아티팩트에서 보이는 `get_api_table` 한 항목이 등록된 모든 `KERNEL32.DLL` 함수를 뜻하지는 않으므로, 이 프로젝트의 KernelEx API 테이블은 소스 입력을 써야 한다.

## 앱별 실행

`--app NAME=경로`를 반복한다. 경로가 폴더이면 그 아래의 `.exe`와 `.dll`을 재귀적으로 읽는다. 핵심 EXE·DLL만 측정하려면 `--app-file NAME=파일`을 같은 앱 이름으로 여러 번 지정한다. 직접 파일을 위치 인자로 넣을 수도 있다. 폴더에 선택적 플러그인이나 다른 프로그램의 DLL이 섞여 있으면 대상 집합을 먼저 선별한다. PE32+ 또는 x86 이외 파일이 들어오면 결과를 조용히 누락하지 않고 오류를 낸다.

```powershell
python tools/measure_pe_coverage.py report `
  --baseline 'benchmarks\win98se-ko-oem-native-exports-v1.json' `
  --kernelex-source-manifest 'benchmarks\kernelex-source-declarations-v1.json' `
  --wrapper-source 'src\m98wrap.c' `
  --app-file 'Chromium 150=C:\targets\chromium-150\chrome.exe' `
  --app-file 'Chromium 150=C:\targets\chromium-150\chrome.dll' `
  --app-file 'Supermium=C:\targets\supermium\chrome.exe' `
  --app-file 'VLC=C:\targets\vlc\vlc.exe' `
  --app-file 'Notepad++=C:\targets\notepad-plus-plus\notepad++.exe' `
  --json-out 'build\pe-import-coverage.json'
```

경로는 설명용 예시다. 제공된 미디어의 고정 선택 파일 및 앱 동봉 DLL 목록으로 보고서를 다시 만들려면 `python tools/run_curated_import_report.py`를 실행한다. 현재 제공된 VS Code 1.138.0 아카이브는 x64이므로 PE32 x86 가져오기 측정에서 오류로 제외한다. x64 파일에 억지로 0% 또는 100% 점수를 부여하지 않는다.

## 수치 해석

- **앱별 비율:** 해당 앱에 입력한 모든 PE의 가져오기 항목 중 이름이 원본 ISO·앱 동봉 DLL·KernelEx 선언·프로젝트 래퍼 중 한 곳과 맞는 항목 수 ÷ 전체 항목 수.
- **가중 비율:** 모든 앱의 가져오기 항목을 합쳐 같은 식으로 계산한다. 한 이름이 여러 EXE/DLL에 나타나면 나타난 횟수만큼 센다. 가져오기가 0개이면 100%가 아니라 `N/A`다.
- `baseline`은 원본 ISO PE 이름, `app_peer`는 해당 앱에 동봉된 PE 내보내기, `kernelex_source`는 KernelEx 소스 선언, `wrapper_artifact`는 프로젝트 래퍼 PE 내보내기, `wrapper_source`는 프로젝트 소스 선언의 일치 횟수다. 이 우선순위로 한 가져오기를 한 범주에만 배정한다. `baseline_or_artifact_percent`는 원본과 래퍼 PE만 포함하고 앱 동봉 DLL·소스 선언을 제외한 참고 수치다. `unresolved`는 DLL·이름·일반/지연 구분과 횟수를 표시한다.

이름 일치는 실제 로더 연결, 호출 규약, 구조체 레이아웃, 반환값, 오류 처리, 그래픽·드라이버·CRT·CPU 요구를 검증하지 않는다. `GetProcAddress`, COM/WinRT, 플러그인, 실행 중 `LoadLibrary`로 찾는 항목도 이 정적 목록에 나타나지 않을 수 있다. 앱 동봉 DLL의 존재는 해당 게스트에서의 로드 가능성을 증명하지 않고, KernelEx·프로젝트 소스 선언은 빌드·설치·동작의 증거가 아니다. PE forwarder의 최종 대상도 이 도구가 검증하지 않는다.

## 고정 앱 파일의 측정 결과

[상세 JSON](../benchmarks/pe-import-name-report-v1.json)은 선택한 EXE/DLL 10개의 파일 SHA-256, 일반·지연 가져오기, 범주별 일치, 미해결 이름을 담는다. 앱에 동봉된 `chrome_elf.dll`, Supermium `p_*.dll`/`pwrp_k32.dll`, VLC `libvlc*.dll`의 PE 내보내기는 해당 앱에만 적용했다. `llvm-readobj --coff-imports`로 별도 비교한 일반 가져오기 수는 Chromium `chrome.exe` 251개, Supermium `chrome.exe` 233개, VLC `vlc.exe` 165개, Notepad++ 614개로 이 파서와 일치했다. 지연 가져오기는 이 도구가 별도로 센다.

| 선택 앱 | 일반 + 지연 | 원본 ISO | 앱 동봉 DLL | KernelEx 소스 | 프로젝트 소스 | 미해결 | 이름 일치 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Chromium 150 (3개 파일) | 967 + 903 | 1,251 | 19 | 143 | 13 | 444 | 1,426/1,870 (76.26%) |
| Supermium 144 R5 (3개 파일) | 977 + 862 | 656 | 897 | 44 | 0 | 242 | 1,597/1,839 (86.84%) |
| VLC 3.0.24 (3개 파일) | 657 + 0 | 505 | 138 | 14 | 0 | 0 | 657/657 (100.00%) |
| Notepad++ 8.9.8 (1개 파일) | 614 + 0 | 537 | 0 | 14 | 4 | 59 | 555/614 (90.39%) |

가져오기 **출현 횟수**를 가중한 합계는 4,235/4,980 (85.04%)다. VLC의 100%도 정적 이름 일치만 뜻한다. Chromium에서 남은 `KERNEL32.DLL` 항목이 81개이고, Supermium의 자체 래퍼가 해결해 준 항목이 897개여서 이름 목록의 우선순위를 정하는 데 쓸 수 있다. 이 수치는 선택한 파일과 소스 후보를 사용한 예비 조사이며, Windows 98 게스트 앱 실행 결과가 아니다.

## Windows API 전체 100% 목표와의 관계

사용자가 지정한 분모는 **Windows API 전체 표면**이다. 위의 앱별 가져오기 총수는 그 분모가 아니다. 따라서 이 도구의 높은 수치를 Windows API 전체 100% 달성이나 필수 앱 구동 성공으로 표시해서는 안 된다.

고정 Windows SDK `10.0.28000.2705`에서 [후보 레코드 151,330건](API_SURFACE_28000.md)을 재현 가능하게 추출했다. 이 후보 목록은 최종 분모가 아니다. Win32 DLL 함수뿐 아니라 COM 인터페이스, WinRT, 시스템 서비스 등의 포함 범위와 A/W 변형·서수·별칭·버전별 API의 집계 규칙을 정해야 한다. 헤더의 선언 수나 PE 내보내기 수에는 중복과 비공개 항목 등이 섞일 수 있다. 각 항목의 적용 범위·의미·동작 시험 결과까지 갖춘 뒤 전체 분모와 구현된 분자를 비교해야 한다. **현재 전체 API 호환성 비율은 아직 산출하지 않았다.**

## 검사

`python -B -m unittest discover -s tools/tests -p 'test_measure_pe_coverage.py' -v`는 작은 합성 PE32로 일반/지연/서수 가져오기, PE 내보내기, 원본·앱 동봉 PE·KernelEx 선언·프로젝트 래퍼·미해결 집계, 가져오기 0개의 `N/A` 처리, PE32+ 거부를 확인한다. 현재 6개 시험이 통과했다. 합성 시험은 실제 앱의 호환성 증거가 아니다. 실제 VS Code 1.138.0 `Code.exe`도 `machine 0x8664`, thunk 폭 8로 검사기가 명시적으로 거부함을 확인했다.
