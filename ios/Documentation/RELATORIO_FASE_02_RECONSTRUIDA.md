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
4. Aplicar a correção 03 e reexecutar o workflow. Esperado: portão PASS, auditorias de headers e
   de APIs em 0, e o build `iphoneos` **ultrapassando** `darwin_platform.c`. Se aparecer outro
   erro Apple real, ele será reportado sem mascaramento — não há afirmação antecipada de que a IPA
   será produzida.
5. Assinar/instalar a IPA no iPhone 13 pelo processo habitual (a IPA sai **não assinada**).
6. Abrir o app, rodar `Run All Tests`, exportar o relatório e trazê-lo de volta
   (Arquivos › No meu iPhone › Winlator PoC, ou `xcrun devicectl device copy from`).
7. Só então classificar cada capacidade como `CONFIRMADA EM DISPOSITIVO FÍSICO`.

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
