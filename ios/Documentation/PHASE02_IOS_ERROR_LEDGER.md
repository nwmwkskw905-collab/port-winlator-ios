# PHASE02_IOS_ERROR_LEDGER — campanha de correção de erros iOS da Fase 02

`PHASE_02_RECONSTRUCTED_POC`. Este arquivo acompanha a campanha: cada problema tem ID,
origem, primeira evidência, categoria, causa raiz, arquivo responsável, status, correção,
teste de regressão, plataforma e dependência externa.

**Regras de rótulo (não negociáveis neste projeto):**

* `FIXED_CONFIRMED_APPLE_CI` só depois de um Apple CI real que comprove;
* nada é marcado como confirmado no iPhone antes de um teste físico real;
* `PASS` só para comportamento observado; `errno=0` nunca é resposta de plataforma;
* capacidade recusada não é defeito de port; ausência de mecanismo não é defeito de port.

**Estados usados:** `OPEN`, `FIXED_PENDING_APPLE_CI`, `FIXED_CONFIRMED_APPLE_CI`,
`DEVICE_RETEST_REQUIRED`, `EXTERNAL_CAPABILITY_BLOCKED`, `NOT_A_DEFECT`.

**Rodada 02 (stabilization pass 02):** abriu S-009 com correção, registro de S-010 a S-017 e
fechou todos os defeitos de código conhecidos. Nenhum deles é marcado como confirmado em
Apple CI ou no iPhone: as correções estão provadas localmente (testes + auditoria + controles
negativos) e o que falta é o próximo ciclo real.

`DEVICE_RETEST_REQUIRED` = a correção existe e está provada localmente (testes + auditoria),
mas o que falta é a **observação no aparelho**. `FIXED_PENDING_APPLE_CI` = a correção existe
e está provada localmente, e o que falta é o próximo ciclo real (build/CI Apple) para
confirmar em Darwin, além do reteste físico.

**Rodada 03 (blocker resolution pass 03, base `f81dda2`):** parte do resultado físico
`IPHONE13_PHYSICAL_RUN_02` (imutável, transcrito em
`Documentation/IPHONE13_PHYSICAL_RUN_02.md`). Não há `FAIL` de código a corrigir — a rodada
ataca apenas os cinco `BLOCKED`, o `UNTESTED` e os defeitos **provados** nos caminhos que essas
correções destravam. O grafo causal foi confirmado no código e ganhou três causas-raiz; oito
defeitos novos (S-018 … S-025) foram encontrados e corrigidos, com auditoria e controles
negativos próprios. Nada foi convertido de `BLOCKED`/`UNTESTED` em `PASS` por classificação, e
nenhuma apólice de assinatura, perfil ou certificado existe no repositório.

**Rodada 04 (run 03 crash investigation, base `d83332a` — o commit de que o IPA do run 03 foi
construído):** o app **abre** e a ação `Run Selected` **termina o processo imediatamente**, nas
quatro suítes relatadas (memory, jit, cpu, threads) e sem relatório. Este é um evento de
execução física: nenhum host/QEMU o reproduz (as oito suítes saem `rc=0 summary=PASS` na
mesma árvore). A rodada trata o evento como **terminação não classificada** — nunca `FAIL`,
`BLOCKED` ou `PASS` — até localizar o ponto de terminação, e registra os campos provisórios em
`Documentation/IPHONE13_PHYSICAL_RUN_03.md`. Resultado da investigação: a causa-raiz está no
**prefixo comum** a toda suíte (não em código de suíte), e a correção é a política de execução
(S-026). A rodada também encontrou e corrigiu um defeito de UB na sonda de fault (S-027),
achado pela **primeira execução de UBSAN** deste projeto — não é a causa do crash e não é
apresentado como tal. Nada foi mascarado: nenhum handler genérico de SIGSEGV/SIGBUS/SIGILL foi
instalado, nenhum resultado foi convertido, e a classificação final continua dependendo de um
novo teste físico.

---

## A. Inventário inicial (o que já era conhecido no começo desta rodada)

Base: `IPHONE13_PHYSICAL_RUN_01`, iPhone 13, `apple_target=ios-device`, `page_size=16384`,
`records=52 pass=42 fail=4 blocked=1 unsupported=1 untested=1 not_applicable=3 summary=FAIL`.

| ID | Observação | Classificação | Status |
| --- | --- | --- | --- |
| L-001 | `jit.alloc = FAIL` | defeito de classificação/semântica (corrigido na Correção 06) | `DEVICE_RETEST_REQUIRED` |
| L-002 | `fs.deep_paths = FAIL` `errno=0` | defeito real (errno perdido) | `DEVICE_RETEST_REQUIRED` |
| L-003 | `ipc.posix_shm = FAIL` `errno=1` sem syscall | defeito de diagnóstico | `DEVICE_RETEST_REQUIRED` |
| L-004 | `loader.run_valid_module = FAIL` `rejected: OK` | defeito real (propagação de motivo) | `DEVICE_RETEST_REQUIRED` |
| L-005 | `jit.map_jit_probe = BLOCKED` | capacidade não concedida pela assinatura | `EXTERNAL_CAPABILITY_BLOCKED` |
| L-006 | `memory.dual_mapping_rw_rx = UNSUPPORTED` | classificação inconsistente (corrigida nesta rodada, S-005) | `DEVICE_RETEST_REQUIRED` |
| L-007 | `jit.write_protect_np = NOT_APPLICABLE` | API indisponível no alvo iOS — classificação correta | `NOT_A_DEFECT` |
| L-008 | `jit.execution_allowed = UNTESTED` | dependente de capacidade bloqueada — classificação correta | `NOT_A_DEFECT` |

### L-001 … L-004 — as quatro falhas físicas

* **Origem:** `IPHONE13_PHYSICAL_RUN_01` (2026-10-02).
* **Categoria:** semântica de resultado / propagação de erro / perda de errno.
* **Causa raiz (individual, do código que produziu cada linha):**
  1. `rt_jit_alloc` registrava `MAP_JIT` apenas no sucesso → a arena recusada não era
     atribuível à capacidade medida pelo probe;
  2. `rt_fs_deep_paths` lia `*err_out` do próprio chamador **antes** de escrevê-lo → `errno=0`;
  3. `rt_ipc_shm` devolvia um errno sem a syscall → impossível decidir *onde* o iOS recusou;
  4. `rt_loader_run` devolvia `RT_FAIL` sem escrever motivo → `rejected: OK`.
* **Arquivos:** `RuntimeCore/src/runtime_jit.c`, `runtime_filesystem.c`, `runtime_ipc.c`,
  `runtime_loader.c`, `Diagnostics/src/phase02_harness.c` (+ headers).
* **Correção:** Correção 06 (commit `e50db04`), preservada integralmente nesta rodada.
* **Teste de regressão:** `Tests/test_runtime_core.c`
  (`test_outcome_classification`, `test_fs_depth_classification`, `test_shm_classification`,
  `test_loader_reason_propagation`, `test_errno_preservation_on_failure`) +
  `tools/fix06_negative_controls.py` (6/6) + `tools/audit_fix06_contracts.py` (=0).
* **Plataforma:** iOS device (observado), Linux/AArch64 (verificado).
* **Dependência externa:** nenhuma.
* **O que falta:** a **nova execução física** — só ela pode substituir a linha antiga.

### L-005 — `MAP_JIT` bloqueado no aparelho

* **Categoria:** capacidade externa (assinatura/entitlement), não defeito de código.
* **Evidência:** `MAP_JIT refused errno=1 (Operation not permitted) (target=ios-device)`.
* **Análise de quatro níveis (§2):** (1) **pedido no projeto**:
  `RuntimePoC/WinlatorPhase02.entitlements` contém `com.apple.security.cs.allow-jit`;
  (2) **presente antes da assinatura**: pertence ao passo de assinatura, fora do repositório;
  (3) **concedido à assinatura**: só se lê no produto; `tools/inspect_entitlements.sh` faz
  isso no CI (com a `UNSIGNED_IPA` responde `ABSENT`, que é a limitação atual);
  (4) **observado em runtime**: `jit.map_jit_probe`, medido pelo app no aparelho.
* **Decisão de projeto (mantida):** o `.entitlements` **não** é ligado a
  `CODE_SIGN_ENTITLEMENTS`, de propósito: ligar uma entitlement que o perfil não carrega
  quebraria a instalação, e o teste deve medir o contexto em que o binário realmente roda.
  O CI constrói sem assinatura (`CODE_SIGNING_ALLOWED=NO`).
* **Dependência externa:** sim. Para conceder JIT: assinar com um perfil que carregue a
  entitlement e então medir de novo (`jit.map_jit_probe`). **Nenhuma solução foi inventada
  no código** — não existe fallback RWX, e o fluxo de trabalho não afirma concessão.
* **Status:** `EXTERNAL_CAPABILITY_BLOCKED`.

---

## B. Defeitos encontrados nesta rodada (auditoria proativa, §12)

| ID | Defeito | Arquivo responsável | Status |
| --- | --- | --- | --- |
| S-001 | resultados JIT descartados com `(void)`: janela de escrita e flush de icache | `Diagnostics/src/phase02_harness.c` | `FIXED_PENDING_APPLE_CI` |
| S-002 | arena dimensionada só pelo primeiro payload; ternário morto; rewrite "clampado" em silêncio | `Diagnostics/src/phase02_harness.c` | `FIXED_PENDING_APPLE_CI` |
| S-003 | "guard não instalável" (−2) reportado como fault (loader e microteste) | `RuntimeCore/src/runtime_loader.c`, harness | `FIXED_PENDING_APPLE_CI` |
| S-004 | `rt_jit_execution_allowed`: escreve sem abrir a janela, ignora o icache, −2 ⇒ "não" falso | `RuntimeCore/src/runtime_jit.c` | `FIXED_PENDING_APPLE_CI` |
| S-005 | dual mapping presume qual operação falhou e classifica EPERM como `UNSUPPORTED` | `runtime_dual_mapping.c`, header, harness | `DEVICE_RETEST_REQUIRED` |
| S-006 | `shm_unlink` final não conferido e `RT_IPC_STAGE_UNLINK` documentado mas inexistente | `RuntimeCore/src/runtime_ipc.c` (+ header) | `FIXED_PENDING_APPLE_CI` |
| S-007 | `getrandom()` devolvendo 0 giraria o laço para sempre | `RuntimeCore/src/linux_platform.c` | `FIXED_PENDING_APPLE_CI` |
| S-008 | DETAIL truncado em 512 chars e relatório truncado em silêncio | `Diagnostics/src/phase02_log.c` | `FIXED_PENDING_APPLE_CI` |
| S-009 | "Save report" grava em `tmp`, enquanto o Info.plist habilita compartilhamento de `Documents` | `RuntimePoC/ContentView.swift` | `FIXED_PENDING_APPLE_CI` |
| S-010 | `rt_fs_links_and_modes` sobrescreve com um `errno` lido depois o errno já capturado por `rt_fs_write_pattern` | `RuntimeCore/src/runtime_filesystem.c` | `FIXED_PENDING_APPLE_CI` |
| S-011 | `rt_fs_links_and_modes` lê `errno` em condição composta que pode ser verdadeira **sem** falha de syscall (`lstat`/`stat` OK + tipo inesperado) | `RuntimeCore/src/runtime_filesystem.c` | `FIXED_PENDING_APPLE_CI` |
| S-012 | `rt_fs_temp_file` reporta escrita curta com `errno` cru (pode ser 0/stale) | `RuntimeCore/src/runtime_filesystem.c` | `FIXED_PENDING_APPLE_CI` |
| S-013 | `rt_ipc_scm_rights` reporta escrita/leitura curta do descritor com `errno` cru | `RuntimeCore/src/runtime_ipc.c` | `FIXED_PENDING_APPLE_CI` |
| S-014 | resumos (`rt_cpu_facts_summary`, `rt_context_describe`, `phase02_log_summary_line`) truncam em silêncio quando não cabem no buffer | `runtime_cpu_abi.c`, `runtime_context.c`, `phase02_log.c` | `FIXED_PENDING_APPLE_CI` |
| S-015 | `rt_signal_roundtrip`: caminho de falha devolve −1 sem escrever errno; falha ao **restaurar** a disposição é descartada e reportada como `PASS` | `RuntimeCore/src/runtime_signals.c` | `FIXED_PENDING_APPLE_CI` |
| S-016 | os cinco round trips de threads devolvem −1 com `*err_out = 0` quando o valor observado diverge do esperado | `RuntimeCore/src/runtime_threads.c` | `FIXED_PENDING_APPLE_CI` |
| S-017 | `rt_mem_protect` sem a proteção de overflow que `rt_mem_reserve` tem (`len + page − 1` envolve e protege uma fração) | `RuntimeCore/src/runtime_memory.c` | `FIXED_PENDING_APPLE_CI` |

### S-001 — resultados JIT descartados (a raiz dos "PASS otimistas")

* **Origem:** auditoria do caminho JIT completo (§1).
* **Primeira evidência:** `(void)rt_jit_begin_write(...)`, `(void)rt_jit_end_write(...)`,
  `(void)rt_jit_invalidate(...)` no microteste (e o mesmo padrão na reescrita).
* **Categoria:** erro engolido / execução não confiável.
* **Causa raiz:** a escrita era feita na arena sem verificar se a janela de escrita foi
  aberta (numa arena `MAP_JIT` isso é uma escrita em memória protegida → `SIGBUS` **fora de
  qualquer guard**) e o flush de icache era descartado — exatamente o "no-op silencioso" que
  o projeto proíbe.
* **Correção:** janela de escrita conferida nos dois pontos (abertura/fechamento), flush
  transformado em **etapa reportada** (`jit.icache_sync`, três desfechos: `PASS` /
  `NOT_APPLICABLE` quando a arquitetura não tem manutenção de icache / `FAIL` quando a
  plataforma tem a capacidade e o flush falha — e nesse caso **nada é executado**).
* **Regressão:** teste unitário de capacidade de payload + auditoria `DISCARDED_RESULTS` /
  `ICACHE_REPORTED` + controle negativo A.
* **Plataforma:** macOS/iOS (o caminho `MAP_JIT`), Linux (o caminho de flush).

### S-002 — dimensionamento da arena e buffers de payload

* **Primeira evidência:** `arena_len = (first_len > second_len) ? first_len : first_len;`
  (expressão morta: nunca escolhe o segundo payload) e `second_len` ainda 0 no momento da
  alocação.
* **Categoria:** defeito latente de memória (escrita além do mapeamento) quando o segundo
  payload fosse maior que o primeiro; e, na correção, um defeito real que **os próprios
  testes pegaram**: emitir os dois payloads no mesmo buffer fazia a primeira escrita copiar
  o segundo payload (`execute_return_42` retornou 4242).
* **Correção:** dois buffers (`payload_first`/`payload_second`), ambos emitidos **antes** da
  alocação, `arena_len = max(first, second)`, verificação explícita de capacidade antes de
  cada cópia e recusa em vez de "clamp" silencioso na reescrita.
* **Regressão:** `test_jit_payload_capacity` (inclui o caso de buffer insuficiente) +
  auditoria `ARENA_SIZING` + controle negativo B.
* **Nota de honestidade:** o defeito de buffer foi introduzido *dentro* desta correção e
  detectado pela suíte antes de qualquer commit — está registrado aqui porque a suíte o
  pegou, não porque ele chegou ao aparelho.

### S-003 — "guard não instalável" ≠ fault

* **Evidência:** `rc != 0` tratado como fault em `rt_loader_run_ex` e no microteste; com
  `rc == -2` a mensagem ficaria "faulted (si_addr=(nil))" — fault que nunca houve.
* **Correção:** `-2` ⇒ `RT_LOADER_ERR_INTERNAL` + errno (loader) e `UNTESTED` com a causa
  explícita (harness); `-1` continua sendo o fault observado.
* **Regressão:** `test_loader_guard_unavailable_is_not_a_fault` (segura um guard externo →
  `EBUSY` → o loader tem de devolver `RT_FAIL`/`INTERNAL`, com o valor de saída intacto) +
  auditoria `GUARD_VS_FAULT` + controle C.

### S-004 — `rt_jit_execution_allowed`

* **Evidência:** escrevia na arena sem abrir a janela quando a alocação veio do caminho
  `MAP_JIT`; `(void)rt_jit_invalidate(...)`; `result != 0 ⇒ 0` transformava "não foi possível
  testar" em "este processo não pode executar memória que escreveu".
* **Correção:** janela conferida, flush honrado, `-2` ⇒ `-1` (não determinado).
* **Regressão:** auditoria `DISCARDED_RESULTS` (o arquivo também é verificado) + suíte JIT.

### S-005 — dual mapping: etapa presumida e classificação inconsistente

* **Evidência física:** `memory.dual_mapping_rw_rx = UNSUPPORTED` /
  `second (executable) view refused errno=1 (Operation not permitted); host=darwin`. O código
  podia falhar em **cinco** pontos (nome único, `shm_open`, `ftruncate`, primeiro `mmap`,
  segundo `mmap`) e o registro **presumia** o segundo `mmap`.
* **Inconsistência adicional:** `EPERM/EACCES` viravam `UNSUPPORTED` no dual mapping enquanto
  o probe de shm (Correção 06) classifica os mesmos errnos como capacidade não concedida.
  O mesmo fato não pode ter duas classificações no mesmo relatório.
* **Correção:** `rt_dual_stage_t` + `rt_dual_stage_name()` com a etapa em cada caminho de
  falha; classificação por etapa+errno com o **mesmo vocabulário** do shm
  (`EPERM/EACCES` → `BLOCKED`; `ENOSYS/ENOTSUP` → `UNSUPPORTED`; divergência entre as duas
  visões com todas as syscalls aceitas → defeito nosso; errno 0 → defeito).
* **Regressão:** `test_dual_mapping_stage_and_classification` + auditorias `DUAL_MAP_STAGE` /
  `CAPABILITY_WORDS` + controles D e E.
* **A linha física de `UNSUPPORTED` permanece intacta** em
  `Documentation/IPHONE13_PHYSICAL_RUN_01.md`: o que muda é o que a **próxima** execução
  passa a poder dizer.

### S-006 — limpeza do objeto de memória compartilhada

* **Evidência:** o relatório da Correção 06 listava `UNLINK` entre as etapas, mas o enum
  `rt_ipc_stage_t` não tinha `RT_IPC_STAGE_UNLINK` e o `shm_unlink` final era descartado.
* **Correção:** `RT_IPC_STAGE_UNLINK` + nome estável; o `shm_unlink` final é conferido e uma
  falha de **limpeza** é reportada como fato de limpeza (o probe continua `PASS`: a capacidade
  foi provada) — nunca confundida com o veredito de capacidade nem engolida.
* **Regressão:** `test_ipc_shm_cleanup_stage` + auditoria `SHM_CLEANUP` + controle F.

### S-007 — progresso zero em `getrandom`

* **Evidência:** `while (filled < length) { got = getrandom(...); if (got < 0) …; filled += got; }`
  — um `got == 0` giraria para sempre.
* **Correção:** `got == 0` ⇒ `EIO`, reportado.
* **Regressão:** auditoria `RANDOM_PROGRESS` + controle G.

### S-008 — truncamento silencioso de evidência

* **Evidência:** `DETAIL` truncado em 512 caracteres sem marcador e relatório truncado ao
  atingir a capacidade do log — e o stage/errno moram **no fim** dessas mensagens.
* **Correção:** marcador explícito `[DETAIL TRUNCATED at 512 chars]` e
  `[PHASE02] NOTE=log-capacity-reached …`, com corte na última linha completa.
* **Regressão:** `test_log_detail_truncation_is_marked` + auditoria `LOG_INTEGRITY` +
  controle H.

### S-009 — "Save report" gravava em `tmp` (corrigido na rodada 02)

* **Origem:** auditoria da rodada 01; causa confirmada por inspeção na rodada 02.
* **Evidência (antes):** `ContentView.swift` usava `NSTemporaryDirectory()` como `workdir`
  **e** como destino do relatório salvo, enquanto `Info.plist` declara `UIFileSharingEnabled` +
  `LSSupportsOpeningDocumentsInPlace`: o compartilhamento de arquivos expõe `Documents`, não
  `tmp`. O relatório salvo ficava inalcançável (sobravam Copy/Share, que foi como a execução
  física 01 saiu do aparelho).
* **Categoria:** persistência de evidência (nenhum resultado de teste estava errado por causa
  disso).
* **Causa raiz:** um único `workdir` servia dois propósitos — diretório de trabalho das suítes
  (que **deve** continuar em `tmp`) e destino do relatório salvo (que deve estar em
  `Documents`, o único exposto).
* **Correção (mínima, só `save()`):** a URL passa a ser
  `FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first`; o `workdir`
  das suítes continua `NSTemporaryDirectory()`; `Phase02Bridge.reportFileName()` continua
  gerando o nome; a gravação continua `try report.write(to:atomically:encoding:)` com os dois
  desfechos reportados em `status`. Nenhuma linha do `body` SwiftUI foi tocada.
* **Regressão:** regra `SAVE_REPORT_TARGET` (destino + as duas chaves do `Info.plist` +
  `workdir` das suítes preservado) e regra `UI_SURFACE_FROZEN` (o bloco `body:` é comparado
  byte a byte com `tools/ui_surface.snapshot.txt`, extraído do commit `631ded6`);
  controles negativos I (volta para `tmp`), J (bundle deixa de expor `Documents`) e K (rótulo
  visível alterado).
* **Plataforma:** iOS (observado por inspeção; a gravação em si é exercitada no aparelho).
* **Status:** `FIXED_PENDING_APPLE_CI`.

---

## B2. Defeitos encontrados na rodada 02 (varredura final)

### S-010 / S-011 / S-012 — `runtime_filesystem.c`: um `errno` que não é o da falha

* **Origem:** varredura final da rodada 02, na família "errno perdido / errno=0".
* **Evidência (antes):**
  1. `rt_fs_links_and_modes`: `if (rt_fs_write_pattern(target, err_out) != 0) { *err_out = errno; }`
     — a função chamada **já** havia escrito o errno da syscall que falhou, e a linha seguinte o
     substituía por um `errno` lido depois (a callee pode ter rodado `close()` no meio; e o
     `EIO` deliberado de escrita curta virava o valor de `errno` que estivesse lá);
  2. `if (lstat(link, &link_info) != 0 || !S_ISLNK(...))` e o par equivalente com `stat`:
     a condição é verdadeira também quando a syscall **funcionou** e o tipo é outro — e nesse
     caso `*err_out = errno` publicava um valor não relacionado, inclusive 0;
  3. `rt_fs_temp_file`: escrita curta (`!= sizeof`) reportada com `*err_out = errno` — escrita
     curta não define errno.
* **Categoria:** propagação de erro (fidelidade de diagnóstico; mesma classe do defeito que
  produziu `errno=0 (Undefined error: 0)` em `IPHONE13_PHYSICAL_RUN_01`).
* **Arquivo:** `RuntimeCore/src/runtime_filesystem.c`.
* **Correção:** (1) a atribuição redundante foi removida — o errno capturado na callee é o que
  vale; (2) `lstat`/`stat` são conferidos separadamente do tipo do que devolveram: errno só
  quando a syscall falhou, `EINVAL` quando o tipo é outro; (3) escrita curta passa a `EIO`
  (`(written < 0) ? errno : EIO`), o mesmo padrão que `rt_fs_write_pattern`/`rt_fs_read_pattern`
  já usavam no mesmo arquivo.
* **Regressão:** regra `ERRNO_CAPTURED_NOW` (padrão exato do defeito 1 e das escritas curtas) +
  `test_failure_carries_and_success_clears_errno` (ENOENT para raiz ausente, ENOTDIR para raiz
  arquivo, sucesso limpa errno) + controles M e N.
* **Plataforma:** iOS (o registro `fs.*` roda no aparelho), Linux/AArch64 (verificado).
* **Status:** `FIXED_PENDING_APPLE_CI`.

### S-013 — `runtime_ipc.c`: transferência curta com `errno` cru

* **Evidência (antes):** no probe de `SCM_RIGHTS`, tanto o `write` no pipe quanto o `read`
  através do descritor recebido reportavam `*err_out = errno` quando a transferência era curta
  (≥ 0 bytes, portanto sem errno definido). O mesmo arquivo já fazia certo no round trip de
  socketpair/pipe (`(wrote < 0) ? errno : EIO`).
* **Correção:** os dois pontos passam a reportar `EIO` na transferência curta; o errno real
  continua sendo o da syscall que falhou.
* **Regressão:** `ERRNO_CAPTURED_NOW` + `test_failure_carries_and_success_clears_errno`
  (`ipc.scm_rights` continua PASS e limpa errno).
* **Status:** `FIXED_PENDING_APPLE_CI`.

### S-014 — resumos truncados em silêncio

* **Evidência (antes):** `rt_cpu_facts_summary`, `rt_context_describe` e
  `phase02_log_summary_line` usavam `snprintf` e só conferiam `written < 0`: com buffer pequeno
  a linha saía cortada sem dizer nada — e é justamente onde vivem os números que o CI usa como
  evidência.
* **Correção:** os três marcam ` [SUMMARY TRUNCATED]` no fim do buffer quando
  `(size_t)written >= cap` (mesma regra do marcador de DETAIL, da rodada 01). Buffer grande o
  bastante ⇒ nenhum marcador, comportamento idêntico ao anterior.
* **Regressão:** `test_summaries_mark_truncation` (marca presente com buffer pequeno e ausente
  com buffer suficiente) + regra `TRUNCATION_MARKED` + controle P.
* **Status:** `FIXED_PENDING_APPLE_CI`.

### S-015 — `rt_signal_roundtrip`: falha sem errno e restauração descartada

* **Evidência (antes):** dois defeitos no mesmo caminho:
  1. quando o handler consultado **não** era o recém-instalado, a função devolvia `-1` sem
     escrever `*err_out` — o chamador (harness usa `int err = 0;`) imprimia `errno=0`, que este
     projeto nunca aceita como resposta de plataforma;
  2. se o `sigaction` que **restaura** a disposição anterior falhasse, o resultado era
     descartado e `*err_out` ainda era zerado logo depois ⇒ o registro saía `PASS` com a
     disposição de sinal possivelmente alterada.
* **Correção:** falha de consulta devolve o errno capturado imediatamente (com a restauração
  tentada antes de sair); handler inesperado devolve `EIO`; falha da restauração devolve o errno
  e `-1` (deixa de ser `PASS`). O caminho de sucesso é idêntico ao anterior.
* **Regressão:** `ERRNO_CAPTURED_NOW` + `test_failure_carries_and_success_clears_errno`
  (SIGUSR1 continua PASS com errno 0; número de sinal inválido falha com errno ≠ 0) + controle Q.
* **Status:** `FIXED_PENDING_APPLE_CI`.

### S-016 — threads: divergência reportada com `errno` de sucesso

* **Evidência (antes):** `rt_thread_roundtrip`, `rt_thread_tls_roundtrip`,
  `rt_thread_mutex_counter`, `rt_thread_condition_pingpong` e `rt_thread_atomics_roundtrip`
  terminavam com `*err_out = 0; return (x == expected) ? 0 : -1;` — divergência devolvia `-1`
  com `errno=0` no relatório, exatamente o que a rodada 01 proibiu.
* **Correção:** cada divergência passa a reportar `EILSEQ` (a convenção do projeto para valores
  que discordam — usada pelos compares de fs/ipc/dual mapping) e o caminho de sucesso fica
  idêntico.
* **Regressão:** `ERRNO_CAPTURED_NOW` (proíbe o ternário `? 0 : -1` e exige os cinco `EILSEQ`) +
  as cinco checagens de sucesso com errno limpo + controle O (quebra os cinco sítios de uma vez).
* **Status:** `FIXED_PENDING_APPLE_CI`.

### S-017 — `rt_mem_protect` sem guarda de overflow (defeito conhecido desde a rodada 01)

* **Evidência (antes):** `rounded = ((len + page - 1u) / page) * page;` sem a guarda
  `len > SIZE_MAX - page` que `rt_mem_reserve` tem: com `len` próximo de `SIZE_MAX` a soma
  envolve, `rounded` fica minúsculo e a chamada protege uma fração do pedido **retornando
  sucesso** — uma perda silenciosa de proteção na suposição de que a região inteira mudou.
* **Correção:** a mesma guarda, com `EOVERFLOW` reportado.
* **Regressão:** `test_mem_protect_rejects_overflow` (recusa + `EOVERFLOW`; e um comprimento
  normal continua sendo protegido) + regra `PROTECT_OVERFLOW` (verifica também que
  `rt_mem_reserve` mantém a sua) + controle L.
* **Status:** `FIXED_PENDING_APPLE_CI`.

---

## B3. Defeitos encontrados na rodada 03 (blockers do run 02 + caminhos destravados)

| ID | Defeito | Arquivo | Status |
| --- | --- | --- | --- |
| S-018 | `rt_jit_alloc` devolvia `NULL` quando o `MAP_JIT` era recusado: o iOS ficava **sem arena nenhuma**, embora o mecanismo W^X de visão única (o mesmo que o run 02 provou funcionar) existisse no próprio runtime | `RuntimeCore/src/runtime_jit.c`, `include/runtime_jit.h` | `DEVICE_RETEST_REQUIRED` |
| S-019 | `rt_jit_execution_allowed` chamava `rt_mem_protect(R-X)` numa arena `MAP_JIT` (transição errada: as proteções são fixadas na criação e a janela de escrita é o mecanismo suportado) | `RuntimeCore/src/runtime_jit.c` | `DEVICE_RETEST_REQUIRED` |
| S-020 | O loader copiava o código para dentro de uma arena `MAP_JIT` **sem abrir a janela de escrita** e em seguida chamava `mprotect` nela | `RuntimeCore/src/runtime_loader.c` | `DEVICE_RETEST_REQUIRED` |
| S-021 | `jit.make_executable` podia ser `PASS` com base no retorno de `mprotect`, sem execução: em aparelhos com TXM o kernel aceita a transição e retira a permissão de execução | `Diagnostics/src/phase02_harness.c` | `DEVICE_RETEST_REQUIRED` |
| S-022 | A cadeia JIT não verificava a escrita (read-back) nem registrava a liberação da arena: os treze passos não eram todos observáveis | `Diagnostics/src/phase02_harness.c` | `DEVICE_RETEST_REQUIRED` |
| S-023 | O experimento de dual mapping não conseguia separar "namespace POSIX nomeado recusado" de "alias executável recusado", e não havia backing compatível com o sandbox iOS | `RuntimeCore/src/runtime_dual_mapping.c`, `include/runtime_memory.h`, harness | `DEVICE_RETEST_REQUIRED` |
| S-024 | Contabilidade das quatro camadas de entitlement incompleta: o estado de fiação não era auditado, o conteúdo do arquivo não distinguia decisão de omissão e o script de inspeção não reportava L1/L2 sem macOS | `RuntimePoC/WinlatorPhase02.entitlements`, `tools/inspect_entitlements.sh`, `tools/build_ios.sh`, harness, `tools/generate_xcodeproj.py` | `FIXED_PENDING_APPLE_CI` |
| S-025 | Instrumentação: detectores ancorados em formas antigas do código (regra `JIT_ATTEMPT_ORDER` sobre o wrapper, controles negativos do Fix 06 e da estabilização) — falhavam alto, não em silêncio, mas deixariam de medir | `tools/audit_fix06_contracts.py`, `tools/fix06_negative_controls.py`, `tools/stabilization_negative_controls.py` | `FIXED_PENDING_APPLE_CI` |

### S-018 — a arena que o iOS precisava era recusada junto com o `MAP_JIT`

O run 02 mediu `jit.alloc = BLOCKED` com `errno=1`, o mesmo do probe. O grafo causal estava
certo, mas a consequência era um port **sem caminho de execução**: `rt_jit_alloc` tratava a
recusa do `MAP_JIT` como fim, embora `memory.rw_to_rx_transition = PASS` e `memory.wx_policy =
PASS` provem, no mesmo aparelho, que a visão única RW ↔ R-X funciona. A correção **não** é
"usar um `mmap` comum para o teste ficar verde": `rt_jit_alloc_ex` tenta o `MAP_JIT`, guarda o
errno da recusa (`map_jit_refused_errno_out`), marca a arena com o mecanismo que a produziu
(`RT_JIT_ARENA_ANON_MAP_JIT_REFUSED`) e cai no mecanismo W^X que o projeto já usa onde não há
`MAP_JIT` — com a recusa publicada no registro e com a execução verificada de verdade. O
`MAP_JIT` continua `BLOCKED` em `jit.map_jit_probe`; o que muda é que a ausência da capacidade
deixa de ser a ausência de arena.

### S-019 / S-020 — transição errada em região `MAP_JIT`

Ambos eram latentes no run 02 (nada executou, porque não havia arena) e são provados pela
leitura do contrato da API: numa região `MAP_JIT` as proteções são fixadas na criação
(estilo `PROT_MAX`) e a operação suportada é abrir/fechar a janela de escrita. Os dois
caminhos agora ramificam pelo **tipo da arena** — janela para `MAP_JIT`, `rt_mem_protect` para
a arena anônima — no microteste, no loader e em `rt_jit_execution_allowed`.

### S-021 / S-022 — "PASS sem execução" e cadeia incompleta

`jit.make_executable` só é `PASS` **depois** de a chamada real devolver o valor esperado: se o
kernel aceitar a transição e a execução falhar dentro da arena, o registro é `BLOCKED` e diz
que o aparelho não honrou a permissão; se falhar fora da arena, é `FAIL` do código emitido —
nunca um `PASS` tirado do retorno de um `mprotect`. A cadeia ganhou verificação da escrita por
read-back (`jit.write_payload`) e o registro da liberação (`jit.free`), fechando os treze
passos exigidos.

### S-023 — o experimento de dual mapping no iOS

A recusa em `shm_open` é do **namespace nomeado**, não da memória compartilhada em geral: o
backend do iOS passou a usar um objeto **file-backed** `MAP_SHARED` dentro do container do
app, desvinculado imediatamente, com duas visões reais (RW e R-X) e verificação de alias —
selecionado explicitamente, só em alvo Apple iOS e só **depois** de a via nomeada ser recusada.
Linux e macOS continuam na via original, que não foi removida. O experimento continua sendo
uma *medida de capacidade*, nunca um requisito: a estratégia do iOS é a visão única.

### S-024 — a fiação do entitlement é decisão, não defeito

Investigação completa: `RuntimePoC/WinlatorPhase02.entitlements` não é referenciado por
`CODE_SIGN_ENTITLEMENTS` em nenhuma configuração, e isso é uma **decisão documentada**, agora
auditada (`tools/audit_entitlement_config.py`, regra `E2`): em plataformas iOS toda entitlement
precisa estar na allowlist do perfil de provisionamento, e um arquivo que pede uma entitlement
que o perfil não permite **quebra o build/instalação assinados** ("provisioning profile does
not include the … entitlement"; dispositivo: `0xE8008016`). Fiar o arquivo não obteria a
capacidade e destruiria o único caminho que valida algo — a instalação do dono do aparelho. O
que passou a existir: a decisão escrita no arquivo, o *opt-in* explícito em `tools/build_ios.sh`,
a nota de quatro camadas no harness, a auditoria e a extensão do `inspect_entitlements.sh` (que
agora reporta L1 e L2 sem macOS e mantém
`ENTITLEMENT_SIGNATURE_INSPECTION=UNTESTED REASON=NO_APPLE_TOOLCHAIN`). Se um dia for fiada,
tem de ser em **Debug e Release juntos** — a auditoria proíbe o subconjunto.

### S-025 — detectores ancorados em código que mudou

Os três controles/regras que apontavam para a forma antiga falharam de forma **visível**
(asserção de âncora), o que é o comportamento correto de um detector honesto; as âncoras foram
atualizadas para a forma nova **sem enfraquecer o defeito plantado**, e o invariante do Fix 06
(`JIT_ATTEMPT_ORDER`) passou a ser verificado na função que realmente mapeia
(`rt_jit_alloc_ex`), com o mesmo efeito de detecção comprovado pelo controlo C.

## B4. Defeitos encontrados na rodada 04 (terminação física do run 03)

| ID | Defeito | Arquivo | Status |
| --- | --- | --- | --- |
| S-026 | O caminho comum executava, no aparelho, a operação que o iOS 26/TXM não permite: entrar em memória que o próprio processo tornou executável (arena JIT e view executável de objeto) e entrar no ponto de entrada do módulo mapeado. No iPhone 13 a permissão de execução é retirada **sem erro reportado**, e a execução da página termina o processo — derrubando junto o relatório e qualquer diagnóstico | `Diagnostics/src/phase02_harness.c`, `Diagnostics/include|src/phase02_execution_policy.*`, `RuntimePoC/Phase02Bridge.m` | `DEVICE_RETEST_REQUIRED` |
| S-027 | `rt_signal_controlled_segv` produzia o fault deliberado escrevendo **através de um ponteiro nulo** — undefined behaviour que o UBSAN reporta (`store to null pointer of type 'volatile int'`) e que um otimizador pode remover. Nenhum sanitizador podia rodar sobre a suíte de sinais | `RuntimeCore/src/runtime_signals.c`, `include/runtime_signals.h` | `FIXED_PENDING_APPLE_CI` |

### S-026 — a operação que o alvo não permite era executada pelo caminho comum

**Causa raiz (única, e explicando as quatro suítes).** Não é um defeito de suíte: é do *prefixo
comum*. `phase02_run_suite` / `phase02_run_all` executam, em toda suíte, os mesmos passos —
inclusive os que pedem permissão de execução a páginas que o processo escreveu. No iPhone 13
(A15, iOS 26, TXM/SPTM) essa permissão **não é concedida a um app de terceiros**: o `MAP_JIT`
sem `dynamic-codesigning` é recusado, `mprotect(PROT_READ|PROT_EXEC)` pode **retornar sucesso e
deixar a página não executável**, e a entrada nessa página termina o processo. O IPA do run 03
continha exatamente as mudanças da pass 03 que passaram a *tentar* essa operação nas suítes
memory/jit/loader (S-018…S-023); a suíte `cpu` e a suíte `threads`, que nunca tocaram código
pass 03, morrem pelo mesmo motivo: elas **não** morrem pela operação JIT, e sim porque o
processo é único — a terminação não tem escopo. Foi por isso que a investigação exigiu mapear o
prefixo comum antes de atribuir o crash ao JIT, e por isso a correção é uma **política de
execução** aplicada no prefixo do harness e na ponte, com o relatório e o journal gravados
**antes** de cada passo perigoso.

**Por que não é mascaramento.** A tentativa não foi removida do produto: existe um *opt-in*
explícito (`PHASE02_ALLOW_EXECUTION=1`, ou `PHASE02_IOS_ALLOW_EXECUTION_ATTEMPT=1` na
compilação) para um ciclo físico dedicado, e o resultado, quando a operação é adiada, é
`BLOCKED` com a política nomeada — nunca `PASS`, nunca um veredito de plataforma. Em Linux,
macOS e macOS-host a cadeia continua executando e **provando execução executando** (o host
segue com `jit` = 11 PASS, sem nenhuma mudança de número). O que muda no aparelho é que a
execução do diagnóstico não pode custar o processo inteiro: sem relatório não há evidência, e
sem evidência a próxima rodada não sabe onde parar.

**Correção (mínima, incremental, sem tocar UI).**

1. `Diagnostics/src/phase02_execution_policy.c` — política pura e testável: perfis
   (`ios-device` com as duas operações fenced, `ios-simulator`, `apple-host`, `portable`),
   `phase02_execution_decide(platform_allows, attempt)` e razões que citam o run 03. A
   simulação de perfil (`PHASE02_TARGET_OVERRIDE`) existe só para exercitar em host as linhas
   que o aparelho cerca, e **se rotula** `profile-source=override-simulation-not-device-evidence`.
2. `Diagnostics/src/phase02_progress.c` — registrador de voo: `write(2)` por checkpoint (uma
   escrita que retornou já está no kernel ⇒ sobrevive a SIGKILL), `O_APPEND`, sem alocação,
   sem `stdio` com buffer, sem handlers; mais o *write-ahead* do relatório antes de cada passo
   perigoso. Um diário que diz `WRITE_AHEAD_OK` nunca está à frente do que existe em disco.
3. `phase02_harness.c` — o passo perigoso passa a ter ramo adiado em três pontos (cadeia JIT,
   view executável do dual mapping, entrada do módulo), cada um registrando `BLOCKED` com a
   razão e **sem** executar nada; e checkpoints nos pontos de entrada
   (`HARNESS_ENTER`, `SUITE_ENTER`, `SUITE_FIRST_TEST_ENTER`, `SUITE_EXIT`) com write-ahead.
4. `Phase02Bridge.m` — jornais + checkpoints do caminho do botão
   (`RUN_SELECTED_ENTER`, `HARNESS_INIT_ENTER/OK`, `SUITE_DISPATCH_ENTER/END`,
   `REPORT_FINALISED`, `RUN_SELECTED_EXIT`) gravando em `NSDocumentDirectory` (visível no app
   Arquivos). A ordem de consumo do log não mudou: `stringWithUTF8String:` **antes** de
   `phase02_log_free`, e `@""` quando a conversão falha — nunca um ponteiro morto, nunca um
   `nil` devolvido a Swift.
5. `Tests/test_run_selected.c` (novo, 206 verificações) — o botão, exercitado: as nove seleções
   pelo mesmo ponto de entrada, segunda pressão, sequência
   `memory→cpu→threads→memory→jit→cpu→all`, `memory×10`/`cpu×10`/`threads×10`, seleção
   impossível, journal/write-ahead, matriz da política e o **perfil iOS-classe simulado**
   (que prova, em host, que a mesma seleção produz relatório íntegro com a tentativa adiada e
   que `cpu`/`threads` ficam **idênticos** nos dois perfis).
6. `tools/pass04_negative_controls.py` (novo, 25/25) — o controle da classe do crash: uma
   vítima real ligada aos fontes reais do projeto é morta por `SIGKILL` no meio da execução; o
   detector tem de nomear o último checkpoint, dizer `TERMINATED_BEFORE_SUITE_EXIT` e encontrar
   o relatório em disco — com o gêmeo negativo mostrando que ele **não** é sempre-verde.

**Testes.** Host: `phase02_run_selected_tests` 206/206; CTest 3/3; unit 298/0; suíte completa
`records=60 pass=56 fail=0`; cada suíte individual sem regressão de número. AArch64/QEMU:
`phase02_run_selected_tests` 206/206, unit 289/0, suíte completa igual. ASAN=PASS, UBSAN=PASS
(ver S-027). Controles: 18/18, 6/6, 19/19, 25/25. Auditorias: todas em 0 (entitlement, JIT
causal, SHM, interface, composição, link, includes/APIs Apple) e
`validate_xcodeproj.py` 62/62.

**O que continua pendente.** `DEVICE_RETEST_REQUIRED`: nada aqui é confirmado no aparelho. O
próximo IPA leva o diário e o write-ahead; se o processo ainda terminar, o diário diz onde; se
não terminar, o relatório dirá `BLOCKED` com a política nomeada e a rodada seguinte terá
evidência para decidir sobre a tentativa dedicada (`PHASE02_ALLOW_EXECUTION=1`).

### S-027 — UB na sonda de fault (achado do UBSAN, não é a causa do crash)

A primeira execução de UBSAN deste projeto (rodada 04) abortou em três binários com
`runtime_signals.c:236:17: runtime error: store to null pointer of type 'volatile int'`: o
fault deliberado era uma escrita através de `(volatile int *)0`. Isso é *undefined behaviour*
(a norma não garante nem que a escrita aconteça) e por isso nenhum sanitizador podia rodar
sobre a suíte de sinais — e a sonda depende de o fault acontecer exatamente ali. A correção não
enfraquece nada: o alvo do fault passou a ser uma página **do próprio processo** tornada
inacessível (`rt_mem_reserve` + `rt_mem_protect(RT_PROT_NONE)`), liberada em todos os caminhos
de retorno. Mesmo sinal, mesma guarda, mesmas semânticas de `si_addr` — agora sobre um fault
definido (`si_addr=0x7f06b305b000` no log do host). `RT_PROT_NONE` não envolve permissão de
execução alguma: a sonda não toca a superfície JIT/entitlement. `UBSAN=PASS`, `ASAN=PASS`.

## C. Auditados e considerados NÃO defeitos (com o motivo)

| Item | Motivo |
| --- | --- |
| `memory.rx_to_rw_transition` reporta `PASS` quando o `mprotect` de volta é **recusado** | convenção documentada do projeto: a recusa é a *observação* de W^X estrito (mesmo padrão de `memory.wx_policy`); o errno vai no DETAIL; nada foi comprovado como errado |
| `rt_mem_release()` não arredonda o `len` como `rt_mem_reserve`/`rt_mem_protect` | contrato assimétrico, sem efeito observável: `munmap` arredonda o comprimento para páginas e todos os chamadores passam o mesmo `len` que mapearam; registrado, não alterado (evita refatoração cosmética) |
| `rt_dual_map_destroy()` com retorno descartado no harness | limpeza best-effort; a falha possível é `munmap` de mapeamento válido, que os testes não observam; o objeto já foi desvinculado por `shm_unlink` |
| `rt_ipc_shm_ex` usa `length = 4096` mesmo com página de 16 KiB | `mmap` arredonda o comprimento para a página e a parte final da página é preenchida com zeros; o probe escreve 8 bytes — auditado, sem defeito |
| Suítes de threads/sinais/CPU (PASS físico) | nenhuma interação nova com os caminhos JIT/loader corrigidos: o mesmo `rt_signal_call_guarded` é usado, sequencialmente, sem estado novo compartilhado; **não foram modificadas** |
| `RuntimePoC/main.c` fora do alvo Xcode | correto: o app usa `@main` do SwiftUI; compilar `main.c` geraria símbolo `main` duplicado |
| `.entitlements` não ligado a `CODE_SIGN_ENTITLEMENTS` | decisão deliberada e documentada no próprio arquivo; ligar quebraria a instalação sem o perfil correto (ver L-005) |
| `jit.write_protect_np = NOT_APPLICABLE` no iPhone | propriedade da API para o alvo iOS (o SDK marca `pthread_jit_write_protect_np` indisponível). Nunca é `PASS` e nunca é defeito; a rodada 03 não reintroduziu a chamada (regra `J10`) |
| `jit.execution_allowed = UNTESTED` no run 02 | consequência, não causa: sem arena executável o probe não conclui nenhum passo. A rodada 03 corrigiu a **implementação** (janela por tipo de arena) e o registro passou a nomear a dependência — `UNTESTED` continua possível, mas só quando um passo do próprio probe não conclui |
| `memory.dual_mapping_rw_rx = BLOCKED` (run 02) | o experimento é medida de capacidade, não requisito do port; a recusa foi em `shm_open` (namespace nomeado). A rodada 03 acrescentou o backend iOS e não promoveu o status |
| `shm_open` recusado no iOS | não significa "memória compartilhada impossível": `MAP_SHARED` anônimo, objeto file-backed no container e `mach` VM não são afetados, e o runtime não depende do namespace nomeado |
| `rt_dual_map_views_aliased` retorna −1 para mapa destruído | contrato documentado (mapa inutilizável); o teste negativo novo depende exatamente disso |
| `rt_jit_alloc` (wrapper) mantido | superfície pública preservada: a rodada 03 **adiciona** `rt_jit_alloc_ex` e mantém o wrapper com o contrato antigo (compatibilidade com testes e chamadas existentes) |
| `Info.plist` sem `MinimumOSVersion` / com `UIFileSharingEnabled` | o Xcode injeta `MinimumOSVersion` de `IPHONEOS_DEPLOYMENT_TARGET=16.0` no processamento; o bundle físico validado no CI já mostrou o executável `arm64` e o `CFBundleIdentifier` esperado; a execução física 01 **instalou e abriu** o app |
| Debug/Release do `.pbxproj` | conferidos: mesmas configurações relevantes nos dois (deployment target, bridging header, `GENERATE_INFOPLIST_FILE=NO`, `CODE_SIGN_STYLE`), sem divergência |
| Fallback de 4096 quando `sysconf(_SC_PAGESIZE)` falha (nos dois backends e em `rt_platform_page_size`) | o valor real é **medido** (`_SC_PAGESIZE`) e o relatório carrega o medido — a execução física registrou `page_size=16384`, que é o valor do próprio iPhone 13; o fallback só seria alcançado com um `sysconf` quebrado, e trocá-lo por falha mudaria o contrato de `rt_mem_*` em caminhos `PASS` sem defeito demonstrado. Coberto pela nova regra `PAGE_SIZE_MEASURED`, que proíbe escrever o tamanho da página no código |
| `(void)rt_mem_protect(region, page, RW, &err)` no fim do probe de RWX do harness | é a restauração best-effort antes de `rt_mem_release()`; a concessão de RWX já foi registrada no registro `memory.wx_policy`, o mapeamento é liberado na linha seguinte e a recusa não tem consequência observável — registrado, não alterado |
| `rt_signal_call_guarded` / estado residual de guarda como causa do crash de cpu/threads | **excluído por leitura e por medição**: a função aparece exatamente duas vezes, ambas dentro do microteste JIT, e `runtime_cpu_abi`/`runtime_threads` não chamam nenhum código da pass 03; além disso, o perfil iOS-classe simulado mostra `cpu` e `threads` com números **idênticos** ao perfil nativo (o crash das quatro suítes não pode ser atribuído a estado de guarda sem evidência nova) |
| Fiação do entitlement como correção | `project.pbxproj` continua com **0** referências ao arquivo de entitlements; removê-lo ou fiá-lo não corrige nada e fiá-lo quebraria a instalação (ver L-005/S-024). A rodada 04 não alterou o arquivo nem a decisão |
| Consumo do log pela ponte (`stringWithUTF8String:` sobre buffer morto) | auditado: `phase02_log_t` é heap, sempre terminado em NUL, com marcadores de truncamento; a ponte converte **antes** de `phase02_log_free` e devolve `@""` se a conversão falhar (caminho ilegível ≠ ponteiro inválido) |
| Execução fora da main thread na ponte | correto e necessário (C sincrônico): o estado Swift só é tocado em `DispatchQueue.main.async`; os botões ficam desabilitados durante a execução, então não há reentrância; seleção inválida é `UNSUPPORTED`, sem crash |
| `phase02_log_record` com buffer de detalhe na pilha (512 B) | truncamento **marcado** (`[DETAIL TRUNCATED at 512 chars]`), `vsnprintf` com capacidade, zero sem `vsnprintf` válido: sem leitura fora do buffer e sem perda silenciosa |
| "Crash por sinal capturado por handler de guarda durante o prefixo" | auditado: a guarda só é instalada dentro do microteste JIT e é desinstalada em seguida; o handler re-emite sinais que não são dele; nenhum handler genérico foi instalado na rodada 04 (proibido por regra) |
| Numeração/estado das suítes em `--suite all` após a política | verificado em host: o resumo agrega os mesmos registros com `blocked` apenas nos pontos adiados; em perfil nativo os números da suíte completa são os mesmos da pass 03 (`records=60 pass=56 fail=0 unsupported=2 n/a=2`) |
| Símbolos duplicados/indefinidos no alvo Apple | `LINK_SYMBOL_AUDIT=PASS` (objetos AArch64, símbolo indefinido por objeto, provedor localizado) e `TARGET_COMPOSITION=0`; o link `iphoneos` continua sendo veredito do linker da Apple (`APPLE_LINK_AUDIT=UNTESTED REASON=NO_APPLE_TOOLCHAIN`) |

---

## D. Contagem

```
Rodada 01 (encerrada em 631ded6)
  KNOWN_ISSUES_AT_START            = 8
  CODE_DEFECTS_FOUND_THIS_ROUND    = 8      (S-001 … S-008)
  CODE_DEFECTS_FIXED_THIS_ROUND    = 8      (S-001 … S-008)
  KNOWN_UNFIXED_CODE_DEFECTS       = 1      (S-009 + S-017 reconhecido no ledger)

Rodada 02 (stabilization pass 02) — a rodada de fechamento
  KNOWN_CODE_DEFECTS_BEFORE_PASS_02 = 2     (S-009, S-017)
  NEW_CODE_DEFECTS_FOUND_PASS_02    = 7     (S-010 … S-016)
  CODE_DEFECTS_FIXED_PASS_02        = 9     (S-009 … S-017)
  KNOWN_UNFIXED_CODE_DEFECTS        = 0
  APPLE_CI_RETEST_REQUIRED          = 9     (S-009 … S-017)
  IPHONE_RETEST_REQUIRED            = 5     (L-001, L-002, L-003, L-004, S-005)
  EXTERNAL_CAPABILITY_BLOCKED       = 1     (L-005, entitlement de JIT)
```

```
Rodada 03 (blocker resolution pass 03, base f81dda2)
  ROOT_CAUSES_IDENTIFIED            = 3      (R1 entitlement de JIT/dynamic-codesigning recusada
                                              ao app; R2 o runtime exigia MAP_JIT para ter arena
                                              (S-018); R3 o sandbox iOS recusa o namespace POSIX
                                              nomeado em shm_open, e o experimento não tinha
                                              backing compatível (S-023))
  CODE_DEFECTS_FOUND_THIS_ROUND     = 8      (S-018 … S-025)
  CODE_DEFECTS_FIXED_THIS_ROUND     = 8      (S-018 … S-025)
  KNOWN_UNFIXED_CODE_DEFECTS        = 0
  DEPENDENT_BLOCKED_TESTS           = 4      (jit.alloc, loader.run_valid_module,
                                              memory.dual_mapping_rw_rx, jit.execution_allowed)
  EXTERNAL_CAPABILITY_BLOCKED       = 2      (R1 entitlement de JIT no iOS; R3 namespace POSIX
                                              nomeado no sandbox iOS - o port não depende dele)
  APPLE_CI_RETEST_REQUIRED          = 7      (S-018 … S-024)
  IPHONE_RETEST_REQUIRED            = 12     (jit.map_jit_probe, jit.alloc, jit.write_payload,
                                              jit.make_executable, jit.execute_return_42,
                                              jit.rewrite_payload, jit.execute_return_4242,
                                              jit.free, jit.execution_allowed,
                                              memory.dual_mapping_rw_rx, ipc.posix_shm,
                                              loader.run_valid_module)

Rodada 04 (run 03 crash investigation, base d83332a)
  EVENT                            = IPHONE13_PHYSICAL_RUN_03 (terminação de processo na ação
                                     Run Selected, sem relatório; campos provisórios registrados
                                     em Documentation/IPHONE13_PHYSICAL_RUN_03.md)
  ROOT_CAUSES_IDENTIFIED           = 1      (R4: o prefixo comum, comum a toda suíte, executava a
                                              operação que o iOS 26/TXM não permite - entrar em
                                              memória que o processo tornou executável - e a
                                              terminação leva junto o relatório e o diagnóstico)
  CODE_DEFECTS_FOUND_THIS_ROUND    = 2      (S-026 crash-class; S-027 UB na sonda de fault,
                                              encontrado pelo primeiro UBSAN do projeto)
  CODE_DEFECTS_FIXED_THIS_ROUND    = 2      (S-026, S-027)
  KNOWN_UNFIXED_CODE_DEFECTS       = 0
  AFFECTED_SELECTED_SUITES         >= 4     (memory, jit, cpu, threads - relatado no aparelho)
  HOST_SELECTED_SUITE_TESTS        = 206    (Tests/test_run_selected.c, host)
  AARCH64_SELECTED_SUITE_TESTS     = 206    (o mesmo teste sob qemu-aarch64)
  CRASH_CLASS_NEGATIVE_CONTROL     = 25/25  (tools/pass04_negative_controls.py; SIGKILL real,
                                              não destrutivo, com gêmeos negativos)
  ASAN                             = PASS   (host: pontos de entrada do botão + todas as suítes)
  UBSAN                            = PASS   (host: idem, após S-027)
  APPLE_CI_RETEST_REQUIRED         = 2      (S-026, S-027)
  IPHONE_RETEST_REQUIRED           = 1      (o próprio run 03: a validação final é física)
```

Nenhum defeito é contado duas vezes por sintomas dependentes: S-005 cobre a etapa **e** a
classificação do mesmo caminho de dual mapping (uma causa, um defeito); L-001 e S-002 são
causas distintas; S-010/S-011/S-012 são três decisões independentes no mesmo arquivo (o
`errno` sobrescrito depois da captura, o `errno` lido quando **não** houve falha de syscall e a
escrita curta tratada como erro com errno), cada uma com o seu próprio controle negativo.

`KNOWN_UNFIXED_CODE_DEFECTS = 0`: todo defeito de código conhecido da Fase 02 está corrigido e
com regressão. Na rodada 03, nenhum dos cinco `BLOCKED` do run 02 foi reclassificado: dois são
probes de capacidade (um deles externo e não obtenível), três são dependentes de uma causa
única cada, e o `UNTESTED` é dependente do caminho de arena — que a rodada 03 destravou de
verdade (implementação + verificação por execução), não por rótulo. O que permanece **não** é defeito de código: a concessão da entitlement de JIT
(`EXTERNAL_CAPABILITY_BLOCKED=1`, dependente de assinatura/provisioning) e as confirmações que
só o Apple CI real e o iPhone podem dar (`APPLE_CI_RETEST_REQUIRED`, `IPHONE_RETEST_REQUIRED`).
