# RELATÓRIO FASE 02 — RUNTIME PoC RECONSTRUÍDA

**Identificação obrigatória:** `PHASE_02_RECONSTRUCTED_POC`
**Data:** 2026-10-02 (UTC) / 2026-10-01 (America/Sao_Paulo)
**Branch:** `ios-phase-02-reconstructed-poc` (criada de `ios-phase-04-box64-registers` @ `e9f016f`)
**Classificação do estado:** `PHASE_02_RECONSTRUCTED_READY_FOR_APPLE_BUILD` + `IPHONE_VALIDATION_PENDING`

> **Aviso de origem.** Este código **não é** a Runtime PoC original da Fase 02. A busca final
> de recuperação classificou a original como `PHASE_02_POC_CONFIRMED_LOST` (nenhuma cópia em
> disco, em compactados, em objetos Git, em snapshots do agente ou nos repositórios GitHub).
> A reconstrução foi **expressamente autorizada** como trabalho novo, guiado pelos artefatos
> documentais que sobreviveram. Nenhum resultado histórico foi copiado para este relatório.

---

## 1. Histórico (o que se sabe da PoC perdida — sem números inventados)

| Fato histórico | Fonte |
| --- | --- |
| A Fase 02 existiu, foi implementada e ficou classificada como `PHYSICAL_VALIDATION_REQUIRED` | registro de conversa das fases |
| Os itens que dependiam de validação física: `MAP_JIT`, `pthread_jit_write_protect_np`, mapeamento duplo RW/RX, JIT no iPhone, conflito 16 KiB (host) × 4 KiB (guest), `ucontext`, comportamento de x18 | `ios/box64-registers/docs/RELATORIO_FASE_04.md` §31 |
| A árvore `ios/` tinha `RuntimeCore/`, `Diagnostics/`, `RuntimePoC/`, `Tests/`, `tools/`, `Documentation/` e um `.xcodeproj` **gerado** por script | `ios/README.md` (sobrevivente) |
| Os alvos eram `phase02_runtime_core`, `phase02_poc`, `phase02_unit_tests`; as suítes eram memory/jit/cpu/threads/signals/fs/ipc/loader | `ios/CMakeLists.txt` (sobrevivente) |

**Os números antigos não podem ser comparados.** O diretório de evidências da Fase 02 e o
`RELATORIO_FASE_02.md` original desapareceram; não existe nenhuma medição histórica preservada.
Portanto este relatório **não apresenta tabela "antes × depois"** — apresentaria números
inventados. O que existe é o que foi medido agora, na §5.

## 2. Fontes documentais utilizadas (todas realmente existentes)

1. `ios/README.md` — árvore de diretórios, nomes de arquivo, as 8 suítes, os comandos de build,
   as duas linhas de `xcodebuild` e a informação de que o projeto Xcode era gerado.
2. `ios/CMakeLists.txt` — `project(WinlatariOSPhase02 C CXX)`, política de warnings, lista
   integral dos 12 fontes de `RuntimeCore/src` + 2 de `Diagnostics/src`, alvos e entradas do ctest.
3. `ios/.gitignore` — produtos de build esperados (`build/`, `*.o`, `*.a`).
4. `ios/box64-registers/docs/RELATORIO_FASE_04.md` — §1 (perda das Fases 01–03) e §31
   (dependências de validação física herdadas da Fase 02).
5. `ios/box64-registers/evidence/apple_target_attempt.txt` — o registro `NO_APPLE_TOOLCHAIN` e a
   sonda de header, que orientam o que o CI Apple deve provar.

## 3. Diferenças conhecidas em relação à PoC perdida (marcadas como novas)

| Item | Estado |
| --- | --- |
| Bundle identifier | `io.winlator.phase02.reconstructed` — **`NEW_RECONSTRUCTED_BUNDLE_IDENTIFIER`** (o original é irreconhecível) |
| Deployment target | **16.0** — **`NEW_RECONSTRUCTED_DEPLOYMENT_TARGET`** (escolhido pelo uso de `ShareLink`; iPhone 13 suporta) |
| Formato de módulo do loader | Definição **nova** desta reconstrução (`RTM1`), documentada em `runtime_loader.h` — não se afirma que era o formato original |
| Mapeamento duplo RW/RX | Implementado com objeto POSIX de memória compartilhada mapeado duas vezes; o mecanismo original é desconhecido |
| Microteste JIT | `42` → reescrita + sincronização de icache → `4242`, como pede a especificação da reconstrução |
| Projeto Xcode | Volta a ser **gerado** por `tools/generate_xcodeproj.py`, como o README descrevia |
| Numeração/contagens de teste | Novas, medidas nesta máquina (§5) — nada copiado |

## 4. Arquivos recriados

```
ios/RuntimeCore/include/    10 headers: runtime_{platform,memory,jit,cpu_abi,threads,signals,filesystem,ipc,context,loader}.h
ios/RuntimeCore/src/        12 fontes: linux_platform.c darwin_platform.c runtime_memory.c runtime_dual_mapping.c
                                       runtime_jit.c runtime_cpu_abi.c runtime_threads.c runtime_signals.c
                                       runtime_filesystem.c runtime_ipc.c runtime_context.c runtime_loader.c
ios/Diagnostics/include/    phase02_log.h, phase02_harness.h
ios/Diagnostics/src/        phase02_log.c, phase02_harness.c
ios/RuntimePoC/             WinlatorPhase02App.swift, ContentView.swift, Phase02Bridge.h/.m,
                            Phase02-Bridging-Header.h, Info.plist, WinlatorPhase02.entitlements, main.c
ios/Tests/                  test_runtime_core.c
ios/tools/                  build_host_harness.sh, build_aarch64_cross.sh, aarch64-linux-toolchain.cmake,
                            build_ios.sh, generate_xcodeproj.py, validate_xcodeproj.py, collect_evidence.sh
ios/                        WinlatorPhase02.xcodeproj/ (gerado: project.pbxproj + xcscheme compartilhado)
ios/Documentation/          este relatório + evidence/
```

O `ios/CMakeLists.txt` **não precisou de alteração**: a reconstrução produziu exatamente os
arquivos que ele já listava — os próprios nomes vieram dele. `ios/README.md` ganhou apenas uma
nota de identificação no topo.

## 5. Testes e resultados medidos agora

**Ferramentas:** gcc 14.2.0 (host x86-64), aarch64-linux-gnu-gcc 14.2.0, cmake 3.31.6,
qemu-aarch64 10.0.13. Política de warnings do `CMakeLists.txt` mantida integralmente
(`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wpointer-arith -Wcast-align
-Wstrict-prototypes -Wmissing-prototypes`), **sem nenhuma supressão adicionada**.

### 5.1 Host x86-64 Linux (`build/host`)

| Medida | Resultado |
| --- | --- |
| Build | **0 warnings, 0 errors** |
| `ctest` | **2/2 passed** (`phase02_unit_tests`, `phase02_suite_all`) |
| Testes unitários | **91 checagens, 0 falhas** (13 grupos, casos positivos e negativos) |
| Suíte completa | **56 records: 52 PASS, 0 FAIL, 0 BLOCKED, 2 UNSUPPORTED, 0 UNTESTED, 2 NOT_APPLICABLE → summary PASS** |

Os 4 registros não-PASS são resultado, não defeito:
`jit.map_jit_probe` = UNSUPPORTED (Linux não tem `MAP_JIT`), `jit.write_protect_np` = UNSUPPORTED,
`cpu.arm64_hwcap` = NOT_APPLICABLE (host x86-64), `cpu.scope` = NOT_APPLICABLE (fatos de host não
provam iOS).

Destaques do que **executou de verdade** no host: microteste JIT (`42` → reescrita → `4242`),
transições RW→R→RW→R-X com fault de escrita observado (`si_addr=0x7f09b65d7000`), mapeamento duplo
aliasing confirmado, `SCM_RIGHTS` com descritor realmente usado, epoll, loader `RTM1` aceitando
módulo válido e rejeitando 7 classes de imagem inválida, SIGSEGV controlado com `si_addr`.

### 5.2 AArch64 sob QEMU (`build/aarch64`) — `AARCH64_QEMU`

| Medida | Resultado |
| --- | --- |
| Build cross | **0 warnings**; binário `ELF 64-bit LSB pie executable, ARM aarch64` |
| Testes unitários | **90 checagens, 0 falhas** |
| Suíte completa | **56 records: 53 PASS, 0 FAIL, 0 BLOCKED, 2 UNSUPPORTED, 1 NOT_APPLICABLE → summary PASS** |
| Diferença de 91→90 | as checagens estruturais específicas de ISA diferem (x86-64 tem uma asserção a mais: byte `0xB8` + `ret` + imediato vs. `RET` + comprimento); explicada, não escondida |
| `cpu.arm64_hwcap` | PASS aqui: `AT_HWCAP=0x00000000effffffb` (no host era NOT_APPLICABLE) — coerente com 53 vs 52 PASS |
| Emissor JIT | `ISA=aarch64`, 8 bytes (`MOVZ`+`RET`) |

**QEMU não valida iOS.** Estes resultados provam que o mesmo código executa em AArch64 e que o
emissor AArch64 produz instruções corretas; nada além disso.

### 5.3 Estrutura do projeto Xcode

| Medida | Resultado |
| --- | --- |
| `tools/validate_xcodeproj.py` | **34 checagens, 0 falhas** |
| Cobertura | identificadores definidos/únicos, seções obrigatórias, cada referência resolvendo em disco, **todo fonte compilável presente na Sources phase**, Info.plist e bridging header ligados, bundle id, deployment target, SDKROOT, Debug/Release, scheme apontando para o alvo e o produto |
| Entitlements | arquivo presente, e a checagem confirma que **não** está ligado a `CODE_SIGN_ENTITLEMENTS` |
| `xcodebuild` | **não executado aqui**: `IOS_BUILD=UNTESTED REASON=NO_APPLE_TOOLCHAIN` |

## 6. Limitações declaradas

1. **Sem toolchain Apple neste ambiente** — nenhum `.app`, nenhuma IPA e nenhum `xcodebuild` foram
   produzidos aqui. O que existe é a validação estrutural do projeto e o caminho de CI.
2. **Resultados de host e de QEMU não são resultados de iPhone.** O próprio relatório de execução
   carrega a nota `host results are NOT iOS results`.
3. **O runner macOS do CI é macOS, não iOS**: ele exercita o backend Darwin (MAP_JIT,
   `pthread_jit_write_protect_np`, `sys_icache_invalidate`) de verdade, mas sob as regras do
   macOS. Esses dados serão rotulados `MACOS_RUNNER`.
4. **`MAP_JIT` e execução de memória gerada no iOS dependem do contexto de assinatura** — por isso
   a suíte responde com `PASS`/`BLOCKED` + errno real, e nunca "sim" por otimismo.
5. **O sandbox do iOS não foi observado**: os testes de filesystem usam o diretório temporário que
   o app passa; o comportamento em `Documents/`/containers só se mede no dispositivo.

## 7. Dependências da validação física (iPhone 13)

| Item | Como será medido |
| --- | --- |
| Execução de código gerado | `jit.execute_return_42` / `jit.execute_return_4242` |
| `MAP_JIT` | `jit.map_jit_probe` (PASS/BLOCKED + errno) |
| `pthread_jit_write_protect_np` | `jit.write_protect_np` |
| W^X estrito e transições | suíte `memory` (matriz RW/R/R-X + fault de escrita) |
| Mapeamento duplo RW/RX | `memory.dual_mapping_rw_rx` |
| Tamanho de página real | `cpu.page_size` (reporta `PAGE_SIZE=<valor real>`, sem hardcode) |
| Threads/TLS, signals, filesystem sandbox, AF_UNIX, `SCM_RIGHTS`, kqueue | suítes `threads`, `signals`, `fs`, `ipc` |
| Loader experimental | suíte `loader` |

## 8. Próximos passos

1. Transferir a reconstrução ao repositório `nwmwkskw905-collab/port-winlator-ios` (pacote
   `phase02-reconstructed-poc.tar.gz`, sem `.git`).
2. Executar `Actions → iOS Runtime PoC (unsigned IPA) → Run workflow` (runner macOS).
3. Assinar/instalar a IPA no iPhone 13 pelo processo habitual (a IPA sai **não assinada**).
4. Abrir o app, rodar `Run All Tests`, exportar o relatório e trazê-lo de volta
   (Arquivos › No meu iPhone › Winlator PoC, ou `xcrun devicectl device copy from`).
5. Só então classificar cada capacidade como `CONFIRMADA EM DISPOSITIVO FÍSICO`.

## 9. Como reproduzir (host, sem Apple)

```sh
cd ios
sh tools/build_host_harness.sh        # build + ctest + suíte + relatório de host
sh tools/build_aarch64_cross.sh       # cross + qemu (AARCH64_QEMU)
python3 tools/generate_xcodeproj.py   # regenera o .xcodeproj
python3 tools/validate_xcodeproj.py   # validação estrutural (34 checagens)
sh tools/build_ios.sh both            # tenta o build Apple; sem toolchain: IOS_BUILD=UNTESTED
sh tools/collect_evidence.sh          # regenera toda a evidência deste relatório
```
