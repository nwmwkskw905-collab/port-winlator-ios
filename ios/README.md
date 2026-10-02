# `ios/` — Fase 02: Runtime iOS PoC

Base nativa iOS ARM64 + harness de diagnóstico para medir, com evidência, quais mecanismos
fundamentais do futuro runtime do Winlator sobrevivem ao iOS.

Esta pasta é **isolada do projeto Android**. Nada em `app/`, `vortek/`, `gladio/`,
`android_alsa/`, `glibc_patches/`, `installable_components/`, `input_controls/` ou
`wine_addons/` foi modificado. Ver `Documentation/RELATORIO_FASE_02.md` §4.

---

## Regra de leitura dos resultados

Nenhum resultado desta pasta pode ser lido sem a sua classe de evidência:

| Classe | Significado |
| --- | --- |
| `CONFIRMADA POR BUILD` | compila para o target; nada sobre execução |
| `CONFIRMADA POR TESTE EM SIMULADOR` | executou no simulador — **SIMULATOR ONLY** |
| `CONFIRMADA EM DISPOSITIVO FÍSICO` | executou num iPhone real |
| `CONFIRMADA NO HARNESS HOST` | executou num host Linux (x86-64 ou AArch64) — **nunca é resultado iOS** |
| `IMPLEMENTADA MAS NÃO VALIDADA` | código escrito, sem execução correspondente |
| `HIPÓTESE` | afirmação que ainda não tem teste |
| `BLOQUEADA` | o ambiente recusou o mecanismo |

`PASS` só é usado quando houve execução correspondente.

---

## Layout

```
ios/
├── RuntimeCore/            # lógica de baixo nível, sem UI, sem dependências Apple obrigatórias
│   ├── include/
│   │   ├── runtime_platform.h      # RuntimePlatform / DarwinPlatform / LinuxPlatform
│   │   ├── runtime_memory.h        # mmap/mprotect/munmap, alinhamento, dual mapping
│   │   ├── runtime_jit.h           # payload por ISA, icache, ponto de entrada chamável
│   │   ├── runtime_cpu_abi.h       # inventário de registradores do Box64, contrato x18
│   │   ├── runtime_threads.h       # pthreads, TLS, mutex, condition, atomics
│   │   ├── runtime_signals.h       # sigaction, máscaras, SIGSEGV controlado
│   │   ├── runtime_filesystem.h    # semântica POSIX, container/drive_c/dosdevices/temp
│   │   ├── runtime_ipc.h           # AF_UNIX, SCM_RIGHTS, pipe, shm, kqueue
│   │   ├── runtime_context.h       # RuntimeContext/Module/Thread/Memory (modelo in-process)
│   │   └── runtime_loader.h        # formato interno mínimo + relocação + entry point
│   └── src/
├── Diagnostics/
│   ├── include/phase02_log.h       # formato [PHASE02] TEST/DEVICE/ARCH/RESULT/DETAIL
│   ├── include/phase02_harness.h   # seleção de suites
│   └── src/
├── RuntimePoC/             # app iOS (SwiftUI + ponte ObjC) e harness CLI
│   ├── WinlatorPhase02App.swift
│   ├── ContentView.swift
│   ├── Phase02Bridge.h/.m
│   ├── Info.plist
│   ├── Phase02-Bridging-Header.h
│   └── main.c              # harness CLI para host/CI
├── Tests/test_runtime_core.c
├── tools/
│   ├── build_host_harness.sh
│   ├── build_aarch64_cross.sh
│   ├── aarch64-linux-toolchain.cmake
│   ├── build_ios.sh
│   ├── generate_xcodeproj.py
│   ├── validate_xcodeproj.py
│   └── collect_evidence.sh
├── Documentation/
│   ├── RELATORIO_FASE_02.md
│   └── evidence/           # saídas brutas citadas no relatório
├── CMakeLists.txt
└── WinlatorPhase02.xcodeproj   # gerado por tools/generate_xcodeproj.py
```

---

## Como executar

### Em Linux / CI (sem Xcode)

```sh
sh ios/tools/build_host_harness.sh                  # build + ctest + suíte completa
sh ios/tools/build_aarch64_cross.sh                 # build AArch64 + execução sob qemu
python3 ios/tools/validate_xcodeproj.py             # validação estática do .xcodeproj
sh ios/tools/collect_evidence.sh                    # regenera toda a evidência do relatório
```

`IOS_BUILD=UNTESTED` nesse caminho, com `REASON=NO_APPLE_TOOLCHAIN`. Isso é limitação de
ambiente, não falha de implementação.

### Em macOS com Xcode

```sh
sh ios/tools/build_ios.sh both        # iphonesimulator + iphoneos (CODE_SIGNING_ALLOWED=NO)
```

Ou diretamente:

```sh
xcodebuild -project ios/WinlatorPhase02.xcodeproj -scheme WinlatorPhase02 \
           -sdk iphonesimulator -configuration Debug build

xcodebuild -project ios/WinlatorPhase02.xcodeproj -scheme WinlatorPhase02 \
           -sdk iphoneos -configuration Debug CODE_SIGNING_ALLOWED=NO build
```

Se o projeto for editado, regenere-o a partir das fontes em vez de mexer no `pbxproj`:

```sh
python3 ios/tools/generate_xcodeproj.py
python3 ios/tools/validate_xcodeproj.py
```

### No iPhone físico

Ver o bloco `PHYSICAL_DEVICE_VALIDATION_REQUIRED` em
`Documentation/RELATORIO_FASE_02.md` §29. O app mostra e exporta o relatório; com
`UIFileSharingEnabled` ativo, ele aparece em Arquivos › No meu iPhone › WinlatorPhase02, e
`xcrun devicectl device copy from` também funciona.

---

## Suites

| Suite | Testes |
| --- | --- |
| `memory` | RW/R/RX, matriz de transições de proteção, alinhamento, W^X, dual mapping |
| `jit` | disponibilidade de `MAP_JIT` / `pthread_jit_write_protect_np`, microteste executado, icache |
| `cpu` | reserva de x18, inventário de registradores do Box64, plano de remapeamento |
| `threads` | create/join, TLS, mutex, condition, atomics |
| `signals` | install/query/restore, máscara, SIGSEGV controlado com `SA_SIGINFO` |
| `fs` | container/drive_c/dosdevices/temp, rw, rename, symlink, chmod, paths longos, temp file |
| `ipc` | AF_UNIX stream, socketpair, `SCM_RIGHTS`, pipe, shm POSIX, sincronização, kqueue |
| `loader` | módulo interno mínimo: bytes → memória → relocação → entry point → execução |

`--suite all` roda tudo; `--suite <nome>` roda uma; `--export FILE` grava o relatório.
