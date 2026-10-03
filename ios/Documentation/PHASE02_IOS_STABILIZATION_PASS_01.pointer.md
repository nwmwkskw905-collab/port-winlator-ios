# Ponteiro — relatórios das rodadas de estabilização iOS (pass 01 e pass 02)

Os relatórios de entrega das rodadas são gerados **fora da árvore** (raiz do workspace) porque
registram o SHA-256 do próprio pacote: um pacote não pode conter o hash de si mesmo.

* pass 01 → `PHASE02_IOS_STABILIZATION_PASS_01.md` (base `e50db04`, commit `631ded6`)
* pass 02 → `PHASE02_IOS_STABILIZATION_PASS_02.md` (base `631ded6`, rodada de fechamento:
  `KNOWN_UNFIXED_CODE_DEFECTS=0`)

Dentro da árvore ficam:

* `Documentation/PHASE02_IOS_ERROR_LEDGER.md` — inventário completo de erros (IDs, causas
  raiz, correções, regressões, plataformas, dependências externas, estados e contagem);
* `Documentation/RELATORIO_FASE_02_RECONSTRUIDA.md`, seções **16** (pass 01) e **17** (pass 02)
  — a narrativa técnica de cada rodada, a auditoria, os defeitos corrigidos, a regressão e o
  estado de cada subsistema;
* `tools/audit_ios_stabilization.py` + `tools/stabilization_negative_controls.py` — as
  15 regras e os 18 controles negativos que impedem o retorno de cada defeito;
* `tools/pass02_scope_report.py` — prova, arquivo por arquivo, que a rodada mudou exatamente o
  que os defeitos exigiram (`PASS02_SCOPE_OUT_OF_SCOPE=0`).
