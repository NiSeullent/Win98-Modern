# KernelEx 소스 선언과 구현 본문 분리 감사

고정된 KernelEx 커밋 `31cdfc3560fc116637ee8ed7be31b12f3aacf5d1`의
`kexbases`·`kexbasen` API 표 25개에서 `DECL_API` **1,097행**을 읽었다.
같은 DLL/이름의 여러 경로를 합치면 **1,060개 이름**이다. 원본 Win98 SE
CAB 내보내기 목록은 이 감사를 위한 *이름 대조 자료*일 뿐이며, 게스트에
설치됐거나 그 구현이 정확하게 동작한다는 증거가 아니다.

이전 이름 목록은 `DECL_TAB("...DLL")`만 파싱하여 `WINSPOOL.DRV`의
31개 선언을 빠뜨렸다. 파서를 `.DRV` 등 Windows PE 모듈 확장자도
인식하도록 고쳤다. 갱신한
[선언 목록](../benchmarks/kernelex-source-declarations-v1.json)에는
18개 모듈 식별자와 1,060개 이름이 들어 있다.

## 본문 분류 결과

[전체 기계 판독 결과](../benchmarks/kernelex-source-body-audit-v1.json)는
각 표 행의 이름, 연결 대상 함수, 선언 위치, 발견된 본문 또는 매크로
위치와 해당 파일 해시를 기록한다. 여러 표 행이 같은 이름을 선언할 수
있으므로 아래 숫자의 분모는 **1,097행**이다.

| 소스 분류 | 행 수 | 의미 |
| --- | ---: | --- |
| `explicit_unimplemented_macro` | 104 | 대상이 `UNIMPL_FUNC` 미구현 매크로에 연결됨 |
| `explicit_stub_body` | 6 | `_stub` 이름의 실제 본문이 있음; 가짜 성공값도 포함 |
| `suspicious_stub_body` | 30 | `_stub` 이름이 아니어도 본문에 `stub`/`UNIMPLEMENTED` 표시가 있음 |
| `forward_to_unicows` | 87 | UNICOWS 전달 매크로 사용; 목적지 설치·동작 확인 필요 |
| `native_alias_candidate` | 18 | 소스 본문 대신 CAB 이름 목록의 다른 원본 함수로 연결되는 후보 |
| `source_delegate_candidate` | 123 | 한 줄짜리 다른 함수 호출 본문 후보 |
| `source_body_candidate` | 628 | 본문을 찾음; 정확한 API 동작 여부는 미검증 |
| `unresolved_target` | 100 | 스캔한 C/C++에서 연결 대상 본문을 찾지 못함 |
| `ambiguous_multiple_bodies` | 1 | 같은 대상의 본문이 둘 이상 보임; 전처리 분기 검사 필요 |

이 분류는 빌드·CORE.INI 라우팅·Win98 직접 호출·앱 기능 성공 점수가
아니다. 특히 628개 본문 후보를 호환 API 개수에 더하면 안 된다. 도구는
`#if` 조건을 평가하지 않으므로, `GetProcessId_new`의 비활성 `#if 0`
본문도 중복 후보로 남긴다. 외부 라이브러리 구현과 어셈블리 정의는
`unresolved_target`에 남을 수 있다. 표시 없는 가짜 구현은 이 휴리스틱을
통과할 수 있으므로 이후 수동 계약 검토와 실제 게스트 시험이 필요하다.

## Notepad++ 8.9.8 직접 가져오기 14개

고정된 x86 `notepad++.exe` SHA-256
`960ad7a8d443536ceeeafa18f99aecb70793fc7c4fb6ef23678a9d46bbf7bc78`의
614개 일반 가져오기 중 원본 CAB에는 없고 KernelEx 소스 표에는 있는
**14개 이름**을 같은 방식으로 분류했다. 표는 *과거 가져오기 후보군의
소스 품질 검사*이며 현재 프로젝트 래퍼가 교체한 경로의 실패를 뜻하지
않는다.

| 분류 | 대상 이름 |
| --- | --- |
| 명시적 미구현 4개 | `GDI32.GdiAlphaBlend`, `KERNEL32.ReplaceFileW`, `SHELL32.SHOpenFolderAndSelectItems`, `USER32.SetLayeredWindowAttributes` |
| 성공처럼 보이는 미구현 1개 | `ADVAPI32.CheckTokenMembership` |
| 원본 별칭 후보 1개 | `KERNEL32.GetNativeSystemInfo` → `GetSystemInfo` |
| 본문 후보 8개 | `KERNEL32.DecodePointer`, `EncodePointer`, `GetFileSizeEx`, `GetModuleHandleExW`, `InitializeSListHead`, `SetFilePointerEx`; `SHELL32.SHGetFolderPathW`, `SHParseDisplayName` |

`CheckTokenMembership_new`는
`apilibs/kexbases/Advapi32/security.c:176`에서 `FIXME(...stub!)`를
출력하고, 요청한 토큰·SID를 검사하지 않은 채 `*IsMember = TRUE`와
`TRUE`를 반환한다. 이름과 반환값만 보면 성공처럼 보이지만 실제 권한
질문에 답하지 않는다. `GdiAlphaBlend`는
`apilibs/kexbases/Gdi32/_gdi32_stubs.c:25`의 `UNIMPL_FUNC(...,11)`에
연결되어 픽셀을 합성하지 않는다. `GetNativeSystemInfo`는
`GetSystemInfo` 이름 별칭이다. CPU·메모리의 현대적 동작이 검증된
것은 아니다. `SHGetFolderPathW_new`는 본문이 있지만 지원 DLL 함수
조회 실패 시 `E_NOTIMPL`을 돌려준다. 이런 사례 때문에 이름 목록을
전체 API 호환률의 분자로 사용하지 않는다.

## 재현

Python 3.10 이상에서 다음 명령을 프로젝트 루트에서 실행한다.
`benchmarks/media/`의 Notepad++ 실행 파일은 공개 패키지에 포함되지
않으므로 본문 전체 감사만 원하면 `--app-pe`를 생략할 수 있다.

```powershell
python tools/build_kernelex_source_manifest.py --source-root third_party/KernelEx --out benchmarks/kernelex-source-declarations-v1.json
python tools/audit_kernelex_stubs.py --source-root third_party/KernelEx --native-manifest benchmarks/win98se-ko-oem-native-exports-v1.json --app-pe benchmarks/media/npp-8.9.8/notepad++.exe --project-root . --out benchmarks/kernelex-source-body-audit-v1.json
python -B -m unittest discover -s tools/tests -p test_audit_kernelex_stubs.py -v
python -B -m unittest discover -s tools/tests -p test_measure_pe_coverage.py -v
```

새 선언 목록 해시를 참조하는 가져오기 보고서와 전체 API 작업 목록도
재생성했다. 이 감사 결과는 우선순위와 위험 검토에
사용하고, 실제 호환성 실적은 별도 게스트 계약 시험에 연결한다.
