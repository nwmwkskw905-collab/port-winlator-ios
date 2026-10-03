# IPHONE13_PHYSICAL_RUN_03 — registro do evento (run físico 03)

`PHASE_02_RECONSTRUCTED_POC`.

**Natureza deste documento.** Diferente de `IPHONE13_PHYSICAL_RUN_01.md` e
`IPHONE13_PHYSICAL_RUN_02.md`, este arquivo **não transcreve um log capturado**: a execução 03
terminou antes de produzir relatório, e nenhum artefato do aparelho foi recebido. O que existe
é o **relato do dono do aparelho** (usuário do projeto) e a árvore que gerou o IPA. Tudo abaixo
está marcado como relato, nunca como medição desta máquina ou de CI. Nada aqui é inventado,
nada é atribuído ao aparelho sem essa marcação, e nada foi convertido em `PASS`.

* **Commit do IPA:** `d83332a` (árvore = pass 03 `4552f6e` + `APPLY_INSTRUCTIONS.md`; conferido
  por `git diff --stat 4552f6e d83332a` → apenas esse arquivo, +38 linhas).
* **Build:** `ios-device`, `arm64`, `page_size=16384` (mesma configuração do run 02, que
  instalou e executou).
* **Data:** 2026-10-03 (relato).

## Relato do evento

| Campo | Valor (relato, verbatim do pedido) |
| --- | --- |
| Abertura do app | abre normalmente (`APP OPENS`) |
| Ação | `Run Selected` fecha o app imediatamente |
| Reprodução | individual com Memory, JIT, CPU, Threads (≥ 4 suítes) |
| Relatório | nenhum (a ação termina com o processo) |

## Campos provisórios (registrados exatamente como fornecidos)

```
IPHONE13_PHYSICAL_RUN_03   = CRASH
PHYSICAL_CRASH_REPRODUCED  = YES
AFFECTED_SELECTED_SUITES   >= 4
MEMORY_CRASH               = YES
JIT_CRASH                  = YES
CPU_CRASH                  = YES
THREADS_CRASH              = YES
ROOT_CAUSE                 = UNKNOWN
```

Estes campos **não** são `FAIL`, `BLOCKED` ou `PASS`: o evento é uma terminação de processo não
classificada até que o ponto de terminação seja localizado. A investigação da pass 04
(`PHASE02_IPHONE_RUN03_CRASH_FIX_04.md`, entrada `S-026` do ledger) trata exatamente isso e
mantém a classificação final pendente de novo teste físico.

## O que este documento NÃO afirma

* não afirma que o JIT é a causa (a reachability no grafo de chamadas foi auditada, não suposta);
* não afirma que o app foi corrigido (a validação final é física);
* não transcreve log de aparelho, porque não houve relatório para transcrever;
* não substitui `IPHONE13_PHYSICAL_RUN_01` e `_02`, que permanecem imutáveis.

## Instrumentação adicionada para o próximo IPA (pass 04)

O próximo ciclo físico carrega o registrador de voo (`Diagnostics/src/phase02_progress.c`):
cada etapa grava uma linha `event=…` em `<Documents>/phase02-run-journal.txt` **antes** de
executar o passo que pode terminar o processo, e o relatório acumulado é escrito em
`<Documents>/phase02-report-ahead.txt`. Uma terminação sem cooperação deixa os dois arquivos,
recuperáveis pelo app Arquivos (sem cabo, sem depurador) e legíveis pelo mesmo leitor que os
testes usam (`phase02_progress_last_event`). A última linha do diário nomeia o passo exato.

Os checkpoints do caminho "Run Selected" são, em ordem:
`RUN_SELECTED_ENTER` → `HARNESS_INIT_ENTER` → `HARNESS_INIT_OK` → `REPORT_INITIALISED` →
`SUITE_DISPATCH_ENTER suite=…` → (por suíte) `HARNESS_ENTER` → `SUITE_ENTER` →
`SUITE_FIRST_TEST_ENTER` → `SUITE_EXIT` → `SUITE_DISPATCH_END` → `REPORT_FINALISED` →
`RUN_SELECTED_EXIT`, com `WRITE_AHEAD_OK` imediatamente antes de cada passo que muda proteção de
memória ou entra em código gerado.
