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

1. ~~Transferir a reconstrução~~ e ~~executar o workflow~~ — **feitos**: a execução 01 do CI Apple
   ocorreu e está registrada na §10 (dois bloqueadores, corrigidos no pacote
   `phase02-apple-ci-fix-01.tar.gz`).
2. ~~Aplicar a correção 01~~ e ~~reexecutar o workflow~~ — **feitos**: a execução 02 ocorreu e
   está registrada na §11 (dois bloqueadores novos, corrigidos no pacote
   `phase02-apple-ci-fix-02.tar.gz`).
3. ~~Aplicar a correção 02~~ e ~~reexecutar~~ — **feitos**: a execução 03 confirmou as correções
   01 e 02 e trouxe o bloqueador macOS×iOS, corrigido no pacote `phase02-apple-ci-fix-03.tar.gz`
   (ver §12).
4. ~~Aplicar a correção 03~~ e ~~reexecutar~~ — **feitos**: a execução 04 ultrapassou
   `darwin_platform.c` e parou em `Phase02Bridge.m`, corrigido no pacote
   `phase02-apple-ci-fix-04.tar.gz` (ver §13).
5. ~~Aplicar a correção 04~~ e ~~reexecutar~~ — **feitos**: a execução 05 confirmou as correções
   01 a 04 (nenhum dos três bloqueadores anteriores reapareceu, e o build passou da compilação
   para a **linkedição**), onde surgiu o defeito de composição de plataforma corrigido no pacote
   `phase02-apple-ci-fix-05.tar.gz` (ver §14).
6. Aplicar a correção 05 e reexecutar. Esperado: as cinco pré-checagens em 0 (4b–4f), portão PASS,
   e a linkedição `iphoneos` **não voltando a falhar** com
   `"___clear_cache", referenced from _linux_icache_flush in linux_platform.o`. Não há afirmação
   de que o build completo ou a IPA sairão: se o SDK revelar outro bloqueador, ele será reportado
   sem mascaramento.
7. Assinar/instalar a IPA no iPhone 13 pelo processo habitual (a IPA sai **não assinada**).
8. Abrir o app, rodar `Run All Tests`, exportar o relatório e trazê-lo de volta
   (Arquivos › No meu iPhone › Winlator PoC, ou `xcrun devicectl device copy from`).
9. Só então classificar cada capacidade como `CONFIRMADA EM DISPOSITIVO FÍSICO`.

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

---

## 10. Correções do primeiro CI Apple real (execução 01)

A primeira execução no runner macOS/Apple ARM64 terminou **vermelha** por **dois bloqueadores
independentes**. O caminho C/CMake, porém, executou de verdade: `AppleClang 15.0.0.15000309`,
os três alvos construídos, `warnings: 0`, `platform=darwin`, `page_size=16384`, `isa=aarch64` e
`== 91 checks, 0 failures ==`. Esses dados são preservados como evidência
**`MACOS_RUNNER_ARM64`** (transcrição em `evidence/ci_run01_macos_runner_arm64.txt`) e **não** são
`IPHONE_PHYSICAL`: `page_size` de 16 KiB, x18 reservado e JIT executando no runner **não** provam
o mesmo no iPhone. `IPHONE_VALIDATION_PENDING` permanece.

### 10.1 Bloqueador A — `project.pbxproj` inválido

`xcodebuild: error: Unable to read project 'WinlatorPhase02.xcodeproj'` /
`The project 'WinlatorPhase02' is damaged and cannot be opened due to a parse error` /
`NSCocoaErrorDomain Code=3840` / exit code 74.

**Causa raiz (reproduzida localmente, sem Mac).** O gerador emitia, em **31** `PBXFileReference`:

```
sourceTree = <group>;          ← sem aspas
```

Na gramática OpenStep do `pbxproj`, `<...>` é um literal de **dados** (bytes hexadecimais), não
uma string. O parser consome `<`, tenta ler `group` como hexadecimal e aborta em `g`:

```
line 31, column 173: invalid character 'g' inside a '<...>' data token
```

Ou seja: o arquivo **não era um plist válido**, exatamente o "damaged ... parse error" do Xcode.
O `Code=3840` / `"JSON text did not start with array or object..."` é a tentativa secundária do
Xcode de ler o mesmo arquivo por outro caminho — sintoma, não causa.

**Correção na causa, no gerador** (nenhuma edição manual do `project.pbxproj`):

* `tools/generate_xcodeproj.py` — `file_ref()` passou a emitir `sourceTree` por `quote()`;
* `quote()` — passou a citar tudo que não seja estritamente seguro sem aspas
  (`A-Za-z0-9_$+/:.-`), incluindo `<`, `>`, `=` e string vazia;
* o gerador agora **parseia o próprio resultado** com `tools/openstep_plist.py` e **se recusa a
  escrever** um projeto inválido (`PBXPROJ_PARSE=OK` quando passa) — essa classe de defeito não
  chega mais ao CI.

O arquivo regenerado difere do anterior em **31 linhas**, todas a mesma troca
`sourceTree = <group>;` → `sourceTree = "<group>";`. Nenhuma outra mudança estrutural: mesmos 69
identificadores, mesmas 356 linhas.

### 10.2 Bloqueador B — `tee` para diretório de evidências inexistente

`tee: ../Documentation/evidence/ci_macos_unit_tests.log: No such file or directory`, com os
testes **passando** (`== 91 checks, 0 failures ==`). Causa: o comando
`(cd build/host && ./phase02_unit_tests) 2>&1 | tee ../Documentation/evidence/...` avaliava o
caminho relativo **depois** do `cd`, apontando para `ios/build/Documentation/...`, que não existe;
com `set -o pipefail`, o status do `tee` virou o status do step.

**Correção:** `EVIDENCE_DIR = ${{ github.workspace }}/ios/Documentation/evidence` (derivado do
workspace), `mkdir -p "$EVIDENCE_DIR"` **antes** de qualquer `tee`, e todos os `tee` com caminho
absoluto. `set -o pipefail` foi **mantido** (7 ocorrências) e nenhum código de saída é escondido;
os `|| true` restantes são apenas em `echo` informativos (versões de SDK, contagem de warnings,
`lipo`/`nm`/`plutil`), nunca em passo crítico.

### 10.3 Validador atualizado (e o que continua sendo o portão)

`tools/validate_xcodeproj.py` passou de 34 para **62 checagens** e agora inclui: parse pela
gramática OpenStep, ausência de token `<...>` sem aspas, ausência de BOM/marcadores de conflito/
JSON acidental, `isa` string em todo objeto, resolução de **todas** as referências do grafo
(`targets`, `mainGroup`, `productRefGroup`, `buildPhases`, `buildConfigurations`, `fileRef`,
`productReference`, `buildConfigurationList`), filiação de cada fonte à fase `Sources` e coerência
do `BlueprintIdentifier` do scheme com o alvo.

Controle negativo executado: apontado ao `project.pbxproj` que o Xcode rejeitou, o validador
**falha** com linha e coluna (`line 31, column 173 ...`), comprovando que a classe de erro passou
a ser detectada fora do macOS.

**O validador Python não substitui o parser da Apple.** O portão autoritativo continua sendo, no
macOS:

```
xcodebuild -project ios/WinlatorPhase02.xcodeproj -list
```

O CI falha nesse passo **antes** de qualquer build `iphoneos` (passo 5 de 13), e `tools/build_ios.sh`
foi alinhado ao mesmo comportamento.

### 10.4 Ordem obrigatória do CI (aplicada)

1. checkout · 2. ambiente Apple (logado) · 3. gerar/regenerar `.xcodeproj` · 4. validação
estrutural · **5. `xcodebuild -list` (portão: reprovar aqui encerra o job)** · 6. build
`iphoneos` · 7. localizar `.app` · 8. validar bundle (Info.plist, executável, arquitetura,
bundle id, `UIDeviceFamily`) · 9. `Payload/` · 10. IPA não assinada · 11. inspecionar IPA ·
12. SHA-256 · 13. upload do artifact.

### 10.5 Estado da regressão após as correções

| Verificação | Resultado |
| --- | --- |
| Host x86-64 (rebuild limpo) | 0 warnings / 0 errors · ctest 2/2 · **91 checks, 0 falhas** · suíte `pass=52 fail=0 blocked=0 unsupported=2 not_applicable=2 summary=PASS` |
| AArch64 (qemu) | 0 warnings · **90 checks, 0 falhas** · suíte `pass=53 fail=0 unsupported=2 not_applicable=1 summary=PASS` |
| Preflight do `.xcodeproj` | **62 checagens, 0 falhas** (+ controle negativo detectando o arquivo antigo) |
| Parse OpenStep do `pbxproj` | `PLIST_SYNTAX=PASS` (69 objetos) |
| Fase 04 | 86 arquivos antes / 86 depois, byte-idênticos fora dos 2 `.txt` de inventário |
| Toolchain Apple | `IOS_BUILD=UNTESTED REASON=NO_APPLE_TOOLCHAIN` (esperado) |

Nenhum teste foi alterado para forçar `PASS`; nenhuma capacidade foi declarada sem execução.

---

## 11. Correção 02 — bloqueadores do segundo CI Apple (execução 02)

A segunda execução real do workflow **confirmou a Correção 01** (o Xcode leu o projeto, o portão
`xcodebuild -list` passou, o `tee` da evidência funcionou) e trouxe **dois bloqueadores novos**:
um no compile do `iphoneos` e um limite de plataforma reportado como falha. Duas coisas mais
importantes aconteceram no mesmo passo: o caminho C/CMake **executou de verdade em Apple ARM64**
(`AppleClang 15.0.0.15000309`, `platform=darwin`, `page_size=16384`, `isa=aarch64`,
`91 checks, 0 failures`, `warnings: 0`) e **nenhum registro de RuntimeCore falhou** — a única
falha foi o teste de caminho profundo descrito em §11.2. Transcrição em
`evidence/ci_run02_macos_runner_arm64.txt`, rotulada **`MACOS_RUNNER_ARM64`**, nunca
`IPHONE_PHYSICAL`.

### 11.1 Bloqueador C — `'sys/random.h' file not found` no SDK iphoneos

```
ios/RuntimeCore/src/runtime_dual_mapping.c:31:10: fatal error: 'sys/random.h' file not found
    #include <sys/random.h>
```

**Causa raiz.** O include estava sob `#if defined(__APPLE__)` — isto é, o header Linux/glibc era
puxado **exatamente na plataforma que não o possui** — e o arquivo **não usava nenhuma função
dele**: `<sys/random.h>` era um include morto de uma revisão anterior, em que a geração do nome
do objeto de memória compartilhada ainda não existia. O SDK `iphoneos` não traz `sys/random.h`;
a API correta no Darwin/iOS é **`arc4random_buf(3)`, declarada em `<stdlib.h>`**, que existe em
todas as plataformas Apple, não exige entitlement e é o CSPRNG recomendado.

**Correção — na camada de plataforma, como pede a arquitetura:**

| Arquivo | Mudança |
| --- | --- |
| `RuntimeCore/include/runtime_platform.h` | `rt_platform_t` ganhou o membro `random_bytes`; declaradas `rt_platform_random_bytes()` e `rt_platform_unique_shm_name()` |
| `RuntimeCore/src/linux_platform.c` | `linux_random_bytes()`: `getrandom(2)` (com laço em `EINTR`) e fallback **apenas em `ENOSYS`** para `/dev/urandom`; qualquer outro erro é reportado |
| `RuntimeCore/src/darwin_platform.c` | `darwin_random_bytes()`: **`arc4random_buf`**; fora do Apple devolve `ENOTSUP` (o backend não finge) |
| `RuntimeCore/src/runtime_memory.c` | despacho (`rt_platform_random_bytes`) e o compositor de nome **único**, usado pelos dois backends |
| `RuntimeCore/src/runtime_dual_mapping.c` | include morto removido; o objeto passa a se chamar `/rt_dual_<pid>_<16 hex>` |
| `RuntimeCore/src/runtime_ipc.c` | o mesmo defeito existia aqui (`/rt_shm_<pid>`, previsível e sobrevivente a um crash): agora `/rt_shm_<pid>_<16 hex>` |

O nome do objeto era `pid` + contador **previsível**: mesmo com `O_EXCL`, outro processo pode
pré-criar o nome, e um objeto deixado por um `crash` transforma um `EEXIST` em falha espúria.
Agora são 64 bits do CSPRNG da plataforma, e **se não houver fonte aceitável a função falha com o
errno real** — não existe fallback silencioso para um nome adivinhável. Um retorno só de zeros
(probabilidade 2⁻⁶⁴) é tratado como fonte inválida, não como sucesso.

**Auditoria de includes (§ pedido).** `tools/audit_apple_includes.py` varre os 17 arquivos que
entram no alvo Apple e reporta headers que o SDK `iphoneos` **comprovável e especificamente** não
possui quando não estão sob guarda de plataforma. Resultado no estado corrigido:

```
UNGUARDED_LINUX_INCLUDES=0 (no Linux-only header reaches the Apple build)
```

O controle negativo foi executado: apontado ao arquivo da execução 02, o auditor acusa
`RuntimeCore/src/runtime_dual_mapping.c:31 sys/random.h` — a mesma linha que quebrou o build.

Tabela completa da revisão (includes de sistema por arquivo):

| Header | Onde | Situação no alvo Apple |
| --- | --- | --- |
| `sys/random.h` | `runtime_dual_mapping.c` | **ausente no SDK iphoneos** — era o defeito; removido |
| `sys/auxv.h`, `sys/epoll.h` | `linux_platform.c`, `runtime_ipc.c` | corretamente sob `__linux__` / `#if defined(__linux__)` |
| `sys/event.h` (kqueue) | `runtime_ipc.c` | existe em todas as plataformas Apple; sob `__APPLE__` |
| `libkern/OSCacheControl.h` | `darwin_platform.c` | existe; já protegido por `__has_include` |
| `sys/statvfs.h` | `runtime_filesystem.c` | presente no SDK macOS (compilou no runner). Passou a ter `__has_include` com fallback `statfs`/`sys/mount.h` para SDKs que não o tragam — o caminho testado continua o mesmo |
| `sys/mman.h`, `pthread.h`, `unistd.h`, `errno.h`, `fcntl.h`, `signal.h`, `setjmp.h`, `stdatomic.h`, `sys/stat.h`, `sys/socket.h`, `sys/un.h`, `sys/time.h` | vários | POSIX/BSD comuns às duas plataformas |

**Conclusão da auditoria:** nenhum outro header **específico de Linux** chega ao build Apple; o
único problema dessa classe era o include morto, e a auditoria agora roda no CI (passo 4b) antes
do portão, para que a próxima ocorrência falhe em segundos com arquivo e linha em vez de dentro do
`clang`.

### 11.2 Bloqueador D — `fs.deep_paths` no runner macOS: limite lido como falha

```
TEST=fs.deep_paths STATUS=FAIL DETAIL=errno=63 (File name too long) at depth=110
records=56 pass=53 fail=1 blocked=0 unsupported=0 not_applicable=2 summary=FAIL
```

**Causa raiz.** O teste pedia uma profundidade **fixa de 110 níveis** — número obtido num host
Linux, onde `PATH_MAX` é 4096. No Darwin `PATH_MAX` é **1024** e o diretório temporário do runner
(`/var/folders/…/T/phase02_XXXXXX`) já ocupa 50–60 caracteres. Cada nível custa 9 caracteres
(`"/d%07u"`), então 110 níveis exigem ~1045 caracteres de caminho: acima do limite **real** da
plataforma. O `ENAMETOOLONG` (errno 63 no Darwin, 36 no Linux) era, portanto, uma **capacidade
legítima da plataforma sendo apresentada como defeito da implementação**. O defeito estava no
teste, não no RuntimeCore.

**Correção — o teste passa a MEDIR, e distingue os três desfechos:**

| Componente | O que faz |
| --- | --- |
| `rt_fs_limits_query()` (novo, `runtime_filesystem.h`) | devolve `path_max` (`pathconf(_PC_PATH_MAX)`, com queda para `PATH_MAX` de `limits.h` e registro de qual foi usado), `name_max` e `path_cap` (o buffer interno da própria implementação) |
| `phase02_measure_depth()` (`phase02_harness.c`) | bisseção sobre `rt_fs_deep_paths()`: crescimento exponencial (8, 16, … até 512) para achar o intervalo, depois bisseção. Mede a capacidade **real**, não uma premissa |
| classificação | **PASS** — capacidade criada e removida + teto no limite documentado da plataforma (`ENAMETOOLONG`), com os números no detalhe; **PASS** — nenhum teto dentro do limite sondado, registrado explicitamente como *"não é alegação de profundidade ilimitada"*; **BLOCKED** — nenhuma profundidade mensurável, ou recusa por `EPERM`/`EACCES`/`ENOSPC`/`EDQUOT`/`EROFS`, com o errno real; **FAIL** — erro inesperado depois de um nível bem-sucedido, ou seja: defeito de implementação |
| `rt_fs_deep_paths()` (biblioteca) | passa a **desfazer o que criou também no caminho de falha** (antes retornava deixando a árvore parcial, e a sonda seguinte falharia com `EEXIST` em vez do limite real); o comprimento do nível só é confirmado depois de `mkdir` bem-sucedido |

Números medidos agora, com o mesmo código:

| Ambiente / geometria | Registro |
| --- | --- |
| Host Linux, raiz curta (`/tmp`) | `PASS capacity: depth up to 224 (2035 chars) … ceiling at depth=225 errno=36 … PATH_MAX=4096[pathconf] cap=2048` |
| Host Linux, raiz de 822 chars (simulação de geometria) | `PASS … depth up to 135 (2033 chars) … ceiling at 136 errno=36` |
| Host Linux, raiz de 2098 chars | `BLOCKED … no depth measurable … errno=36` |
| Host Linux, workdir é arquivo comum | `BLOCKED … errno=20 (Not a directory)` |
| Host Linux, workdir inexistente | `BLOCKED … errno=2 (No such file or directory)` |
| AArch64 (qemu) | `PASS capacity: depth up to 224 (2035 chars) … errno=36` |

No **runner macOS** espera-se `PATH_MAX=1024[pathconf]`, raiz `…/T/phase02_XXXXXX` (~55 chars) e,
portanto, capacidade ≈ **107 níveis** (~1018 caracteres) com teto em `ENAMETOOLONG` — registrado
como limite da plataforma, exatamente como a execução 02 deveria ter registrado. Os três desfechos
foram exercitados em laboratório (tabela acima, evidência
`evidence/host_fs_deep_paths_classification.txt`), rotulados como **simulação de geometria de
caminho em Linux**, não como resultado Darwin.

### 11.3 Resultados preservados da execução 02 (`MACOS_RUNNER_ARM64`)

Preservados **como estão** e classificados apenas como runner macOS: `platform=darwin`,
`page_size=16384`, `isa=aarch64`, `91 checks, 0 failures`, W^X, dual mapping RW/RX, `MAP_JIT` probe,
`pthread_jit_write_protect_np` presente, JIT → 42, rewrite + icache → 4242, x18 reservado,
threads/TLS/mutex/condition/atomics, signals, AF_UNIX, `SCM_RIGHTS`, POSIX shm, kqueue/kevent,
loader. Nada disso é `IPHONE_PHYSICAL`; `page_size` de 16 KiB no runner **não** prova o tamanho de
página no iPhone.

### 11.4 Estado da regressão após a Correção 02

| Verificação | Resultado |
| --- | --- |
| Host x86-64, rebuild limpo | 0 warnings / 0 errors · ctest **2/2** · **119 checks, 0 falhas** · suíte `records=56 pass=52 fail=0 blocked=0 unsupported=2 untested=0 not_applicable=2 summary=PASS` |
| AArch64 (qemu), rebuild limpo | 0 warnings · **118 checks, 0 falhas** · `pass=53 fail=0 unsupported=2 not_applicable=1 summary=PASS` |
| Parse OpenStep do `pbxproj` | `PLIST_SYNTAX=PASS` (69 objetos) — Correção 01 intacta |
| Preflight do `.xcodeproj` | **62 checagens, 0 falhas** |
| Auditoria de includes Apple | `UNGUARDED_LINUX_INCLUDES=0` (+ controle negativo detectando o defeito real) |
| Fase 04 | 86 arquivos antes / 86 depois, byte-idênticos fora dos 2 `.txt` de inventário |
| Toolchain Apple | `IOS_BUILD=UNTESTED REASON=NO_APPLE_TOOLCHAIN` (esperado aqui) |

**Delta de checagens unitárias (91 → 119 no host; 90 → 118 no AArch64):** são **+28 checagens
novas**, todas exigidas por esta correção — **+15** em `test_platform_randomness()` (fonte de
aleatoriedade disponível e distinta a cada chamada, comprimento zero aceito, `NULL` recusado com
`EINVAL`, nome único/prefixado/imprevisível, buffer pequeno recusado com `ENAMETOOLONG`) e **+13**
no grupo de filesystem (7 → 20: limites consultáveis e coerentes, profundidade segura dentro do
limite medido, profundidade absurda recusada com `ENAMETOOLONG`, **ausência de resíduo após uma
sonda falha**, e `NULL`/profundidade 0/limites `NULL` recusados com `EINVAL`). Nenhuma checagem foi
removida ou afrouxada — as duas que aparecem como removidas no diff foram reescritas e continuam
presentes. Os CHECKs do fonte agora coincidem exatamente com os executados (119), porque nenhum
deles vive em ramo morto; a diferença de −1 entre host e AArch64 continua sendo a asserção
específica de ISA.

### 11.5 O que a Correção 02 **não** prova

* O build `iphoneos` **não** foi confirmado: a execução 02 parou em `runtime_dual_mapping.c` e este
  ambiente não tem toolchain Apple. O que se pode afirmar é que **a causa demonstrada daquela
  parada foi eliminada** e que a classe de erro passou a ser detectada antes do compilador.
* O `.app`, o `Payload/` e a IPA continuam **não produzidos** neste ambiente.
* `IPHONE_VALIDATION_PENDING` permanece: nada aqui é validação de iPhone.
* A auditoria de includes é **estática**: quem decide é o compilador da Apple.

---

## 12. Correção 03 — macOS e iOS são alvos diferentes (execução 03)

A terceira execução real do workflow confirmou as Correções 01 e 02 (o portão passou, a suíte
macOS rodou, o `sys/random.h` não reincidiu) e trouxe o próximo bloqueador, de outra natureza:

```
ios/RuntimeCore/src/darwin_platform.c:165:5: error:
    'pthread_jit_write_protect_np' is unavailable: not available on iOS
    iPhoneOS17.5.sdk/usr/include/pthread.h: '... has been explicitly marked unavailable here'
```

### 12.1 Causa raiz

O código tratava **`__APPLE__` como uma única plataforma**. `pthread_jit_write_protect_np` existe
no `pthread.h` do macOS e é **marcada como indisponível pelo SDK do iPhoneOS**. Três lugares
carregavam a mesma confusão `__APPLE__ && __aarch64__`:

| # | Lugar | Defeito |
| --- | --- | --- |
| 1 | `darwin_jit_write_protect_set()` | a **chamada** — o erro de compilação do CI #3 |
| 2 | `darwin_capabilities()` | o **bit `RT_CAP_JIT_WP_NP`**, anunciado também no iOS. Como o harness lê exatamente esse bit, o iOS declararia a capacidade **presente** — uma capacidade reivindicada que o alvo não pode exercer legalmente |
| 3 | `rt_jit_probe_map_jit()` | usa `MAP_JIT`, que **ambos** os SDKs definem; segue sob `defined(MAP_JIT)` (degrada para `ENOTSUP` se algum SDK não o tivesse) e continua sendo **sonda de execução**, porque no iOS quem decide é o entitlement, não o símbolo |

### 12.2 O que foi feito — e o que deliberadamente **não** foi

* **Não** se usou disponibilidade por versão (`@available`, weak linking): a restrição é uma
  propriedade da API **para aquele target**, não da versão do sistema; nenhum teste de versão
  torna a chamada legal.
* **Não** se removeu a chamada: ela continua existindo no macOS, agora gateada por alvo.
* **Não** se declarou a capacidade onde ela não pode ser chamada.
* O alvo é derivado de **`<TargetConditionals.h>`** — `TARGET_OS_OSX`, `TARGET_OS_IPHONE`,
  `TARGET_OS_SIMULATOR` — com queda conservadora: num build Apple sem esse header o alvo é
  `apple-unknown` e a API **não** é usada (assumir macOS seria recriar o defeito).

```
RT_APPLE_TARGET_NONE | MACOS | IPHONE_DEVICE | IPHONE_SIMULATOR | UNKNOWN_APPLE
RT_APPLE_HAS_JIT_WRITE_PROTECT  ==  (RT_APPLE_TARGET == RT_APPLE_TARGET_MACOS)
```

A chamada e o bit de capacidade são gateados **por essa macro**, e o relatório passa a imprimir
`apple_target=` no cabeçalho, para que "resultado de macOS", "resultado de iOS" e "resultado de
host" nunca mais possam ser confundidos na evidência.

### 12.3 Semântica adotada (a que a suíte distingue)

| Alvo / situação | Registro | Por quê |
| --- | --- | --- |
| macOS, hook disponível e responde 0 | `PASS` | API existe para o alvo e foi **chamada com sucesso** |
| macOS, hook existe mas recusa | `BLOCKED` | disponível, porém barrada — errno real no detalhe |
| **iOS device / iOS simulator** | `NOT_APPLICABLE` | a API é **indisponível para o alvo** (o SDK a marca assim): não é capacidade do port, não é defeito, e **não pode** virar `PASS` |
| Linux (ou plataforma sem a API) | `UNSUPPORTED` | a plataforma não tem essa API |
| build Apple de alvo desconhecido | `UNSUPPORTED` | conservador: não alega ser iOS nem macOS |

A distinção é testável **em qualquer host**: `phase02_classify_write_protect(has_capability,
probe_result, apple_target)` é uma função pura, e os quatro desfechos (mais dois casos de borda)
são verificados por testes unitários com entradas sintéticas — inclusive os ramos de iOS, que de
outra forma só seriam exercitados num aparelho.

`jit.map_jit_probe` continua sendo sonda de execução, com o alvo no detalhe e um aviso explícito:
no iOS um simulador aceita `MAP_JIT` mais facilmente que um aparelho, então **essa linha sozinha
nunca prova comportamento de dispositivo**, e uma recusa com `EPERM` é registrada como
capacidade-não-concedida (entitlement), nunca como defeito do port.

### 12.4 Auditoria de APIs Apple (além da de headers)

`tools/audit_apple_apis.py` (novo, roda no CI como passo **4c**, antes do portão) procura
símbolos **que compilam no macOS e o SDK do iPhoneOS recusa**: `pthread_jit_write_protect_np`,
`pthread_jit_write_protect_supported_np`, `proc_pidinfo`, `proc_pidpath`, `proc_name`,
`proc_listpids`, `proc_pid_rusage`, `setiopolicy_np`. Regra: a referência precisa estar sob uma
guarda que **exclua iOS por alvo** (`RT_APPLE_HAS_JIT_WRITE_PROTECT`, `TARGET_OS_OSX`,
`!TARGET_OS_IPHONE`, `!TARGET_OS_SIMULATOR`) — `defined(__APPLE__)` **não** conta, porque é
verdadeiro no iOS também. O auditor ignora literais de string, para não confundir *nomear* uma
API numa mensagem com *chamá-la*.

| Verificação | Resultado |
| --- | --- |
| Estado corrigido | `UNGUARDED_MACOS_ONLY_APIS=0` |
| Controle negativo (arquivo da CI #3) | acusa **`RuntimeCore/src/darwin_platform.c:165 pthread_jit_write_protect_np`** — a linha exata do erro |
| Auditoria de headers (Correção 02, preservada) | `UNGUARDED_LINUX_INCLUDES=0` |

Além disso, a lógica de mapeamento de alvo é exercitada por uma **simulação das macros do SDK**
(`evidence/host_apple_target_mapping_simulation.txt`): para cada conjunto de macros que o SDK
definiria, o alvo derivado e a barreira da API são verificados em tempo de compilação
(`#error`), com controle negativo que recusa um iOS capaz de chamar a API. É explicitamente
rotulado como **simulação de preprocessador em host Linux**, não como build Apple.

### 12.5 Testes acrescentados

| Grupo | Checagens | O que provam |
| --- | --- | --- |
| `test_apple_target()` | **+6** | nome não vazio; código e nome concordam; código é um valor documentado; alvo `none` exatamente quando o host não é Apple; a barreira segue o **alvo** e não `__APPLE__`; **a capacidade é anunciada exatamente onde o alvo pode chamar a API** (a invariante que a CI #3 quebrou) |
| `test_write_protect_semantics()` | **+7** | os quatro desfechos (PASS/BLOCKED/NOT_APPLICABLE/UNSUPPORTED) e dois casos de borda (simulador; Apple de alvo desconhecido), incluindo a garantia de que **nenhuma entrada classifica como PASS sem chamada bem-sucedida** |

Os seis primeiros são **igualdades** entre fatos observáveis, portanto não são vacuosos em
plataforma alguma; os sete últimos usam entradas sintéticas, então os ramos de iOS são exercitados
mesmo num host Linux.

### 12.6 Estado da regressão após a Correção 03

| Verificação | Resultado |
| --- | --- |
| Host x86-64, rebuild limpo | 0 warnings / 0 errors · ctest **2/2** · **132 checks, 0 falhas** · suíte `records=56 pass=52 fail=0 blocked=0 unsupported=2 untested=0 not_applicable=2 summary=PASS` |
| AArch64 (qemu), rebuild limpo | 0 warnings · **131 checks, 0 falhas** · `pass=53 fail=0 unsupported=2 not_applicable=1 summary=PASS` |
| Cabeçalho do relatório | `platform=linux page_size=4096 apple_target=none` (o alvo passa a ser visível) |
| `jit.write_protect_np` no host | `UNSUPPORTED … (target=none, capability bit absent)` — como antes, agora com o alvo explícito |
| Correção 01 (pbxproj/portão/evidência/pipefail) | `PLIST_SYNTAX=PASS` (69 objetos) · preflight **62 checagens, 0 falhas** · `EVIDENCE_DIR` absoluto · 6× `mkdir -p` · `pipefail` |
| Correção 02 (randomness/limites) | `UNGUARDED_LINUX_INCLUDES=0` · `arc4random_buf` no Darwin, `getrandom` no Linux · `fs.deep_paths` medido por bisseção |
| Fase 04 | 86 arquivos, byte-idênticos fora dos 2 `.txt` de inventário · 0 alterações no git |
| Toolchain Apple | `IOS_BUILD=UNTESTED REASON=NO_APPLE_TOOLCHAIN` (esperado aqui) |

### 12.7 O que a Correção 03 **não** prova

* Não prova que o build `iphoneos` completa: ele parou em `darwin_platform.c` na execução 03 e
  este ambiente não tem toolchain Apple. Prova-se que **a causa demonstrada foi eliminada**, que a
  classe de erro passou a ser detectada antes do compilador e que o iOS **não** reivindica mais a
  capacidade de write-protect.
* Não prova nada sobre o iPhone 13: `IPHONE_VALIDATION_PENDING` permanece. Mesmo com o build
  completo e a IPA gerada, JIT no aparelho continua dependendo de assinatura/entitlements, e a
  suíte responde `NOT_APPLICABLE`/`BLOCKED` com o motivo real, nunca `PASS` por otimismo.
* Um resultado de `MAP_JIT` no **simulador** não vale para o **aparelho**.

---

## 13. Correção 04 — chamada sem declaração visível (execução 04)

A quarta execução real confirmou as Correções 02 e 03 (nem `sys/random.h`, nem
`pthread_jit_write_protect_np unavailable on iOS` reapareceram) e o build `iphoneos` chegou ao
alvo do app, onde parou em um defeito de **interface**:

```
ios/RuntimePoC/Phase02Bridge.m:75:69: error: call to undeclared function 'rt_jit_isa';
    ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
    warning: format specifies type 'char *' but the argument has type 'int' [-Wformat]
1 warning and 1 error generated.   ** BUILD FAILED **
```

### 13.1 Causa raiz exata

`rt_jit_isa` **existe** e **está declarada**:

| Onde | O quê |
| --- | --- |
| `RuntimeCore/src/runtime_jit.c:20` | `const char *rt_jit_isa(void)` — a **implementação** |
| `RuntimeCore/include/runtime_jit.h:31` | `const char *rt_jit_isa(void);` — a **declaração** |
| `RuntimePoC/Phase02Bridge.m:75` | a **chamada**, num arquivo que incluía só `phase02_harness.h`, `phase02_log.h` e `runtime_platform.h` |

O header que declara a função nunca foi incluído ali. Sem protótipo visível, o compilador
assume `int` (C89) ou recusa (C99+) — daí o **erro**, e daí o **warning de formato**: `%s`
recebendo o `int` que o compilador inferiu em vez do `const char *` real. Nada estava errado na
função, no tipo de retorno ou no diagnóstico de ISA: era só invisibilidade de declaração.

**A correção não foi** — e não poderia ser — uma declaração ad hoc na bridge, um cast para
calar o `%s`, ou a remoção do diagnóstico de ISA.

### 13.2 Correção na camada arquitetural

O sintoma revelou um problema maior: a camada de app alcançava headers do RuntimeCore. A regra
passou a ser explícita e verificável:

> **`RuntimePoC/*.m` e o bridging header falam APENAS com a API de Diagnostics
> (`phase02_harness.h`, `phase02_log.h`). Tudo que o app precisa do RuntimeCore é exposto pela
> API do harness.**

| Arquivo | Mudança |
| --- | --- |
| `Diagnostics/include/phase02_harness.h` | nova `phase02_platform_summary(char *out, size_t capacity)` — o app deixa de precisar de `runtime_platform.h`/`runtime_jit.h` |
| `Diagnostics/src/phase02_harness.c` | implementação (a camada que já vê todos os fatos do RuntimeCore compõe a string) |
| `RuntimePoC/Phase02Bridge.m` | inclui só Diagnostics; `platformDescription` usa o resumo e `stringWithUTF8String:` — sem `%s` para o chamador errar, sem cast, **com o ISA preservado**; inclui `<stdio.h>`/`<string.h>` que ela de fato usa |
| `RuntimePoC/Phase02-Bridging-Header.h` | só a camada Diagnostics (os headers de RuntimeCore saíram) |
| `RuntimePoC/main.c` | usa o mesmo resumo no cabeçalho (`# host: …`) — CLI e app imprimem a mesma linha |

O resumo produzido é:

```
platform=linux page_size=4096 isa=x86-64 apple_target=none
```

### 13.3 Auditoria de interfaces (nova)

`tools/audit_interfaces.py` roda no CI como passo **4d** e verifica três coisas:

1. **declarações implícitas** — uma função do projeto (`rt_*`, `phase02_*`, `Phase02*`) chamada
   num arquivo onde nenhum header alcançável a declara, e que não é definida acima da chamada;
2. **violação de camadas** — arquivos da camada de app incluindo header do RuntimeCore;
3. **definições sem declaração** — função não-`static` definida em `.c`/`.m` que header algum
   declara (portanto impossível de chamar de outro arquivo sem protótipo).

O auditor resolve `#include`/`#import` recursivamente pelos **mesmos diretórios de busca do
projeto** (`HEADER_SEARCH_PATHS` do gerador), então um arquivo que passa aqui compila do mesmo jeito.

| Verificação | Resultado |
| --- | --- |
| Estado corrigido | `INTERFACE_AUDIT=0` — 16 fontes, nenhuma chamada sem declaração, nenhuma violação de camada |
| **Controle negativo** (bridge do commit `e924199`) | acusa **`Phase02Bridge.m:65: 'rt_jit_isa' has no visible declaration`** *e* a violação de camada (`includes RuntimeCore header 'runtime_platform.h'`) |

### 13.4 Validação com compilador real, sem Apple toolchain

`tools/check_bridge_syntax.sh` (passo **4e** no CI) roda `clang` sobre `Phase02Bridge.m` com a
política de warnings do projeto, `-Werror=implicit-function-declaration` e um **stub de
Foundation** em `tools/fake_foundation/` — que declara apenas o que a bridge usa, com
`+stringWithFormat:` marcado com o mesmo tipo de formato que o Foundation real usa
(`__NSString__`), de modo que `%@` é válido e `%s` com `int` é diagnosticado igual.

| Verificação | Resultado |
| --- | --- |
| Estado corrigido | `BRIDGE_SYNTAX=PASS` — **0 warnings, 0 errors** |
| **Controle negativo** (bridge do `e924199`) | reproduz **os diagnósticos da CI #4, no mesmo arquivo, linha e coluna**: `Phase02Bridge.m:75:69 'call to undeclared function rt_jit_isa'` + `format specifies type char * but the argument has type int` |
| Achado extra do controle negativo | `strcmp` em `Phase02Bridge.m:41` sem protótipo visível — um defeito **latente** que só compilava na Apple porque `Foundation.h` trazia `<string.h>` de carona. Corrigido no mesmo passe, com includes explícitos |

Isso é uma pré-checagem, **não** a autoridade: quem decide continua sendo o compilador da Apple
(o passo 6 do workflow, logo depois). O stub nunca é usado pelo app — só por este script.

### 13.5 Testes acrescentados

`test_platform_summary()` — **+11 checagens**: o resumo é produzido, o comprimento devolvido bate
com a string, contém `platform=`, `page_size=`, `isa=` e `apple_target=`, e os valores são os
**reais** (`rt_platform_name()`, `rt_jit_isa()`), não constantes copiadas; mais os negativos:
buffer curto, buffer de 1 byte (que precisa ser **limpo**, não deixado pela metade), `NULL` e
capacidade 0 recusados.

### 13.6 Regressão após a Correção 04

| Verificação | Resultado |
| --- | --- |
| Host x86-64, rebuild limpo | 0 warnings / 0 errors · ctest **2/2** · **145 checks, 0 falhas** · suíte `records=56 pass=52 fail=0 blocked=0 unsupported=2 untested=0 not_applicable=2 summary=PASS` |
| AArch64 (qemu), rebuild limpo | 0 warnings · **144 checks, 0 falhas** · `pass=53 fail=0 unsupported=2 not_applicable=1 summary=PASS` |
| Cabeçalho do relatório | `platform=linux page_size=4096 apple_target=none` · `# host: platform=… isa=x86-64 apple_target=none` |
| Auditoria de interfaces (4d) | `INTERFACE_AUDIT=0` (+ controle negativo pegando o defeito real) |
| Sintaxe da bridge (4e) | `BRIDGE_SYNTAX=PASS` 0/0 (+ controle negativo reproduzindo a CI #4) |
| Auditoria de includes (4b, Correção 02) | `UNGUARDED_LINUX_INCLUDES=0` |
| Auditoria de APIs (4c, Correção 03) | `UNGUARDED_MACOS_ONLY_APIS=0` |
| `.xcodeproj` | `PLIST_SYNTAX=PASS` (69 objetos) · preflight **62 checagens, 0 falhas** |
| Correção 01 | `EVIDENCE_DIR` absoluto · `mkdir -p` antes de cada `tee` · `pipefail` · portão `xcodebuild -list` intacto |
| Fase 04 | 86 arquivos, byte-idênticos fora dos 2 `.txt` de inventário · 0 alterações no git |
| Toolchain Apple | `IOS_BUILD=UNTESTED REASON=NO_APPLE_TOOLCHAIN` |

*(Um warning real apareceu durante a regressão e foi corrigido, não silenciado:
`main.c:115: declaration of 'summary' shadows a previous local [-Wshadow]`, resolvido renomeando
a variável local.)*

### 13.7 O que a Correção 04 **não** prova

* Não prova que o build `iphoneos` completa: ele parou em `Phase02Bridge.m` na execução 04 e este
  ambiente não tem toolchain Apple. Prova-se que **a causa demonstrada foi eliminada** e que
  agora existem três verificações estáticas (4b/4c/4d/4e) cobrindo as três classes de falha já
  vistas: header ausente, símbolo indisponível no alvo e declaração invisível.
* Não prova nada sobre o iPhone 13: `IPHONE_VALIDATION_PENDING` permanece, e continua valendo
  que JIT no aparelho depende de assinatura/entitlements — a suíte responde
  `NOT_APPLICABLE`/`BLOCKED` com o motivo real, nunca `PASS` por otimismo.

## 14. Correção 05 — `linux_platform.o` no produto Apple (execução 05)

A quinta execução real confirmou as Correções 01 a 04 — nem `sys/random.h`,
`pthread_jit_write_protect_np unavailable on iOS`, nem `call to undeclared function 'rt_jit_isa'`
reapareceram — e o build `iphoneos` avançou da compilação para a **linkedição**, onde parou:

```
    "___clear_cache", referenced from:
          _linux_icache_flush in linux_platform.o
    ld: symbol(s) not found for architecture arm64
    clang: error: linker command failed with exit code 1
    ** BUILD FAILED **
```

### 14.1 Por que `linux_platform.o` está no link do `iphoneos`

`tools/generate_xcodeproj.py` **descobre** os fontes por glob (`RuntimeCore/src/*.c`, 12 arquivos)
e os coloca todos na `PBXSourcesBuildPhase` do target `WinlatorPhase02` — `linux_platform.c`
incluído. Isso é **intencional** e está documentado em `runtime_platform.h`:

> Both backends are always compiled, so the harness can report what a platform would do without
> pretending that this host is that platform.

O que **não** era intencional: `linux_icache_flush()` chamava `__builtin___clear_cache()` sem
qualquer guarda de plataforma. Em ARM esse builtin **não é inline**: o clang o baixa para uma
chamada externa a `__clear_cache` (Mach-O `___clear_cache`), símbolo que o SDK do iPhoneOS não
fornece. Como o Xcode entrega os objetos **diretamente** ao linker (não há biblioteca estática no
projeto gerado), todo símbolo indefinido do objeto precisa resolver — mesmo que nada chame a
função. **Demonstrado em `Documentation/evidence/clang_clear_cache_lowering.txt`, passo 2:** o
mesmo objeto ligado direto falha (`undefined reference to 'totally_missing_symbol'`), e dentro de
um arquivo `.a` nunca é puxado (`main ran`).

Nada no produto Apple chama o backend Linux: `rt_platform_current()` retorna `&rt_platform_darwin`
sob `__APPLE__` (`runtime_memory.c:129-134`), e a única referência a `linux_icache_flush` em toda a
árvore é o inicializador de `rt_platform_linux`. O linker não estava "mantendo" o objeto por
referência real: ele o recebeu na fase de sources.

### 14.2 Qual das três opções foi escolhida, e por quê

| Opção | Decisão | Motivo |
| --- | --- | --- |
| Excluir `linux_platform.c` do target Apple | **não** | contradiz a regra documentada da camada (os dois backends são sempre compilados para que o harness possa relatar o que a outra plataforma *faria*) e mudaria a composição de ambos os sistemas de build |
| Compilar o arquivo com as implementações Linux **inativas** fora do Linux | **sim** | é exatamente o padrão que o próprio arquivo já usa em `linux_random_bytes()` (`#if defined(__linux__)` … `#else` → `ENOTSUP`); não há símbolo novo, nem shim, nem cabeçalho inventado |
| Reorganizar a abstração de plataforma | não nesta correção | mudança grande, fora do escopo incremental; a regra "implementação de plataforma só compila naquela plataforma" já é verificável por auditoria |

### 14.3 `__clear_cache` — o que foi medido e o que foi preservado

Baixamento real do builtin (Debian clang 19.1.7, sonda sem headers; tabela completa na evidência
`clang_clear_cache_lowering.txt`):

| Alvo | Emitido |
| --- | --- |
| `aarch64-linux-gnu`, `armv7-linux` | `bl __clear_cache` |
| `armv7-apple-ios`, `arm64-apple-macos`, `arm64-apple-ios`, `arm64_32-apple-watchos` | `bl ___clear_cache` |
| `x86_64`/`i386` (Linux, macOS e simulador iOS) | **nenhuma instrução** (icache coerente) |

Preservado e **não** tocado: `darwin_icache_flush()` → `sys_icache_invalidate()`
(`libkern/OSCacheControl.h`, disponível em macOS **e** iOS, fornecido pelo libSystem) — a
implementação correta para Darwin/iOS. Nada de símbolo falso, shim vazio, no-op silencioso,
biblioteca aleatória no linker ou redirecionar Apple para código Linux. Pelo contrário, a única
configuração onde **não** existe implementação honesta (Apple ARM sem
`<libkern/OSCacheControl.h>`) agora **recusa compilar** (`#error`) em vez de gerar um binário que
falha ao ligar ou que pula a sincronização que código gerado exige; o ramo Apple x86 continua
legal porque ali o builtin se expande para *nenhuma instrução*.

### 14.4 Nova auditoria — composição, símbolos e seleção de backend (passo **4f**)

`tools/audit_platform_composition.py` avalia os **condicionais reais** de cada arquivo para cada
configuração de alvo (`linux-x86_64`, `linux-aarch64`, `macos-arm64`, `iphoneos-arm64`,
`iphonesimulator-x86_64`) e reporta:

| Verificação | Resultado |
| --- | --- |
| `TARGET_COMPOSITION` | a lista de fontes do `.xcodeproj` (lida do projeto gerado) e a do CMake conferem com o que existe em disco; nada de `Tests/`, `tools/`, Fase 04 ou `build/` dentro do app; fonte em disco sem target é reportada |
| `APPLE_FORBIDDEN_SYMBOLS` | **0** — nenhum símbolo/header proibido alcançável na configuração Apple (`__builtin___clear_cache` e `__clear_cache` valem para ARM; no x86 o builtin não emite instrução) |
| `LINUX_FORBIDDEN_SYMBOLS` | **0** — nenhum API Apple alcançável no Linux (`sys_icache_invalidate`, `arc4random_buf`, `pthread_jit_write_protect_np`, `kqueue/kevent`, `MAP_JIT`) |
| `BACKEND_SELECTION` | **OK** — `rt_platform_current()` decide por `__APPLE__`/`__linux__`, e cada backend é definido uma única vez, no seu próprio arquivo, e declarado no header |
| `UNDECIDED_CONDITIONS` | **0** — nenhuma condição ficou sem resposta; o que não puder ser decidido é reportado, e a região conta como alcançável (nunca "chutada" como inativa) |

A tabela final lista, por configuração, qual manutenção de cache cada uma alcança:
`darwin_platform.c: sys_icache_invalidate` nas três Apple; `linux_platform.c:
__builtin___clear_cache` **só** nas Linux.

**Controle negativo** (`tools/platform_composition_negative_control.py`, transcrição em
`Documentation/evidence/platform_composition_negative_control.txt`), com a árvore restaurada em
`finally`: (A) `linux_platform.c` exatamente como na execução 05 → `APPLE_FORBIDDEN_SYMBOLS=2`
apontando `linux_platform.c:154`; (B) guarda removida de `sys_icache_invalidate` →
`LINUX_FORBIDDEN_SYMBOLS=2`; (C) `rt_platform_current()` devolvendo o backend Linux sob `__APPLE__`
→ `BACKEND_SELECTION=FAILED`; (D) fonte em disco fora de qualquer target →
`TARGET_COMPOSITION=2`. `PLATFORM_COMPOSITION_NEGATIVE_CONTROL=PASS`.

### 14.5 Auditoria de link (`tools/audit_link_symbols.sh`)

Sem toolchain Apple, examina o que **é** examinável e não inventa o resto: compila os mesmos
arquivos com o cross compiler, lista os símbolos indefinidos de cada objeto, mostra que
`__clear_cache` é referenciado **apenas** por `linux_platform.o`, localiza quem o fornece
(`/usr/lib/gcc-cross/aarch64-linux-gnu/14/libgcc.a`, `T __clear_cache`), desmonta a função real
(`bf … bl 0 <__clear_cache>` / `ret`), imprime os pontos de chamada de icache e a seleção de
backend — e então declara `APPLE_LINK_AUDIT=UNTESTED REASON=NO_APPLE_TOOLCHAIN`, porque o link
`iphoneos` é veredito do linker da Apple. `LINK_SYMBOL_AUDIT=PASS`.

### 14.6 Regressão após a Correção 05

| Verificação | Resultado |
| --- | --- |
| Host x86-64, rebuild limpo | **0 warnings / 0 errors** · ctest **2/2** · **145 checks, 0 falhas** · suíte `records=56 pass=52 fail=0 blocked=0 unsupported=2 untested=0 not_applicable=2 summary=PASS` |
| AArch64 (cross + qemu), rebuild limpo | **0 warnings** · **144 checks, 0 falhas** · `pass=53 fail=0 unsupported=2 not_applicable=1 summary=PASS` |
| Cabeçalho do relatório | `platform=linux page_size=4096 apple_target=none` · `# host: platform=linux page_size=4096 isa=x86-64 apple_target=none` |
| Composição/plataforma (4f, nova) | `TARGET_COMPOSITION=0` · `APPLE_FORBIDDEN_SYMBOLS=0` · `LINUX_FORBIDDEN_SYMBOLS=0` · `BACKEND_SELECTION=OK` · `UNDECIDED_CONDITIONS=0` · `PLATFORM_COMPOSITION_AUDIT=0` |
| Controle negativo da nova auditoria | 4/4 controles pegos (`PLATFORM_COMPOSITION_NEGATIVE_CONTROL=PASS`) |
| Auditoria de link | `LINK_SYMBOL_AUDIT=PASS` · `APPLE_LINK_AUDIT=UNTESTED REASON=NO_APPLE_TOOLCHAIN` |
| Auditoria de includes (4b, Correção 02) | `UNGUARDED_LINUX_INCLUDES=0` |
| Auditoria de APIs (4c, Correção 03) | `UNGUARDED_MACOS_ONLY_APIS=0` |
| Auditoria de interfaces (4d, Correção 04) | `INTERFACE_AUDIT=0` |
| Sintaxe da bridge (4e, Correção 04) | `BRIDGE_SYNTAX=PASS` 0/0 |
| `.xcodeproj` | `PLIST_SYNTAX=PASS` (69 objetos) · preflight **62 checagens, 0 falhas** |
| Correção 01 | `EVIDENCE_DIR` absoluto · `mkdir -p` antes de cada `tee` · `pipefail` · portão `xcodebuild -list` intacto |
| **Ponto único mais forte da correção** | o pré-processado AArch64 de `linux_platform.c` e `darwin_platform.c` é **idêntico** antes e depois (`sha256` do `-E -P` inalterado): nada muda no que se compila em Linux |
| Fase 04 | 86 arquivos byte-idênticos fora dos 2 `.txt` de inventário · 0 alterações no git |
| Toolchain Apple | `IOS_BUILD=UNTESTED REASON=NO_APPLE_TOOLCHAIN` — nenhum resultado de Xcode fabricado |

### 14.7 O que a Correção 05 **não** prova

* Não prova que o build `iphoneos` completa nem que a IPA sai. Prova que **a causa demonstrada foi
  removida na camada certa** e que agora existem cinco pré-checagens (4b–4f) cobrindo as quatro
  classes de falha já vistas: header ausente, símbolo indisponível no alvo, declaração invisível e
  **símbolo de plataforma errada alcançando outro alvo / objeto direto no link**.
* Não prova nada sobre o iPhone 13: `IPHONE_VALIDATION_PENDING` permanece. JIT no aparelho continua
  dependendo de assinatura/entitlements, e a suíte responde `NOT_APPLICABLE`/`BLOCKED` com o motivo
  real — nunca `PASS` por otimismo.

## 15. Correção 06 — validação física no iPhone 13 (execução `IPHONE13_PHYSICAL_RUN_01`)

### 15.1 A execução física

Primeira execução real em `ios-device` (iPhone 13), documentada integralmente em
`Documentation/IPHONE13_PHYSICAL_RUN_01.md` e **imutável**:

```
platform=darwin page_size=16384 isa=aarch64 apple_target=ios-device
records=52 assertions=0 pass=42 fail=4 blocked=1 unsupported=1 untested=1 not_applicable=3 summary=FAIL
PHASE_02_PHYSICAL_VALIDATION=FAIL
```

A IPA foi compilada para `iphoneos`, empacotada, assinada e instalada; o aplicativo abriu e
percorreu todas as suítes. Quatro `FAIL`. Nenhum deles foi convertido em `PASS`, nenhum foi
simplesmente rebaixado para `BLOCKED`, e cada um recebeu causa-raiz individual **no código que
produziu a linha**.

### 15.2 `jit.alloc` — a mesma capacidade que o probe, agora provada

`rt_jit_alloc()` falhava com `errno=1` vindo do `mmap(MAP_JIT)`, mas `*map_jit_attempted_out` só
era escrito **em caso de sucesso** — a arena recusada ficava sem qualquer ligação com o probe
`BLOCKED` e aparecia como defeito independente. Correções:

* `runtime_jit.c`: `*map_jit_attempted_out = 1` **antes** do `mmap`, inclusive na recusa;
* `runtime_loader.c`/harness: o status e o errno do probe são passados a `phase02_jit_microtest()`;
* `phase02_classify_jit_alloc(attempted, probe_status, probe_errno, alloc_errno)` devolve
  `DEPENDENCY_BLOCKED` **apenas** quando a tentativa ocorreu, o probe mediu `BLOCKED` e
  `alloc_errno == probe_errno != 0`. Qualquer outra combinação (errno diferente, errno perdido,
  probe `PASS`) continua sendo defeito. `phase02_classify(DEPENDENCY_BLOCKED, RT_PASS)` devolve
  `FAIL`, ou seja, se a capacidade tiver sido oferecida o resultado volta a ser defeito.

### 15.3 `fs.deep_paths` — o `errno=0` era um `errno` lido antes de ser escrito

`rt_fs_deep_paths()` calculava `failure = (err_out != NULL) ? *err_out : EIO`, lendo o
parâmetro de saída do **próprio chamador** antes de qualquer escrita: o motivo real era descartado
e o registro imprimia `errno=0 (Undefined error: 0)`. Correções:

* `rt_fs_write_pattern(path, err_out)` captura o errno **no ponto da falha**; a leitura de volta
  captura antes do `close()`;
* `rt_fs_deep_paths_ex(..., rt_fs_stage_t *stage_out)` reporta a etapa (`mkdir`, `leaf-path-join`,
  `leaf-write`, `leaf-read`, `leaf-compare`, `leaf-unlink`, `errno-lost`);
* invariante: uma falha **nunca** sai com errno 0 — se algum caminho futuro quebrar isso, o
  registro traz `RT_FS_STAGE_ERRNO_LOST` com `EIO` em vez de um zero enganoso;
* `phase02_classify_fs_depth_error`: `ENAMETOOLONG` na etapa da plataforma → `PASS` (limite
  documentado); teto do **próprio buffer** (`PATH_CAP`/`LEAF_JOIN`) → `PASS` com a proveniência do
  teto declarada (`OK_PROBE_LIMIT`), nunca “limite da plataforma”; `EPERM/EACCES/ENOSPC/EDQUOT/EROFS`
  → `BLOCKED`; `errno=0` → defeito (`FAIL`).

No host o teto é o buffer do probe: `depth up to 224 (2035 chars) ... the ceiling at depth=225
(stage=leaf-path-join, errno=36) is this probe's own path buffer (cap=2048), NOT the platform's
(PATH_MAX=4096[pathconf])`.

### 15.4 `ipc.posix_shm` — o errno sem a syscall

`errno=1` sozinho não distingue “o sandbox recusa `shm_open`” de “recusa o segundo `mmap`”. Agora
`rt_ipc_shm_ex(&err, &stage)` reporta `RT_IPC_STAGE_*` (`shm_open`, `ftruncate`, `mmap(first view)`,
`mmap(second view)`, `compare`, `unlink`) com o errno capturado imediatamente, e
`phase02_classify_shm_error(stage, err)` decide: `EPERM/EACCES` → capacidade não concedida
(`BLOCKED`); `ENOSYS/ENOTSUP` → `UNSUPPORTED`; divergência entre as duas visões com **todas** as
syscalls aceitas → defeito do nosso mapeamento; `errno=0` → defeito do caminho de erro. A
classificação segue a etapa medida, nunca o conhecimento externo sobre a plataforma. A
infraestrutura existente (`rt_dual_map_create`) já usa a mesma interface — nenhuma arquitetura nova
foi criada.

### 15.5 `loader.run_valid_module` — `rejected: OK`

`rt_loader_run()` retornava `RT_FAIL` quando `rt_jit_alloc()` falhava **sem escrever o motivo**
(`*loader_err_out` ficava em `RT_LOADER_OK`) e sem reportar errno: daí `rejected: OK`. Agora
`rt_loader_run_ex(..., &os_err, &map_jit_attempted)`:

* retorna `RT_BLOCKED` com `RT_LOADER_ERR_JIT_UNAVAILABLE` + errno quando a arena executável é
  recusada (módulo válido, execução inviável);
* `RT_LOADER_ERR_EXEC_FAULT` com `si_addr` quando o módulo foi mapeado e a entrada falhou;
* `RT_LOADER_ERR_INTERNAL` quando o passo que falhou é nosso;
* `RT_FAIL` fica reservado à **recusa por validação**, e a invariante é: todo retorno diferente de
  `RT_PASS` carrega um motivo ≠ `RT_LOADER_OK`;
* o wrapper `rt_loader_run()` continua existindo (compatibilidade) e o harness usa
  `phase02_classify_loader_result()` — um `RT_FAIL` com motivo `OK` é classificado como defeito, e
  a combinação física (imagem válida + arena recusada + probe bloqueado + errno igual) como
  dependência do `jit.map_jit_probe`.

### 15.6 Semântica de resultados (incremento mínimo, sem regra genérica)

`phase02_harness.h` documenta a tabela `outcome → status`; nada de “FAIL→BLOCKED” automático:

| Outcome | Status | Quando |
| --- | --- | --- |
| `OK` | `PASS` | sucesso observado |
| `OK_PROBE_LIMIT` | `PASS` | capacidade medida; o teto é do probe e está declarado |
| `RUNTIME_DEFECT` | `FAIL` | defeito nosso (inclui errno 0 e motivo ausente) |
| `CAPABILITY_MISSING` | `BLOCKED` | a plataforma recusou o que o teste precisa |
| `DEPENDENCY_BLOCKED` | `BLOCKED` | dependência medida não é `PASS`; se a dependência passou → `FAIL` |
| `UNSUPPORTED` | `UNSUPPORTED` | não existe no alvo |
| `NOT_APPLICABLE` | `NOT_APPLICABLE` | o alvo não pode chamar a API |
| `UNDETERMINED` | `UNTESTED` | causa não estabelecida — nunca `PASS`, nunca `FAIL` |

### 15.7 Entitlements — quatro níveis, nenhum presumido

1. **pedido no projeto** — `RuntimePoC/WinlatorPhase02.entitlements` contém
   `com.apple.security.cs.allow-jit`, e o projeto **não** o liga a `CODE_SIGN_ENTITLEMENTS`: nada é
   aplicado por si só;
2. **presente antes de assinar** — pertence ao passo de assinatura (fora do repositório; sem
   credenciais);
3. **concedido à assinatura** — só se lê no produto construído, com
   `tools/inspect_entitlements.sh` (novo; imprime **apenas nomes de chaves e booleanos**, nunca
   certificado, team id ou UDID; em produto não assinado responde `UNSIGNED_IPA`; em host não-Darwin
   responde `UNTESTED REASON=NO_MACOS_HOST`), executado no CI como diagnóstico que não falha o build;
4. **observado em execução** — o que a suíte mediu (`MAP_JIT *`), agora acompanhado de uma linha
   `[PHASE02] NOTE=jit.entitlement_levels` que separa os quatro níveis e não altera contagem alguma.

A suíte física #1 não prova, em momento algum, que o JIT está concedido — e a Correção 06 não
afirma que esteja.

### 15.8 Auditoria estática e controles negativos (novos)

`tools/audit_fix06_contracts.py` — sete regras, cada uma nascida de uma linha física:
`LOADER_REASON` (nenhum retorno não-`PASS` sem motivo), `SHM_STAGE`, `DEPTH_STAGE` (o ramo de falha
não pode zerar o errno), `ERRNO_ZERO_DEFECT`, `JIT_ATTEMPT_ORDER`, `COMPAT_SURFACE`,
`PHYSICAL_BASELINE` (o run #1 existe, verbatim, e continua `FAIL`).

`tools/fix06_negative_controls.py` — seis controles que reintroduzem cada defeito e verificam o
detector; restauração conferida por SHA-256 e reconstrução verde no fim:

| Controle | Defeito reintroduzido | Detectado por |
| --- | --- | --- |
| A | loader perde o motivo → volta o `rejected: OK` | auditoria + testes |
| B | o probe de profundidade deixa de preservar o errno | auditoria + testes |
| C | tentativa de MAP_JIT registrada só após sucesso | auditoria (em Linux não é observável) |
| D | registro de shm sem a etapa que falhou | auditoria (o host concede POSIX shm) |
| E | baseline física reescrita para `PASS` | auditoria |
| F | `errno=0` aceito como veredito (falha silenciosa como `PASS`) | auditoria + testes |

`FIX06_NEGATIVE_CONTROLS=6/6` (no CI, em modo estático, 6/6 pelos quatro primeiros + E/F).

### 15.9 Regressão após a Correção 06

| Verificação | Resultado |
| --- | --- |
| Host x86-64, rebuild limpo | **0 warnings / 0 errors** · ctest **2/2** · **207 checks, 0 falhas** · suíte `records=56 pass=52 fail=0 blocked=0 unsupported=2 untested=0 not_applicable=2 summary=PASS` |
| AArch64 (cross + qemu), rebuild limpo | **0 warnings** · **198 checks, 0 falhas** · `pass=53 fail=0 unsupported=2 not_applicable=1 summary=PASS` |
| Regressão da condição física (host) | `RLIMIT_AS` reduzido e **provado eficaz** por probe: `rt_loader_run_ex` devolve `BLOCKED` + `JIT_UNAVAILABLE` + errno ≠ 0; sob `qemu-user`, que ignora `RLIMIT_AS`, o teste imprime `skip` com o motivo em vez de fingir |
| Classificadores puros (rodam em todo host) | combinação física de run #1, `errno=0`, estágios de shm e de profundidade, motivo ausente: todos verificados |
| Auditoria Fix 06 (nova) | `FIX06_CONTRACTS_AUDIT=0` (7/7 regras) |
| Controles negativos Fix 06 (novos) | `FIX06_NEGATIVE_CONTROLS=6/6`, restauração byte-idêntica |
| Composição de alvos (Correção 05) | `TARGET_COMPOSITION=0` · `APPLE_FORBIDDEN_SYMBOLS=0` · `LINUX_FORBIDDEN_SYMBOLS=0` · `BACKEND_SELECTION=OK` · `UNDECIDED_CONDITIONS=0` · `PLATFORM_COMPOSITION_AUDIT=0` |
| Controle negativo da composição | `PLATFORM_COMPOSITION_NEGATIVE_CONTROL=PASS` (controle A agora **seleciona sozinho** a revisão com a forma do CI #5, perguntando à própria auditoria) |
| Link | `LINK_SYMBOL_AUDIT=PASS` · `APPLE_LINK_AUDIT=UNTESTED REASON=NO_APPLE_TOOLCHAIN` |
| Includes / APIs / interfaces / bridge | `UNGUARDED_LINUX_INCLUDES=0` · `UNGUARDED_MACOS_ONLY_APIS=0` · `INTERFACE_AUDIT=0` · `BRIDGE_SYNTAX=PASS` 0/0 |
| `.xcodeproj` | `PLIST_SYNTAX=PASS` · preflight **62 checagens, 0 falhas** |
| Entitlements | `ENTITLEMENT_INSPECTION=UNTESTED REASON=NO_MACOS_HOST` (nível 3 exige produto assinado em macOS) |
| Fase 04 | `git diff -- ios/box64-registers` **vazio** · nenhum arquivo de `Tests/` ou `RuntimePoC/` alterado fora do previsto |
| Inventário | 54 entradas, todas válidas |
| Correções 01–05 | intactas: nenhum teste removido, nenhum `PASS` comprado, `sys_icache_invalidate` no Apple, Linux icache inerte fora de Linux |

### 15.10 O que a Correção 06 **não** prova

* Não prova que o iPhone passa: `PHASE_02_PHYSICAL_VALIDATION=FAIL` permanece até existir a
  `IPHONE13_PHYSICAL_RUN_02`. Nenhum resultado físico foi fabricado aqui.
* Não prova que o JIT funciona no aparelho, nem que o entitlement foi concedido: sem assinatura com
  JIT, o resultado honesto continua `BLOCKED`.
* Não transforma `UNSUPPORTED`/`BLOCKED`/`UNTESTED`/`NOT_APPLICABLE` em `PASS` — inclusive
  `memory.dual_mapping_rw_rx = UNSUPPORTED` e `jit.write_protect_np = NOT_APPLICABLE`.
* Não corrige `ipc.posix_shm` “no escuro”: se o sandbox iOS recusar `shm_open`, a resposta correta
  passa a ser `BLOCKED` **com a syscall nomeada**; se recusar o segundo `mmap`, a resposta nomeia
  isso.

## 16. Rodada de estabilização iOS — pass 01 (pacote `phase02-ios-stabilization-pass-01`)

Base: Correção 06 (`e50db04`) preservada integralmente. Objetivo único: **corrigir todos os
defeitos demonstráveis da Fase 02 nos caminhos que o iPhone executa**, sem tocar em interface,
sem remover nada e sem transformar `FAIL` em `PASS`. Inventário completo em
`Documentation/PHASE02_IOS_ERROR_LEDGER.md`; relatório de entrega com o SHA-256 em
`PHASE02_IOS_STABILIZATION_PASS_01.md` (raiz do workspace).

### 16.1 O que foi auditado

Caminho JIT completo (entrada → probe de capacidade → `MAP_JIT` → alocação → escrita → emissão
ARM64 → proteção → sincronização de icache → execução → retorno → reescrita → nova
sincronização → segunda execução → liberação), entitlements em quatro níveis, memória/W^X/dual
mapping, POSIX shm, filesystem (profundidade, limites, buffers, errno, limpeza), loader RTM1
ponta a ponta, CPU/ABI (auditoria sem alteração), threads/sinais (auditoria de interação),
IPC (sem tocar nos `PASS`), backends de plataforma, e o build Apple por análise estática
(compiler, bridge ObjC, Swift, headers, availability, target conditionals, linker, símbolos
indefinidos/duplicados, pertencimento de fontes, frameworks, entitlements, Info.plist,
pbxproj, build settings) — sem inventar resultado de Xcode: `APPLE_*` locais continuam
`UNTESTED REASON=NO_APPLE_TOOLCHAIN`.

### 16.2 Oito defeitos de código encontrados e corrigidos

| ID | Defeito | Correção | Regressão |
| --- | --- | --- | --- |
| S-001 | janela de escrita e flush de icache chamados com `(void)` no microteste; escrita em arena `MAP_JIT` sem janela (crash fora do guard) e icache não sincronizado em silêncio | janela conferida nas duas escritas; novo passo reportado `jit.icache_sync` (`PASS` / `NOT_APPLICABLE` / `FAIL` que impede a execução) | testes + auditoria `DISCARDED_RESULTS`/`ICACHE_REPORTED` + controle A |
| S-002 | arena dimensionada por `(first_len > second_len) ? first_len : first_len` (expressão morta) e reescrita "clampada" em silêncio | dois buffers de payload emitidos antes da alocação, `arena_len = max(...)`, checagem de capacidade e recusa explícita | `test_jit_payload_capacity` + `ARENA_SIZING` + controle B |
| S-003 | "guard não instalável" (`-2`) reportado como fault com `si_addr=(nil)` (loader e microteste) | `-2` ⇒ `RT_LOADER_ERR_INTERNAL` + errno; no harness ⇒ `UNTESTED` com a causa | `test_loader_guard_unavailable_is_not_a_fault` + `GUARD_VS_FAULT` + controle C |
| S-004 | `rt_jit_execution_allowed` escrevia sem janela, ignorava o icache e convertia `-2` em "não" | janela conferida, flush honrado, `-2` ⇒ "não determinado" | `DISCARDED_RESULTS` + suíte JIT |
| S-005 | dual mapping presumia *qual* operação falhou e classificava `EPERM/EACCES` como `UNSUPPORTED`, divergindo do probe de shm | `rt_dual_stage_t` + nome da etapa em cada caminho; classificação por etapa+errno com o vocabulário do shm | `test_dual_mapping_stage_and_classification` + `DUAL_MAP_STAGE`/`CAPABILITY_WORDS` + controles D/E |
| S-006 | `shm_unlink` final descartado e `RT_IPC_STAGE_UNLINK` documentado mas inexistente | etapa `UNLINK` criada, nomeada e conferida; falha de limpeza reportada como fato de limpeza (nunca como veredito) | `test_ipc_shm_cleanup_stage` + `SHM_CLEANUP` + controle F |
| S-007 | `getrandom()` devolvendo 0 giraria o laço para sempre | `got == 0` ⇒ `EIO` | `RANDOM_PROGRESS` + controle G |
| S-008 | `DETAIL` truncado em 512 chars e relatório truncado pela capacidade, ambos em silêncio | marcadores explícitos de truncamento | `test_log_detail_truncation_is_marked` + `LOG_INTEGRITY` + controle H |

Um defeito adicional foi **introduzido e detectado dentro da própria correção** (S-002: os dois
payloads no mesmo buffer faziam a primeira execução devolver 4242) — a suíte o pegou antes de
qualquer commit, e ele está registrado no ledger por honestidade.

Um defeito **não** foi corrigido, por estar fora do escopo desta rodada (regra de UI):
`S-009` — "Save report" grava o relatório em `tmp`, enquanto o `Info.plist` habilita
compartilhamento de `Documents`; a correção de uma linha está proposta no ledger e a
recuperação por **Copy/Share** (usada na run #1) continua funcionando.

### 16.3 Correções 01–06 preservadas (conferido)

`sys_icache_invalidate` segue sendo o caminho Apple; `linux_icache_flush` continua Linux-only;
`pthread_jit_write_protect_np` continua ausente no iOS (e o bit de capacidade também);
`MAP_JIT` continua um probe de runtime; o bridge continua sem declaração ad hoc; a
classificação causal `jit.alloc → jit.map_jit_probe` e o motivo obrigatório do loader
continuam na íntegra; nenhum teste foi removido; nenhum `PASS` foi comprado.

### 16.4 Regressão após a rodada

| Verificação | Resultado |
| --- | --- |
| Host x86-64, rebuild limpo | **0 warnings / 0 errors** · ctest **2/2** · **237 checks, 0 falhas** · suíte `records=58 pass=54 fail=0 blocked=0 unsupported=2 untested=0 not_applicable=2 summary=PASS` |
| AArch64 (cross + qemu) | **0 warnings** · **228 checks, 0 falhas** · `records=58 pass=55 fail=0 … summary=PASS` |
| Caminho JIT completo, agora auditável no relatório | `jit.icache_sync` (x2), `jit.alloc`, `jit.make_executable`, `jit.execute_return_42`, `jit.rewrite_payload`, `jit.execute_return_4242`, `jit.execution_allowed` — todos presentes e verdes no host |
| Nova auditoria | `IOS_STABILIZATION_AUDIT=0` (9 regras) |
| Novos controles negativos | `STABILIZATION_NEGATIVE_CONTROLS=8/8`, restauração byte-idêntica |
| Auditorias anteriores | `FIX06_CONTRACTS_AUDIT=0` · `FIX06_NEGATIVE_CONTROLS=6/6` · `PLATFORM_COMPOSITION_AUDIT=0` (+ controle `PASS`) · `LINK_SYMBOL_AUDIT=PASS` · `APPLE_LINK_AUDIT=UNTESTED REASON=NO_APPLE_TOOLCHAIN` · `INTERFACE_AUDIT=0` · `BRIDGE_SYNTAX=PASS` · `UNGUARDED_LINUX_INCLUDES=0` · `UNGUARDED_MACOS_ONLY_APIS=0` |
| `.xcodeproj` | `PLIST_SYNTAX=PASS` · preflight **62 checagens, 0 falhas** |
| Entitlements | `ENTITLEMENT_INSPECTION=UNTESTED REASON=NO_MACOS_HOST` (nível 3 exige produto assinado em macOS) |
| Fase 04 | `git diff -- ios/box64-registers` **vazio** |
| Interface/UI | **zero** arquivos de `RuntimePoC/` alterados (`.swift`, `.m`, `.h`, `.plist`, `.entitlements`, `main.c`) |

### 16.5 Estados, ao fim da rodada

* **JIT:** caminho completo instrumentado e verificado; `MAP_JIT` continua dependendo de
  entitlement (capacidade externa); nenhum fallback RWX; nenhuma transformação de flush em
  no-op; a execução só acontece com o icache sincronizado ou com a arquitetura declarada
  incoerente por natureza (x86).
* **Entitlements:** os quatro níveis continuam separados; o projeto **pede** a entitlement e
  não a liga ao `CODE_SIGN_ENTITLEMENTS` (decisão documentada); o CI inspeciona o produto; a
  concessão é externa.
* **Shared memory:** `shm_open`/`ftruncate`/`mmap`×2/`compare`/`unlink` com etapa e errno;
  recusa do sandbox ⇒ `BLOCKED` com a syscall nomeada; ausência ⇒ `UNSUPPORTED`.
* **Loader:** `RT_PASS` só com execução observada; `BLOCKED` + motivo + errno quando a
  capacidade falta; `INTERNAL` para defeito nosso (inclusive guard indisponível); `FAIL` só
  para imagem recusada pela validação.
* **Filesystem:** profundidade medida com etapa e errno reais; teto do próprio buffer
  declarado como tal; `errno=0` é defeito.
* **Backends:** `linux_platform.c` e `darwin_platform.c` intactos na arquitetura; nenhuma API
  Linux alcançável no Apple e nenhum símbolo Apple no Linux
  (`APPLE_FORBIDDEN_SYMBOLS=0`, `LINUX_FORBIDDEN_SYMBOLS=0`, `BACKEND_SELECTION=OK`).
* **Fase 04:** intacta, byte a byte.
* **Fase 05:** não iniciada.

## 17. Rodada de estabilização iOS — pass 02 (rodada de fechamento)

Base: Correção 06 (`e50db04`) + pass 01 (`631ded6`), ambos preservados. Objetivo único desta
rodada: **zerar os defeitos de código conhecidos da Fase 02 antes do Apple CI real**.
Inventário em `Documentation/PHASE02_IOS_ERROR_LEDGER.md`; relatório de entrega com o SHA-256
em `PHASE02_IOS_STABILIZATION_PASS_02.md` (raiz do workspace).

### 17.1 S-009 — o relatório salvo agora vai para onde o aparelho o expõe

Causa confirmada por inspeção: `save()` montava a URL a partir do **mesmo** `workdir` das
suítes (`NSTemporaryDirectory()`), enquanto `Info.plist` declara `UIFileSharingEnabled` +
`LSSupportsOpeningDocumentsInPlace` — o compartilhamento de arquivos expõe `Documents/`, não
`tmp/`. Correção mínima, uma função:

* destino passa a ser `FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first`
  (com falha explícita se não existir);
* o `workdir` das suítes continua `tmp` (mover isso mudaria o comportamento da suíte de
  filesystem, que é um caminho `PASS`);
* o nome do arquivo continua vindo de `Phase02Bridge.reportFileName()`;
* a gravação continua `try report.write(to:atomically:encoding:)` com os dois desfechos
  reportados em `status`.

**Nenhuma linha da interface foi tocada:** o bloco `body:` (rótulos, ordem, layout, botões,
cores) é comparado byte a byte com `tools/ui_surface.snapshot.txt`, extraído do commit
`631ded6` da pass 01 — a regra `UI_SURFACE_FROZEN` falha se ele mudar.

### 17.2 Varredura final — sete defeitos reais a mais (S-010 … S-016) e S-017

| ID | Defeito | Correção | Regressão |
| --- | --- | --- | --- |
| S-010 | `rt_fs_links_and_modes` sobrescrevia, com um `errno` lido depois, o errno já capturado por `rt_fs_write_pattern` | atribuição redundante removida | `ERRNO_CAPTURED_NOW` + controle M |
| S-011 | `lstat`/`stat` conferidos em condição composta: `errno` publicado quando a syscall **funcionou** e o tipo era outro (inclusive 0) | syscall e tipo conferidos separadamente; `EINVAL` para tipo inesperado | `ERRNO_CAPTURED_NOW` + teste de contrato |
| S-012 | `rt_fs_temp_file` reportava escrita curta com `errno` cru | `(written < 0) ? errno : EIO` | `ERRNO_CAPTURED_NOW` + controle N |
| S-013 | `rt_ipc_scm_rights` reportava transferência curta com `errno` cru (nos dois sentidos) | `EIO` na transferência curta | `ERRNO_CAPTURED_NOW` + teste |
| S-014 | três compositores de resumo truncavam em silêncio quando o buffer era pequeno | marcador ` [SUMMARY TRUNCATED]` | `TRUNCATION_MARKED` + `test_summaries_mark_truncation` + controle P |
| S-015 | `rt_signal_roundtrip`: falha devolvia −1 sem errno; falha ao **restaurar** a disposição era descartada e virava `PASS` | errno em toda falha; restauração honrada | `test_failure_carries_and_success_clears_errno` + controle Q |
| S-016 | os cinco round trips de threads devolviam −1 com `*err_out = 0` em divergência | `EILSEQ` na divergência | teste de cada round trip + controle O (quebra os cinco sítios) |
| S-017 | `rt_mem_protect` sem a guarda de overflow que `rt_mem_reserve` tem | guarda `len > SIZE_MAX - page` + `EOVERFLOW` | `test_mem_protect_rejects_overflow` + `PROTECT_OVERFLOW` + controle L |

Todos os sete novos defeitos têm a mesma assinatura do defeito que a execução física
encontrou (`errno=0` / errno não relacionado à chamada que falhou): é a família que a Fase 02
mais precisa manter fechada, porque é ela que faz uma falha de plataforma parecer um defeito do
port e vice-versa.

### 17.3 Auditoria que impede o retorno de cada defeito

`tools/audit_ios_stabilization.py` passou de 11 para **15 regras**:

`DISCARDED_RESULTS`, `ICACHE_REPORTED`, `ARENA_SIZING`, `GUARD_VS_FAULT`, `DUAL_MAP_STAGE`,
`CAPABILITY_WORDS`, `SHM_CLEANUP`, `RANDOM_PROGRESS`, `LOG_INTEGRITY`, `SAVE_REPORT_TARGET`,
`UI_SURFACE_FROZEN`, **`ERRNO_CAPTURED_NOW`** (o errno da chamada que falhou é o que vale — sem
sobrescrita depois da captura, sem `errno` de escrita curta, sem falha sem errno),
**`PROTECT_OVERFLOW`**, **`TRUNCATION_MARKED`**, **`PAGE_SIZE_MEASURED`** (o tamanho de página
vem do `sysconf` do backend; `16384` não pode aparecer escrito no código).

`tools/stabilization_negative_controls.py` passou de 8 para **18 controles**: cada regra é
exercitada contra o seu próprio defeito, e cada controle exige a detecção e restaura o arquivo
conferindo o SHA-256 (A–K da rodada 01, L–R desta rodada).

### 17.4 Estado físico e o que continua externo

`IPHONE13_PHYSICAL_RUN_01` continua **imutável** (`pass=42 fail=4 blocked=1 unsupported=1
untested=1 not_applicable=3 summary=FAIL`): as correções não transformam retroativamente aquela
execução. O que depende de fora continua registrado como depende de fora:

* concessão da entitlement de JIT (`com.apple.security.cs.allow-jit`) — assinatura/provisioning
  no aparelho; nenhum fallback foi criado, nenhuma capability foi "concedida" no código;
* confirmação em Darwin (Apple CI real) e no iPhone: `APPLE_*` locais seguem
  `UNTESTED REASON=NO_APPLE_TOOLCHAIN` e `ENTITLEMENT_INSPECTION=UNTESTED REASON=NO_MACOS_HOST`.

### 17.5 Regressão desta rodada

| Verificação | Resultado |
| --- | --- |
| Host x86-64, build limpo | **0 warnings / 0 errors** · **274 checks, 0 falhas** (era 237) · ctest 2/2 |
| AArch64 (cross + qemu) | **0 warnings · 265 checks, 0 falhas** (era 228) |
| Suíte completa, host e AArch64 | `records=58 assertions=0 pass=54/55 fail=0 blocked=0 unsupported=2 summary=PASS` |
| Auditoria nova | `IOS_STABILIZATION_AUDIT=0` (15 regras) |
| Controles negativos novos | `STABILIZATION_NEGATIVE_CONTROLS=18/18`, restauração byte-idêntica, árvore verde depois |
| Correção 06 | `FIX06_CONTRACTS_AUDIT=0` · `FIX06_NEGATIVE_CONTROLS=6/6` |
| Plataforma / link / interface | `PLATFORM_COMPOSITION_AUDIT=0` (+ controle `PASS`) · `LINK_SYMBOL_AUDIT=PASS` · `INTERFACE_AUDIT=0` · `BRIDGE_SYNTAX=PASS` · `UNGUARDED_LINUX_INCLUDES=0` · `UNGUARDED_MACOS_ONLY_APIS=0` |
| `.xcodeproj` | `PLIST_SYNTAX=PASS` · preflight **62 checagens, 0 falhas** |
| Escopo | `PASS02_SCOPE_OUT_OF_SCOPE=0` · `UI_VISUAL_CHANGES=0` · `FILES_DELETED=0` · `PHASE04_FUNCTIONAL_CHANGES=0` · `PHASE05_STARTED=NO` |
| Fase 04 | `git diff -- ios/box64-registers` **vazio** |

### 17.6 Contagem final da campanha

```
KNOWN_CODE_DEFECTS_BEFORE_PASS_02 = 2
NEW_CODE_DEFECTS_FOUND_PASS_02    = 7
CODE_DEFECTS_FIXED_PASS_02        = 9
KNOWN_UNFIXED_CODE_DEFECTS        = 0
APPLE_CI_RETEST_REQUIRED          = 9
IPHONE_RETEST_REQUIRED            = 5
EXTERNAL_CAPABILITY_BLOCKED       = 1
```
