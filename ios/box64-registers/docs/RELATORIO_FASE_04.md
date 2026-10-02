# RELATÓRIO FASE 04 — BOX64 ARM64 REGISTER ADAPTATION

**Fase:** 04 — Adaptação de registradores ARM64 do Box64 para Darwin/iOS
**Estratégia implementada:** `SELECTED_STRATEGY=A` (guest R8: host **x18 → x9**), com preservação explícita nas 4 fronteiras de helper C.
**Base de código:** box64 upstream v0.4.4, commit `2f130fab1d6e1a4ee8a71dc60cfdfcc839ad192a`
**Workspace:** `winlator` @ `b6b2259158cf38d06067c34430d840d56b46d220` (submódulo `app` @ `a030f552f452158a2db64fdb32b490fa19c0b48d`)
**Binário de referência (censo):** `app/src/main/assets/box64/usr/local/bin/box64`, 30 725 672 B, sha256 `9c151739…c2608ca9`
**Data:** 2026-10-02
**Classificação final:** **`PHASE_04_STRUCTURALLY_COMPLETE`** + **`IPHONE_VALIDATION_PENDING`**

> **Como classificar cada resultado (regra da Fase 02, mantida).** Nada neste relatório é `CONFIRMADA EM DISPOSITIVO FÍSICO`. As medições de código emitido, frame e fuzz rodaram sob **`qemu-aarch64` em host Linux** e portanto são, no melhor caso, `CONFIRMADA POR TESTE EM SIMULADOR (SIMULATOR ONLY)`. As compilações cross são `CONFIRMADA POR BUILD` (objetos/TUs), nunca build completo. O que não foi executado está marcado `IMPLEMENTADA MAS NÃO VALIDADA`, `HIPÓTESE` ou `BLOQUEADA`. `IPHONE_VALIDATION_PENDING` acompanha a fase inteira e **não** é substituído por nenhum resultado de host.

---

## 1. Estado inicial

### 1.1 O que existia no começo desta fase

* **Nenhum arquivo do box64 havia sido modificado** para esta fase; nenhum branch da Fase 04 existia; nada commitado.
* O material produzido nas Fases 01–03 **não estava mais no workspace**: a árvore `ios/box64-platform/` (30 arquivos / 3 373 linhas) e o `RELATORIO_FASE_03.md` foram perdidos junto com o histórico git local. Evidência preservada do truncamento: `/tmp/arena-workspace/hydrate.zip` (161 583 499 B, 454 entradas, **zero** diretórios `.git`, **zero** arquivos `src/dynarec/**`).
* O que sobreviveu e foi reutilizado: `ios/box64-registers/` com o censo x18 do binário empacotado, os *scripts* de censo, a sonda ISA (`pair_test.S`/`pair_main.c`) e o rascunho do relatório nas seções §0–§18.
* Consequência registrada: **a lista literal das 34 seções não sobreviveu** ao truncamento; ela foi re-fornecida pelo requisitante e este documento agora segue exatamente a lista prescrita, com o conteúdo técnico do rascunho preservado e remapeado (§§5–12 herdam integralmente o texto técnico das antigas §§4–11).

### 1.2 Estado do ambiente e restrição de recursos

```
$ nproc ; free -m | awk 'NR==2{print $2}'   → 2 ; 1982
$ getconf PAGE_SIZE                          → 4096      (host qemu-user; NÃO é o do iPhone)
$ gcc --version | head -1                    → gcc (Debian 14.2.0) 14.2.0
$ clang --version | head -1                  → Debian clang version 19.1.7
$ aarch64-linux-gnu-gcc --version | head -1  → 14.2.0 (cross)
$ qemu-aarch64 --version | head -1           → qemu-aarch64 version 10.0.13 (Debian 1:10.0.13+ds-0+deb13u1)
```

O build completo do box64 (**~350 TUs**, 2 vCPU, ~1,98 GB) **não cabe nesta máquina** e não foi tentado (a fase proíbe OOM-repeat). Toda a verificação foi feita por TU → objeto → harness, como prescrito (item (m)).

### 1.3 Ordem de prioridade e regras aplicadas

`CORREÇÃO > MANUTENIBILIDADE > PERFORMANCE`. A estratégia de menor diff foi explicitamente descartada (§10). Regras ativas: nenhuma substituição textual (`sed s/x18/x22/g` ou equivalente); nenhuma presunção de que um registrador é livre sem evidência; nenhum hardcode Darwin espalhado; nenhum `#ifdef __APPLE__`; nada de fork; nenhuma afirmação de que "o Box64 funciona no iOS"; nada de Fase 05.

---

## 2. Branch/commit base

| Item | Valor |
|---|---|
| Repositório do entregável | `brunodev85/winlator.git`, HEAD `b6b2259158cf38d06067c34430d840d56b46d220` |
| Submódulo | `app` @ `a030f552f452158a2db64fdb32b490fa19c0b48d` (sparse: `app/src/main/assets/box64/*`) |
| Branch de trabalho | **`ios-phase-04-box64-registers`** (criado a partir de `main`, sem force, sem rebase) |
| Commit | **um único commit**, mensagem exata `Adapt Box64 ARM64 register model for Darwin iOS`, conteúdo `ios/` |
| Base do código analisado | box64 **v0.4.4** @ `2f130fab1d6e1a4ee8a71dc60cfdfcc839ad192a` (clone em `/tmp/box64-ref`) |

**Desvio registrado (§2 do rascunho, mantido):** o branch da Fase 04 é criado a partir de `b6b2259` e **não** de um ancestral da Fase 03 — esse ancestral não existe mais localmente. Nenhum `git reset`, `checkout` destrutivo ou `rebase` foi ou será usado.

**Estado do `git status --short` no início do trabalho:**

```
$ cd /home/user/winlator && git status --short
 D gladio
 D vortek
?? ios/
```

As duas entradas ` D` (`gladio`, `vortek`) são **pré-existentes ao trabalho desta fase**: são submódulos cujas árvores de trabalho não sobreviveram à reconstrução do workspace descrita na §1.1 (os diretórios `.git` não entram no snapshot). Elas **não** foram tocadas, **não** integram o commit, e o procedimento obrigatório "se a árvore tem mudanças inesperadas, **não** destrua trabalho" foi respeitado (`git reset --hard` / `git clean -fd` não foram usados). O commit usa caminho explícito (`git add ios`), o que por construção não inclui essas deleções — verificado: `git add -n ios | grep -c 'gladio\|vortek'` → `0`.

---

## 3. Metodologia de contagem x18

**Universo:** desmontagem completa do binário aarch64 empacotado pelo Winlator, com o binário de ferramentas aarch64:

```
$ aarch64-linux-gnu-objdump -d /tmp/p04/usr/local/bin/box64 > /tmp/p04/disasm_all.txt
```

**Definição de "linha com x18":** linha de instrução cujo **texto de operandos** contém o token `x18` ou `w18` com fronteira de palavra (`\bx18\b`, `\bw18\b`). Isso exclui `x18` dentro de literais hexadecimais (`…x18…`) e dentro de nomes de símbolo maiores.

```
total de linhas de instrução : 3 534 632
linhas com \bx18\b           :     9 609
linhas com \bw18\b           :       157
linhas com ambos (overlap)   :         0
UNIÃO (x18 ∪ w18)            :     9 766   ← idêntico ao total reportado na Fase 02
grep ingênuo por substring   :    19 309   (9 700 linhas contaminadas por hex)
```

**Funções e atribuição por unidade de tradução:**

```
funções com \bx18\b   :   606
funções com \bw18\b   :    12
funções (união)       :   615
```

| TU (`readelf` `STT_FILE`) | linhas x18 |
|---|---|
| `gtkclass.c` | 8 568 |
| `crtstuff.c` | 630 |
| `wrappedlibx11.c` | 136 |
| `wrappedgobject2.c` | 133 |
| `wrappedgtkx112.c` | 104 |
| `threads.c` | 13 |
| `<unknown>` | 6 |
| `dynarec_arm64_helper.c` | 6 |
| 5 outras TUs | ≤ 3 cada |

**Leitura:** 99,6 % das ocorrências de x18 no binário empacotado estão em **código de host** (`gtkclass.c`, `crtstuff.c` — wrappers GTK e runtime C), **não** no núcleo do dynarec. Apenas ~38 referências estão em TUs do núcleo. É isso que torna o problema tratável: o conflito é de **modelo de registradores do JIT**, não de milhares de instruções.

**Declaração de método (filtros, ferramenta, otimização):**

| Item | Valor |
|---|---|
| Arquitetura | AArch64 (ELF do binário empacotado, desmontado por toolchain aarch64) |
| Ferramenta | `aarch64-linux-gnu-objdump -d` (o `objdump` do host recusa o ELF) |
| Filtro | `\bx18\b` / `\bw18\b` (fronteira de palavra), sobre linhas de instrução |
| Duplicatas | nenhuma; `x18 ∩ w18 = 0` verificado |
| Otimização | **a do binário como publicado** (não recompilado) |
| Funções | intervalos de símbolo do cabeçalho do `objdump` |
| TU | `STT_FILE` do `readelf`, somando exatamente as linhas contadas |
| Reprodução | `python3 ios/box64-registers/scripts/x18_census.py <binário>` |

**Armadilhas metodológicas identificadas (não repetir):**

1. `grep x18` por substring **infla ~2×** (literais hexadecimais contêm `x18`): usar `\bx18\b`/`w18`.
2. A receita `awk` do tipo `fn=$0 … /x18/{print fn}` **não é censo de registradores** — originou os 2 361 "funções" da Fase 03 (§4).
3. `objdump -D` em vez de `-d` infla o universo (25 267 linhas / 2 830 funções contra 9 609 / 606 corretos).
4. Linhas de continuação do `objdump` quebram regex ancorada em `\t` (4 716 falsos positivos) — ancorar no endereço.
5. `grep -c` com resultado 0 sai com status 1 e mata cadeias `&&` — usar `|| true`.
6. Cross-compilar TU de `DYNAREC_PASS` sem `-DSTEP=<n>` falha com `'STEP' undeclared`.

---

## 4. Explicação Fase 02 × Fase 03

* **9 766 = 9 609 + 157 com overlap 0.** A Fase 02 contou a **união** (`x18 ∪ w18`); a Fase 03 contou **apenas `x18`**. Não há contradição entre as fases: são duas métricas diferentes, reproduzidas ambas exatamente.
* **2 361 é artefato de método**, não um segundo censo. Reproduzido com a receita `awk` que propaga o último cabeçalho de função e casa substring **sem** fronteira de palavra: o resultado é uma contagem de *linhas sob a última função vista*, incluindo literais hexadecimais — não de funções que usam x18. A contagem correta de **funções** é 606 (`x18`), 12 (`w18`), **615 (união)**.
* **Δ = 3 (615 vs 612 da Fase 02) é resíduo limitado.** Hipótese registrada (não conclusão): diferença de janela/atribuição de símbolos entre as duas medições. Não afeta nenhuma decisão de arquitetura.
* Nota correlata: as 3 533 105 instruções reportadas pela Fase 03 contra 3 534 632 medidas aqui diferem em 1 527 linhas (0,04 %) — compatível com seleção/versão de `objdump`; também não afeta decisões.

**Conclusão:** os três números (9 766, 9 609, 2 361) são todos reprodutíveis e nenhum deles indica um problema de arquitetura. O que importa para esta fase é a **distribuição**: 99,6 % das ocorrências estão fora do dynarec (§3).

---

## 5. Mapa guest→host

Fonte autoritativa: `src/dynarec/arm64/arm64_mapping.h`. **Estado depois da Estratégia A** (a coluna "host" é o resultado desta fase; a coluna "antes" documenta o upstream):

| Slot `regs[]` | Guest | Host **depois** | Host antes | Classe AAPCS64 do host | Observação |
|---|---|---|---|---|---|
| `regs[0]` | RAX | x10 | x10 | temporário (caller-saved) | 1º retorno em `call_n` |
| `regs[1]` | RCX | x11 | x11 | temporário | |
| `regs[2]` | RDX | x12 | x12 | temporário | |
| `regs[3]` | RBX | x13 | x13 | temporário | guest callee-saved em host caller-saved → salvo por `call_n` |
| `regs[4]` | RSP | x14 | x14 | temporário | idem |
| `regs[5]` | RBP | x15 | x15 | temporário | idem |
| `regs[6]` | RSI | x16 | x16 | IP0 (caller-saved) | |
| `regs[7]` | RDI | x17 | x17 | IP1 (caller-saved) | |
| `regs[8]` | **R8** | **x9** | **x18** | temporário puro (caller-saved) | **mudança desta fase** |
| `regs[9]` | R9 | x19 | x19 | callee-saved | |
| `regs[10]` | R10 | x20 | x20 | callee-saved | |
| `regs[11]` | R11 | x21 | x21 | callee-saved | |
| `regs[12]` | R12 | x22 | x22 | callee-saved | |
| `regs[13]` | R13 | x23 | x23 | callee-saved | |
| `regs[14]` | R14 | x24 | x24 | callee-saved | |
| `regs[15]` | R15 | x25 | x25 | callee-saved | |

(`wR8…wR15` são os mesmos números: `#define wR8 xR8`.)

**Registradores não-guest do modelo:**

| Papel | Registrador | Evidência |
|---|---|---|
| Ponteiro `emu` | x0 (`xEmu`) | `arm64_mapping.h` |
| Temporários do emissor | x1–x6 | idem |
| PC do x87 | x7 (`x87pc`) | idem; restaurado por `NATIVE_RESTORE_X87PC()` |
| **Registrador de plataforma** | **x18 (`xPLATFORM`)** | **novo**: `#define xPLATFORM 18`; no `_WIN32` carrega o TEB, no Linux/Android não é usado, no Darwin é reservado |
| Reserva de emergência | x8 | não atribuído (papel ABI de *indirect result* — evitado de propósito) |
| Flags do guest | x26 (`xFlags`) | prolog `ldp x26,x27,[x0,#128]` |
| RIP do guest | x27 (`xRIP`) | também argumento de `LinkNext` (`arm64_next.S`) |
| Base de frame | x28 (`xSavedSP`) | prolog `add x28,sp,16`; único uso em código emitido: `SUBx_U12(xSP, xSavedSP, 16)` em `ret_to_next` |
| LR / SP / ZR | x30 / x31 / x31 | |
| x29 (FP) | apenas salvo/restaurado | nunca usado como frame pointer |

### 5.1 Evidência de ociosidade de x8/x9 (a afirmação "x9 é livre")

Quatro verificações independentes, todas com saída vazia ou apenas comentário/string:

1. **Não há `#define x8`/`x9`** em `arm64_mapping.h`.
2. **Nenhum uso de identificador**: `grep -rn '\bx8\b\|\bx9\b' src/dynarec/arm64/` → só comentários e strings de impressão.
3. **Nenhum argumento numérico 8/9** em posição de registrador nos emissores:
   `grep -rnE '\b(MOVx|MOVw|MOVz|MOVx_REG|MOVw_REG|MOVz_REG|STRx_U12|STRw_U12|LDRx_U12|LDRw_U12|STPx_S7_offset|STPx_S7_preindex|LDPx_S7_offset|LDPx_S7_postindex|BLR|BR|CMPx|CMPw|ADDx_REG|SUBx_REG|ADDx_U12|SUBx_U12|FMOVD|VMOV)\s*\(\s*(8|9)\s*[,)]' src/dynarec/arm64/` → **vazio**.
4. **Nenhuma estrutura indexada por número de GPR** no backend (as únicas indexações por registrador são do cache SSE/x87).

*Ressalva honesta:* "livre" significa "não usado pelo mapeamento nem pelos emissores"; **não** significa "preservado através de chamadas de host" — x9 é *caller-saved* em ambos os ABIs (§§6–7). É exatamente essa diferença que a §11 quantifica e a §12 resolve.

### 5.2 Restrição de codificação do par (R8,R9) — medida, não presumida

`STPx_S7_offset(xR8, xR9, …)` grava dois slots de memória (64 e 72) com **um** `STP`. Sob o remap, a adjacência *de registrador* deixa de valer. Sonda medida neste workspace:

```
$ aarch64-linux-gnu-gcc -static -O0 -o out/pair_test pair_test.S pair_main.c && qemu-aarch64 ./out/pair_test
stp x9,x19,[x0,#32]  -> [+32]=0x1111  [+40]=0x2222   (expect 0x1111 / 0x2222)
ldp x9,x19,[x0,#32]  -> stored back [+48]=0x1111 [+56]=0x2222
stp x9,x27,[x0,#64]  -> [+64]=0x1111  [+72]=0x3333
VERDICT: non-consecutive STP/LDP ACCEPTED
```

**Resultado:** `STP`/`LDP` aceitam pares **não consecutivos** (e de registradores altos), com ordem **primeiro registrador → endereço menor**. O par (R8,R9) permanece uma única instrução sob remap — não é preciso dividir em dois `STR`. *Ressalva de método: medido em `qemu-aarch64`, não em silício Apple; a instrução é ISA-genérica, mas a validação física permanece pendente (§31).*

### 5.3 Reconciliação das tabelas de texto (divergência pré-existente registrada)

`dynarec_arm64_functions.c:702-757` (`register_mappings[]`) lista, para os nomes do guest, os valores host. As quatro linhas `rsi/rdi/rsp/rbp` estão **trocadas** em relação ao mapa autoritativo (`xRSP=14, xRBP=15, xRSI=16, xRDI=17`, comprovado por `STPx_S7_offset(xRSP, xRBP, xEmu, offsetof(x64emu_t, regs[_SP]))`). Uso real: apenas `x64disas_add_register_mapping_annotations()` (`:906`) — texto de anotação de disassembly, nunca codegen.

**Decisão desta fase: as 4 linhas trocadas NÃO foram corrigidas.** Motivo: é defeito cosmético **pré-existente e não relacionado** ao x18; corrigi-lo misturaria dois assuntos no commit cujo escopo é a adaptação do modelo de registradores. Fica registrado como achado para fase futura (§32). As linhas do **R8**, essas sim, foram atualizadas (`{"r8","x9"}, {"r8d","w9"}, {"r8w","x9"}, {"r8b","x9"}`), assim como `arm64_printer.c` (índice 9 → `"xR8"`, índice 18 → `"xPLATFORM"`).

---

## 6. ABI Linux ARM64

**Escopo:** AAPCS64 (AArch64 Linux) — a ABI do alvo atual do backend.

| Reg | AAPCS64 genérico / Linux | Uso no box64 ARM64 |
|---|---|---|
| x0–x7 | argumentos/retorno, caller-saved | x0 = `xEmu` (fixo); x1–x6 = temporários do emissor; x7 = `x87pc` |
| x8 | *Indirect result location* (caller-saved) | **não atribuído** (reserva de emergência) |
| x9–x15 | temporários, caller-saved | x10–x15 = RAX,RBX(13),RSP(14),RBP(15)… com x9 **agora** = guest R8 |
| x16/x17 | IP0/IP1, caller-saved | RSI/RDI |
| **x18** | **"The Platform Register, if needed; otherwise a Caller-saved register"**; o próprio AAPCS64 recomenda: *"Software developers creating platform-independent code are advised to avoid using r18 if at all possible"* | **deixa de ser usado** (era guest R8); só aparece sob `_WIN32` (TEB) via `xPLATFORM` |
| x19–x28 | callee-saved | **integralmente ocupados**: R9–R15 (7) + xFlags + xRIP + xSavedSP (3) → **nenhum callee-saved livre** |
| x29 | FP | salvo/restaurado, nunca como frame pointer |
| x30 / x31 | LR / SP, 16-byte alignment | idem |

**Dois fatos que sustentam a decisão:**

1. **x18 é caller-saved no Linux.** A premissa implícita do box64 ("x18 sobrevive a chamada de host") **não é garantida por ABI**: sobrevive porque o compiler/glibc não o usam. Verificado que **não existe `-ffixed-x18` em lugar nenhum do build** (`grep -rn "ffixed\|fixed-x18" CMakeLists.txt cmake/` → vazio). Ou seja: a fragilidade é **latente no Linux também**, e a Estratégia A a remove.
2. **Não existe registrador callee-saved livre**, logo **qualquer** destino para o guest R8 é caller-saved — a preservação explícita na fronteira C é inevitável, não uma escolha estética (é isso que a §11 formaliza).

**Modelo SIMD/FPU (não afetado):** box64 **não usa** mapeamento fixo para SSE/x87; há cache dinâmico (`neoncache_t`) em `dynarec_arm64_private.h`, com `fpu_pushcache`/`fpu_popcache` emitindo apenas `VSTR`/`VLDR` — **sem chamadas de host e sem GPRs**. O remap de um GPR do guest **não toca** o modelo SIMD.

---

## 7. ABI Apple ARM64

**Escopo:** arm64-apple-ios (ABI Apple, base iOS 17).

| Reg | AAPCS64 genérico / Linux | arm64-apple-ios | Conflito? |
|---|---|---|---|
| x0–x7 | argumentos/retorno, caller-saved | idem | não |
| x8 | indirect result (caller-saved) | idem (com divergências listadas pela Apple) | não |
| x9–x15 | temporários, caller-saved | idem | não |
| x16/x17 | IP0/IP1 | idem | não |
| **x18** | plataforma "se necessário", senão caller-saved; evitar em código portável | **"The platforms reserve register x18. Don't use this register."** | **SIM** |
| x19–x28 | callee-saved | idem | não |
| x29 | FP | **"must always address a valid frame record"** | não para o mapa; sim para disciplina de frame |
| x30 / SP | LR / 16-byte | idem; **red zone de 128 bytes** explicitada | não |
| FP/SIMD | v0–v7 args/scratch; v8–v15 callee-saved (64 bits baixos); v16–v31 caller-saved | idem | não |
| varargs | regra AAPCS64 | argumentos variádicos vão para a pilha; `va_list` = `char*` | fora do escopo — risco documentado |
| `long double` | 128-bit (quad) | **64-bit (igual a `double`)** | fora do escopo — risco documentado |

**Conclusões:**

1. **x18 é o único registrador do mapa cuja classificação difere entre os dois ABIs.** Todo o resto (x10–x17, x19–x25, x26–x28, x0, x1–x7) tem a mesma semântica.
2. Tirar o guest R8 de x18 **não é concessão ao iOS**: é conformidade com o próprio AAPCS64 (que recomenda evitar r18). Isso é o que permite **uma única solução para Linux, Android e Darwin**, sem `#ifdef` de mapeamento — condição do requisito (g).
3. Disciplinas Apple **preservadas** pela estratégia: x18 nunca é escrito (não há `__APPLE__` no patch: `git diff | grep -c '__APPLE__'` → `0`); x29 continua intocado durante o bloco (`grep -rn '\bx29\b' src/dynarec/arm64/` → apenas `arm64_prolog.S:12` e `arm64_epilog.S:37`), satisfazendo "x29 deve apontar para um frame record válido".

**Riscos ABI identificados e fora do escopo desta fase (registrados, não resolvidos):** `long double` de 64 bits (verificado que o backend ARM64 **não** usa `long double`/`__float128`: `grep -rn "long double\|__float128" src/dynarec/arm64/` → vazio; a emulação x87 de 80 bits do emu precisa de auditoria própria em fase futura); varargs (afeta wrappers nativos, não o mapeamento — `call_n` só é usado para *simple wrappers* de aridade fixa).

---

## 8. x18

### 8.1 Inventário no código-fonte

```
$ cd /tmp/box64-ref && grep -rn '\bx18\b' src/ --include=*.c --include=*.h --include=*.S | grep -v '/wrapped'
```

Resultado: **18 linhas no repositório, das quais 16 em `src/dynarec/arm64/`** (as outras 2 pertencem ao backend RV64 — `rv64_mapping.h:26,113` — onde `x18` nomeia um registrador callee-saved do RV64, sem relação com ARM64).

| Papel | Sites | Localização (upstream) |
|---|---|---|
| **(A) guest R8 em trânsito de frame** | 6 | `arm64_prolog.S:29`, `arm64_epilog.S:16`, `arm64_next.S:23,41,56,74` |
| **(B) TEB do Windows (`_WIN32`)** | 3 literais + 2 simbólicos | `arm64_next.S:26,59`, `arm64_epilog.S:23`; simbólicos em `dynarec_arm64_helper.c:563,616` |
| **(C) texto de impressão (cosmético)** | 3 | `dynarec_arm64_functions.c:739,741,742` |
| **(D) comentários** | 4 | `dynarec_arm64_helper.c:555,556,607,651` |

### 8.2 O lado simbólico (o que realmente importa)

```
$ grep -rn '\bxR8\b' src/dynarec/arm64/
  dynarec_arm64_helper.c:559  STPx_S7_offset(xR8,  xR9,  xEmu, offsetof(x64emu_t, regs[_R8]));
  dynarec_arm64_helper.c:563  LDRx_U12(xR8, xEmu, offsetof(x64emu_t, win64_teb));      // _WIN32
  dynarec_arm64_helper.c:611  STPx_S7_offset(xR8,  xR9,  xEmu, offsetof(x64emu_t, regs[_R8]));
  dynarec_arm64_helper.c:616  LDRx_U12(xR8, xEmu, offsetof(x64emu_t, win64_teb));      // _WIN32
  dynarec_arm64_helper.c:671  MOVx_REG(x4, xR8);                                        // call_n
```

**Conclusão:** todo o uso de x18 no dynarec ARM64 é **simbólico** (`xR8`), exceto 13 sítios físicos em `.S`/texto, e **nenhum emissor de opcode escreve x18 diretamente**. Um remap correto é mudança de *tabela*, não de milhares de instruções — o que a §12 executa e a §27 comprova por disassembly.

### 8.3 Censo do binário empacotado (resumo executivo)

`9 766 = 9 609 (x18) + 157 (w18)`, em `3 534 632` instruções, com **99,6 % fora do dynarec** (§3). Nenhuma das ocorrências fora do dynarec é acionável por esta fase: são código de host compilado (GTK/wrappers/runtime C), não código emitido pelo JIT.

---

## 9. Implementação _WIN32 existente

**Precedente documentado, não copiado.** O mecanismo `_WIN32` resolve um problema *diferente* e não é replicável no Darwin; esta seção existe para (a) documentar o precedente e (b) garantir que ele continue funcionando depois do remap.

### 9.1 Estrutura e campo

```
emu/x64emu_private.h:129-131
    #ifdef _WIN32
    uint64_t    win64_teb;
    #endif
```

Offset efetivo **3104** — usado como literal nos arquivos `.S` (`ldr x18,[x0,3104]`), nunca por `offsetof`.

### 9.2 Os 5 sítios onde o TEB entra em x18 (estado atual, pós-fase)

| # | Local | Código | Contexto |
|---|---|---|---|
| 1 | `dynarec_arm64_helper.c:564` | `LDRx_U12(xPLATFORM, xEmu, offsetof(x64emu_t, win64_teb));` | `call_c`, **depois** de gravar R8/R9 em `emu->regs[]` e **antes** do `BLR` |
| 2 | `dynarec_arm64_helper.c:618` | idem | `call_d`, mesma posição relativa |
| 3 | `arm64_epilog.S:23` | `ldr x18, [x0, 3104]` | depois de gravar R8/R9, antes de restaurar a pilha e `ret` |
| 4 | `arm64_next.S:26` | `ldr x18, [x0, 3104]` | depois de salvar o registrador do guest R8 na pilha, antes de `bl LinkNext` |
| 5 | `arm64_next.S:59` | `ldr x18, [x0, 3104]` | idem em `arm64_next_invalid` |

**Mudança desta fase nesses sítios:** os **dois sítios simbólicos** (1 e 2) deixaram de escrever `xR8` e passaram a escrever **`xPLATFORM`**, que é `#define xPLATFORM 18`. Sem isso, o remap teria produzido o pior bug possível: o TEB passaria a ser carregado **em x9** (o novo lar do guest R8) no Windows, silenciosamente. Nos `.S` o literal `x18` permanece — e **deve** permanecer, porque ali o TEB é o próprio registrador de plataforma.

### 9.3 Razão original

No Windows ARM64, **x18 é o TEB**. Sempre que código C de host roda — runtime, CRT, APIs — x18 precisa conter o TEB válido. O box64 então **troca o conteúdo de x18 no limite com o código C**: dentro do bloco (e nas transições bloco→bloco, que não passam por C) x18 = R8 do guest; em toda entrada em função C x18 = TEB.

### 9.4 Efeito sobre a preservação do guest R8

Não há perda, porque a troca vem sempre acompanhada de salvamento explícito: `call_c`/`call_d` fazem `STP xR8,xR9 → emu->regs[8..9]` **antes** do `LDR` do TEB e recarregam depois do `BLR`; `arm64_next` faz `stp/ldp` do registrador do guest R8 em volta de `LinkNext`; o epilog grava o R8 em `emu->regs[8]` antes de x18 receber o TEB. **O precedente demonstra a disciplina "registrador guest ↔ memória na fronteira C"**, e a aplica em 5 pontos. O que ele *não* faz é valer para **todas** as fronteiras (`READFLAGS`/`GRABFLAGS`/`flagsCacheTransform`/`checkCRC`) — e é exatamente aí que a Estratégia A precisou adicionar spill (§17).

### 9.5 Custo medido do precedente

+1 `LDR` por chamada de helper (`call_c`/`call_d`) e por transição de bloco (`arm64_next`), **no Windows**; nenhuma instrução adicional no caminho normal do guest; 5 sítios a manter em sincronia.

### 9.6 Por que **não** pode ser copiado para Darwin

1. O mecanismo **exige escrever x18** — proibido pela Apple ("Don't use this register"). Não há valor de plataforma que o box64 possa legitimamente manter ali.
2. No Windows o swap é legítimo porque o **sistema atribui** x18 ao processo (o TEB é do app). No Darwin não existe equivalente atribuído.
3. O mecanismo depende de conhecer **todas** as fronteiras C para trocar de volta — e, como a §9.4 mostra, nem todas são cobertas. No Windows isso é tolerado porque o valor ausente é justamente o R8 (guest caller-saved logo após chamada nativa); esse "tolerado" **não** se transfere: na Apple qualquer janela com x18 ≠ valor de plataforma é violação.

**Registro honesto:** o precedente mostra *que a fronteira C é o lugar certo para descarregar o guest R8*; **não** fornece destino válido para x18 no Darwin. Por isso a estratégia C foi rejeitada (§10) e o aprendizado dela (spill explícito na fronteira) foi incorporado à estratégia escolhida.

---

## 10. Alternativas avaliadas

Critérios exigidos: correção, complexidade, impacto no codegen, pressão de registradores, performance, número de mudanças, risco de regressão, manutenção upstream. Os números de "mudanças" derivam das listas de sítios medidas (§§5–8), não de estimativas.

**Descrição das estratégias:**

* **A — Remap permanente, plataforma-independente.** Guest R8 passa de x18 para **x9**; x18 deixa de ser usado por qualquer plataforma; preservação do R8 em fronteiras C passa a ser **explícita** nos 4 sítios de helper que hoje não a fazem.
* **B — Spill/reload puro (sem registrador).** Guest R8 residente em memória (`emu->regs[8]`), carregado por uso.
* **C — Padrão `_WIN32` (x18 de duplo uso).** x18 = R8 dentro do bloco, valor de plataforma nas fronteiras.
* **D — Dois mapeamentos sob `#ifdef`.** x18 no Linux/Android, x9 no Darwin.
* **E — Libertar um registrador callee-saved.** Demover `xSavedSP` (x28) para `emu->xSPSave` (padrão RV64/PPC64LE) e dar **x28** ao guest R8 — eliminando qualquer spill/reload novo.

### 10.1 Comparação

| Critério | **A (x18→x9)** | B (memória) | C (`_WIN32`-like) | D (`#ifdef`) | **E (x28)** |
|---|---|---|---|---|---|
| Correção (R8 preservado em toda fronteira) | Sim, **explícita** nos 4 sítios + `call_c/d` já existentes; verificável por disassembly (§27) | Sim, por construção | Não no Darwin (x18 proibido) | Sim, mas duas provas distintas | Sim, **por ABI** (callee-saved) |
| Complexidade do modelo | Baixa: R8 fica **igual** aos demais caller-saved do guest (RAX…RDI) | Alta: toda leitura/escrita de R8 vira load/store | Média-alta: disciplina de 5+ sítios, inválida no destino | Média-alta: duas verdades | Média-alta: mexe no frame e na reconstrução de SP |
| Impacto no codegen | **Nenhuma estrutura nova**; muda o operando só nos acessos a R8 | Reescrita de emissores que usam R8 como operando direto | Nenhum em Linux; ilegal em Darwin | Depende do alvo | Nenhum |
| Pressão de registradores | x8/x9 = 1 realmente livre (fica 1: x8) | libera registrador | 2 papéis no mesmo registrador | = A no Darwin | **zera** o livre (x8 e x9 ambos livres — melhor) |
| Performance | +1 instr. no prolog e +1 no epilog; +2 instr. em 4 sítios de helper (um raro: `checkCRC`) | Piora caminhos quentes | Custo já existente no Windows; ilegal no Darwin | = A | **Zero** custo de fronteira (vantagem real) |
| Nº de mudanças (sítios medidos) | 1 definição de mapa + `TO_NAT`/`IS_GPR` + 3 `.S` + **4** sítios de helper + 2 sítios TEB simbólicos + 2 tabelas de texto = **8 arquivos, +73/−20 linhas** | Centenas | 5+ sítios (e proibida) | 2 mapas + todos os `.S` condicionais | 1 mapa + prolog/epilog + `ret_to_next` + campo no emu + **todos** os `.S`/printer que citam x28 |
| Risco de regressão | **Baixo**: falha aparece em teste de preservação/diferencial; não toca a pilha | Alto (superfície enorme, performance) | Alto (viola ABI Apple) | Alto (código não testável em paridade) | **Médio-alto**: altera a máquina de call/ret e o reset de SP (`ret_to_next`), cuja falha é corrupção silenciosa de pilha |
| Manutenção upstream | **Alta**: AAPCS64 recomenda evitar r18; "reservado → não mapeado" já existe (PPC64LE deixa r2/TOC e r13/TLS fora do mapa); idioma de tabela já usado por RV64/LA64 | Baixa | Baixa (só interessa ao Windows) | Baixa: upstream rejeita mapas condicionais | Média: precedente explícito em RV64/PPC64LE |

### 10.2 Evidências que diferenciam

* **Precedente interno de "registrador reservado não é mapeado"** — `ppc64le_mapping.h`: `r2 toc native toc TOC pointer (reserved) N/A` e `r13 - - TLS pointer (reserved) N/A`. Mesmo raciocínio que a Apple impõe para x18; logo A/E são idiomáticas, C/D não.
* **Precedente interno de "sem callee-saved livre → evacuar para o emu struct"** (base de E) — `emu/x64emu_private.h:78`: `#if defined(RV64) || defined(PPC64LE) // no spare callee-saved register for xSavedSP, store in emu struct instead`, com salvamento/restauração explícitos (inclusive aninhamento) em `rv64_prolog.S:73-74` / `rv64_epilog.S:46-50` e `ppc64le_prolog.S:96-99` / `ppc64le_epilog.S:43-49`.
* **Precedente interno de tabela de mapa** — `rv64_mapping.h:70`, `la64_mapping.h:66`.
* **Ausência de inversa no ARM64** — não existe `TO_X64`, ao contrário de LA64; adotar tabela não cria obrigação de manter mapa reverso.
* **Sem `SVC` emitido** — não existe fronteira de syscall a cobrir (`grep -rn '\bSVC\b' src/dynarec/arm64/` → vazio).

### 10.3 Ranking explícito e por que E não foi escolhida

`CORREÇÃO`: A e E **empatam** — em A a preservação é **explícita e verificável** (4 sítios novos + os já existentes); em E é **implícita por ABI** (x28 callee-saved). Não há déficit de correção em A; o que E oferece é garantia mais barata de manter.

`MANUTENIBILIDADE`: A é superior porque (i) o frame (`arm64_prolog`/`epilog`) permanece intacto — e o frame é justamente onde a auditoria exige prova; (ii) E introduz um invariante **novo** (`emu->xSPSave` em sincronia, com aninhamento) que hoje não existe no ARM64, cujo modo de falha (SP restaurado errado em `ret_to_next`) é corrupção silenciosa; (iii) em A o guest R8 passa a ser tratado **como todos os outros** caller-saved do guest, em vez de caso especial.

`PERFORMANCE`: E é superior (zero spill novo). A custa +2 instruções por bloco e +2 em 4 sítios de helper. A ordem imposta é `CORREÇÃO > MANUTENIBILIDADE > PERFORMANCE`.

---

## 11. Análise de liveness

### 11.1 Fato estrutural: não existe callee-saved livre

x19–x28 estão **integralmente ocupados** (R9–R15 = 7, xFlags, xRIP, xSavedSP = 3). Logo **todo** destino possível para o guest R8 é **caller-saved**, e a pergunta de liveness não é "quem preserva x9?" — é **"em quais pontos do código emitido o guest R8 está vivo e uma chamada C pode destruir o lar dele?"**. Essa lista foi **enumerada e medida**, não presumida:

```
$ grep -n 'BLR(' src/dynarec/arm64/dynarec_arm64_helper.c src/dynarec/arm64/dynarec_arm64_helper.h
  helper.c:425,428     jump_to_next()   BLR(dest)   ─┐
  helper.c:457         ret_to_next()    BLR(dest)   ─┴─ chamadas a dynablock / dispatcher
  helper.c:567         call_c()         BLR(reg)    ─── alvo em registrador temporário
  helper.c:625         call_d()         BLR(x87pc)  ─── idem (x87pc é usado como temporário do endereço)
  helper.c:680         call_n()         BLR(16)     ─── chamada nativa (wrapper simples)
  helper.c:2365        flagsCacheTransform() BLR(x1) ──┐
  helper.c:3008,3018   checkCRC()       BLR(x3) ×2  ─┴─ helpers C de suporte ao bloco
  helper.h:1008,1020   READFLAGS/GRABFLAGS BLR(x6)  ─── updateflags_arm64
```

(mais os dois `bl` de `arm64_next.S` para `LinkNext`/`LinkNextInvalid`, que não são emissores).

### 11.2 Classificação de liveness por sítio (a decisão central da fase)

| Sítio | Naturaleza | Guest R8 vivo depois? | Ação sob a Estratégia A |
|---|---|---|---|
| `jump_to_next`/`ret_to_next` (425,428,457) | salto para dynablock/dispatcher | sim (o bloco destino espera o R8 correto) | **coberto**: `arm64_prolog`/`epilog` e o `arm64_next.S` remapeado salvam o lar do R8 (§§14–16) |
| `call_c` (567), `call_d` (625) | helper C do próprio box64 | sim | **já coberto pelo código upstream**: `STPx_S7_offset(xR8,xR9,…)` antes e `GO(R8,R9)` depois (§17.1) — nenhuma mudança necessária |
| `READFLAGS`/`GRABFLAGS` (helper.h:1008,1020) | helper C em ponto arbitrário do bloco | **sim** | **spill/reload adicionados** (§17.2) |
| `flagsCacheTransform` (2365) | idem | **sim** | **spill/reload adicionados** (§17.3) |
| `checkCRC` (3008,3018) | idem, emitido raramente (`PREFLAGSNEEDED`) | **sim** | **spill/reload adicionados** (§17.4) |
| `call_n` (680) | chamada nativa, semântica de **chamada do guest** | o ABI do **guest** permite perder R8 (R8 é caller-saved na ABI x86-64 SysV) | **nenhuma mudança**: R8 é movido para registrador de argumento antes da chamada (671) e a perda é legítima — idêntico ao que já ocorre com RAX/RCX/RDX/RSI/RDI |

### 11.3 A premissa que a fase derruba (e por que ela era invisível)

"x18 sobrevive a chamadas de host" **não é garantido por ABI** — nem no Linux (x18 é caller-saved; sobrevive porque compiladores/glibc não o usam, e **não há `-ffixed-x18` no build**: `grep -rn "ffixed\|fixed-x18" CMakeLists.txt cmake/` → vazio). Com o R8 em **x9**, a premissa fica **falsa por construção**: x9 é temporário puro e **qualquer** callee C pode usá-lo.

**Demonstração medida (não argumento):** o helper C compilado do harness usa apenas `x0–x8` neste caso (`x9` ausente: `grep -c '\bx9\b' artifacts/frame/new/hf_c_poison.dis` → `0`) — ou seja, **nem o host demonstra o problema espontaneamente**. Por isso o controle negativo do harness **força** o clobber de x9/x18 (modelando a liberdade que o ABI dá a qualquer callee), e por isso a decisão precisa ser tomada por **ABI** (§§6–7) e verificada por **teste explícito** (§§23, 25).

### 11.4 O que a liveness **não** exige

* **Sem fronteira de syscall**: nenhum `SVC` é emitido pelo backend → nenhuma perda de caller-saved por syscall dentro de um bloco.
* **Sem `longjmp` no caminho do dynarec**: auditoria em §18.2 → os arquivos que contêm `longjmp` não tocam `xR8`/`regs[_R8]`.
* **Sem chamadas de host no modelo SIMD**: `fpu_pushcache`/`fpu_popcache` emitem apenas `VSTR`/`VLDR` (§6).

---

## 12. Estratégia selecionada

### 12.1 Decisão

**`SELECTED_STRATEGY=A`** — remap permanente e plataforma-independente do guest R8 para **x9**, com preservação explícita nas 4 fronteiras de helper que não a faziam, `xPLATFORM` nomeado para o TEB do Windows e `TO_NAT`/`IS_GPR` regenerados como tabela. E registrada como alternativa defensável; B e C rejeitadas por correção/risco; D rejeitada por criar duas verdades de codegen.

### 12.2 Entrada de banco de dados (requisito (f))

```
SELECTED_STRATEGY=A
REASON=Remap permanente e plataforma-independente do R8 guest para x9 (x18 deixa de ser usado em
  qualquer plataforma), com preservacao explicita do R8 nas 4 fronteiras de helper C que hoje nao a
  fazem. Sustentado por: (1) AAPCS64 recomenda evitar r18 em codigo portavel e Apple proibe usar x18,
  portanto remover x18 serve Linux, Android e Darwin com UMA verdade de codegen; (2) nao existe
  registrador callee-saved livre (x19-x28 integralmente ocupados), logo qualquer destino e
  caller-saved e a preservacao explicita e inevitavel - e o backend JA preserva explicitamente os
  demais caller-saved do guest (call_c/call_d fazem STP/LDP de R8/R9), fazendo do R8 um caso uniforme
  em vez de excecao; (3) todo o uso de x18 no dynarec e simbolico (xR8): remap e mudanca de tabela,
  nao de milhares de instrucoes; (4) idioma de tabela e "reservado -> nao mapeado" ja existem no
  proprio box64 (RV64/LA64 TO_NAT; PPC64LE exclui r2/TOC e r13/TLS). EVIDENCIA DE EXECUCAO: 45/45
  checks do harness de codegen, 15/15 do harness de frame, fuzz 2000 iteracoes sem divergencia,
  censo de disassembly com zero x18 no codigo emitido (ver §§20-29).
REJECTED_ALTERNATIVES=B (spill/reload puro: reescreve emissores quentes e multiplica superficie de
  erro); C (padrao _WIN32: exige escrever x18, proibido pela Apple, e nao existe valor de plataforma
  a manter); D (mapeamento sob #ifdef: cria duas verdades de codegen, impossibilita prova diferencial
  e viola "sem hardcodes Darwin espalhados"); E (evacuar xSavedSP para emu->xSPSave e dar x28 ao R8:
  correcao por ABI e zero custo de fronteira, porem altera a maquina de frame/call-ret e o reset de SP
  em ret_to_next, com modo de falha silencioso - mantida como alternativa se priorizada garantia por
  ABI; requer auditoria adicional de aninhamento antes de qualquer adocao).
EXPECTED_REGRESSION_RISK=BAIXO em Linux/Android (mudanca de operando para acessos ao R8; nenhuma
  instrucao de guest deixa de ser emitida; sem alteracao de frame); MEDIO na fronteira de helpers se
  algum dos 4 sitios for omitido - mitigado por teste de preservacao em chamada de helper, por
  controle negativo e por auditoria de disassembly. Nenhum risco em GPU/Wine/DXVK (fora de escopo).
EXPECTED_PERFORMANCE_IMPACT=PROXIMO DE ZERO: +1 instrucao no prolog e +1 no epilog por bloco
  (regroup de ldr/ldp ao remapear o slot 8) e +2 instrucoes (STR/LDR) em 4 sitios de helper, um deles
  emitido raramente (checkCRC). Custo de atribuicao de registrador inalterado (x8 permanece livre).
  NAO MEDIDO em benchmark: esta fase nao executou carga real de jogo (impossivel sem dispositivo).
```

### 12.3 Abstração central (requisito (g)) — como implementada

Princípio cumprido: **uma única fonte de verdade numérica**, com o codegen emitindo corretamente *automaticamente* a partir dela, e **zero `#ifdef __APPLE__`** em emissores (verificado: `git diff | grep -c '__APPLE__'` → `0`; `grep -rc '__APPLE__' src/dynarec/arm64/` → nenhum arquivo).

1. **Definição única do mapa** (`arm64_mapping.h`): `#define xR8 9`, com comentário normativo citando AAPCS64 ("The Platform Register, if needed; otherwise a Caller-saved register") e a regra Apple ("The platforms reserve register x18. Don't use this register.").
2. **`TO_NAT` como tabela gerada** (idioma RV64/LA64), substituindo `(xRAX + (A))`, que deixou de ser válida porque o mapa **não é mais afim** em A.
3. **`IS_GPR` como bitmap gerado** — e isto **não é cosmético**: a definição upstream era o teste de faixa `((A)>=xRAX && (A)<=xRIP)`, que passaria a **mentir** para o R8 (9 < 10), e `IS_GPR` é usado em 2 sítios reais do produto (`dynarec_arm64_helper.c:218,319`, ambos decidindo se `ret` é registrador do guest). Mantê-la intacta seria um **bug** introduzido pelo remap.
4. **Tabelas geradas, não digitadas**: `scripts/apply_strategy_a.py` contém os dados do mapa e **gera** `TO_NAT`/`IS_GPR`, com auto-verificação (16 valores distintos, nenhum slot = 18, `IS_GPR` coerente com `TO_NAT`) que **aborta** a aplicação do patch se falhar. Histórico: uma tentativa manual de bitmap já tinha saído 1 entrada curta — motivo pelo qual a geração é obrigatória.
5. **Registrador de plataforma nomeado**: `#define xPLATFORM 18`, usado nos 2 sítios simbólicos do TEB (§9.2) — impede que o TEB seja escrito no novo lar do R8.
6. **Tabelas de texto atualizadas**: `dynarec_arm64_functions.c` (r8/r8d/r8w/r8b) e `arm64_printer.c` (índice 9 → `"xR8"`, índice 18 → `"xPLATFORM"`).
7. **`arm64_mapping.h` continua sendo `#define`** (expressão constante de compilação), para não quebrar usos em `case`, `#if` e macros de emissão.

**Propriedade resultante (verificada):** **nenhum emissor de opcode foi editado** e, ainda assim, todo acesso ao R8 emite o registrador novo — porque os emissores falam `xR8`/`TO_NAT(8)`. O caminho Darwin deixa de ser "um caminho Darwin": passa a ser *o* caminho, com x18 ausente por construção.

---

## 13. Arquivos modificados

### 13.1 Código do produto — 8 arquivos, **+73 / −20 linhas**

```
$ cd /tmp/box64-ref && git diff --stat
 src/dynarec/arm64/arm64_epilog.S            |  2 +-
 src/dynarec/arm64/arm64_mapping.h           | 42 ++++++++++++++++++++++++++---
 src/dynarec/arm64/arm64_next.S              |  8 +++---
 src/dynarec/arm64/arm64_printer.c           |  8 +++---
 src/dynarec/arm64/arm64_prolog.S            |  2 +-
 src/dynarec/arm64/dynarec_arm64_functions.c |  8 +++---
 src/dynarec/arm64/dynarec_arm64_helper.c    | 17 ++++++++++--
 src/dynarec/arm64/dynarec_arm64_helper.h    |  6 +++++
 8 files changed, 73 insertions(+), 20 deletions(-)
```

**A mudança substantiva de mapa (`arm64_mapping.h`), em duas partes:**

```diff
-#define xR8     18
+// Guest R8 lives in x9, NOT in x18 (Fase 04 / Darwin).
+// AAPCS64 calls x18 "The Platform Register, if needed; otherwise a Caller-saved
+// register" and advises platform-independent code to avoid it; arm64-apple-ios
+// reserves it outright ("The platforms reserve register x18. Don't use this
+// register.").  x9 is a plain caller-saved temporary this backend never used for
+// anything else, so no guest register loses its home and x8 stays free as an
+// emergency scratch.
+// This mapping is platform independent on purpose: Linux, Android and Darwin keep
+// ONE register model (no per-platform #ifdef); x18 is simply unused except as the
+// Windows TEB in the _WIN32 paths (see xPLATFORM below).
+#define xR8     9
@@
-// convert a x86 register to native according to the register mapping
-#define TO_NAT(A) (xRAX + (A))
-#define IS_GPR(A) ((A)>=xRAX && (A)<=xRIP)
+// Host platform register.  NOT a guest register: x18 holds the Windows TEB under
+// _WIN32 (win64_teb), is unused on Linux/Android, and is reserved by arm64-apple-ios.
+#define xPLATFORM 18
+
+// convert a x86 register to native according to the register mapping.
+// Table form, like the RV64/LA64 backends: with guest R8 moved out of the
+// xRAX..xRIP range the mapping is no longer an affine function of the guest
+// register number, so xRAX+(A) cannot express it any more.
+// (Generated from the #defines above - do not hand-edit.)
+#define TO_NAT(A) (((uint8_t[]) { xRAX, xRCX, xRDX, xRBX, xRSP, xRBP, xRSI, xRDI, xR8, xR9, xR10, xR11, xR12, xR13, xR14, xR15 })[(A)])
+// Guest GPR slots by host register number (bitmap, same reason as TO_NAT).
+#define IS_GPR(A) (((uint8_t[]) { \
+    /*  0.. 7 */ 0, 0, 0, 0, 0, 0, 0, 0, \
+    /*  8..15 */ 0, 1, 1, 1, 1, 1, 1, 1, \
+    /* 16..23 */ 1, 1, 0, 1, 1, 1, 1, 1, \
+    /* 24..31 */ 1, 1, 1, 1, 0, 0, 0, 0, \
+    })[(A)])
```
(texto integral, incluindo as 14 linhas de comentário normativo, em `patched/arm64_mapping.h` e no patch `0001-…`).

**Nenhum emissor de opcode foi tocado.** Nenhum arquivo fora de `src/dynarec/arm64/` foi tocado. A substância da mudança está em 1 definição de mapa (`xR8 18→9`), 2 tabelas geradas, 3 linhas de `.S` (uma por arquivo), 2 sítios TEB simbólicos, 5 spill/reload (4 sítios) e 2 tabelas de texto.

### 13.2 Artefatos persistentes no workspace (`ios/box64-registers/`)

| Caminho | Conteúdo |
|---|---|
| `patch/0001-strategy-a-guest-r8-x18-to-x9.patch` | **fonte de verdade durável** da mudança (250 linhas, sha256 `bda64167…dda76`); **byte-idêntico** ao diff aplicado na árvore (verificado em §33) |
| `patched/` | espelhos dos 8 arquivos alterados (comparados com a árvore: idênticos — §33.3) |
| `include/new/arm64_mapping.h` | mapa + tabelas **geradas**, para o harness compilar contra o modelo novo |
| `include/orig/arm64_mapping.h` | mapa upstream, para o harness diferencial |
| `include/upstream/arm64_emitter.h`, `arm64_prolog.S`, `arm64_epilog.S`, `arm64_next.S` | originais upstream extraídos de `git show HEAD:` — permitem o build diferencial sem depender de `/tmp` |
| `scripts/apply_strategy_a.py` | dados do mapa + geração de `TO_NAT`/`IS_GPR` + auto-verificação + aplicação e re-aplicação idempotente do patch |
| `scripts/x18_census.py` | censo x18 do binário |
| `scripts/build_codegen_tests.sh`, `scripts/build_frame_tests.sh` | builds/reprodução completos dos dois harnesses (e do build mutante) |
| `scripts/warnings_pass.sh` | passe de avisos diferencial (upstream × patched) |
| `scripts/precommit_review.sh` | revisão pré-commit automatizada (§33) |
| `harness/` | `t_codegen.c` (T1–T9), `t_frame.S`/`t_frame_main.c` (T10–T15), `clobber.S`, `frame_call.S`, `emu_layout.h`, sonda ISA |
| `harness/artifacts/` | binários (regeneráveis), logs das execuções, dumps `.bin`/`.dis` |
| `evidence/` | censo, hashes, diffstat, checks de TU, auditoria de assembly, passe de avisos, tentativa Apple, revisão pré-commit |
| `docs/RELATORIO_FASE_04.md` | este documento |

### 13.3 Arquivos **não** modificados (e por quê)

* `arm64_emitter.h` — as macros de emissão falam `xR8`; nada a mudar (hash registrado em `evidence/emitter_header_sha256.txt`, `d2913a11…6e5`; verificado idêntico ao upstream em §33.3).
* `sigtools.h` / `sigtools.c` — o índice do registrador vem da macro do mapa; remapear o mapa **propaga** (§18.1).
* `arm64_lock.S` — usa apenas x0–x4/w0,w3; LSE é ortogonal ao modelo de registradores.
* `register_mappings[]` linhas `rsi/rdi/rsp/rbp` — defeito cosmético pré-existente, fora de escopo (§5.3).

---

## 14. Prolog

**Arquivo:** `src/dynarec/arm64/arm64_prolog.S`.

**Mudança (1 linha):**

```diff
@@ -26,7 +26,7 @@ arm64_prolog:
     ldp     x16, x17, [x0, (8 *  6)]
-    ldp     x18, x19, [x0, (8 *  8)]
+    ldp     x9, x19, [x0, (8 *  8)]      // guest R8 is in x9 (Strategy A); x18 stays the platform register
     ldp     x20, x21, [x0, (8 * 10)]
```

O `ldp` continua carregando **dois** registradores do guest (R8 em x9, R9 em x19) em **uma** instrução — a sonda ISA da §5.2 é o que autoriza isso (par não consecutivo é legal; ordem primeiro-registrador→endereço-menor preservada).

**Verificação executada (harness de frame, testes T10/T13):**

```
$ cd harness && timeout 120 qemu-aarch64 ./artifacts/frame/new/t_frame_new
[T10] guest R8 round trip through the real arm64_prolog/arm64_epilog
  PASS  guest R8 survives prolog -> block -> epilog for 16 values (0,1,-1,0x0123456789abcdef + 12 pseudo-random)
[T13] full register file round trip (prolog -> plain block -> epilog)
  PASS  all 16 guest GPRs come back unchanged (incl. R8 at regs[8])
  PASS  guest eflags come back unchanged
  PASS  guest rip comes back unchanged
```

O T13 é a prova de que o prolog **e** o epilog, executados de verdade, devolvem os 16 GPRs + flags + rip inalterados — inclui o slot 8 (R8 em x9) e o 9 (R9 em x19), que são os dois que o `ldp` remapeado carrega.

**Achados:**

* `x18` **não** é salvo nem restaurado pelo frame (upstream já era assim) e **não precisa** ser: sob a Estratégia A ele não carrega estado do guest. O TEB continua entrando em x18 apenas no caminho `_WIN32` do epilog (§15).
* nenhuma instrução adicional foi introduzida: o custo é exatamente o mesmo `ldp` (a afirmação "regroup de ldr/ldp" do rascunho se confirmou desnecessária — o remap preservou o par).

---

## 15. Epilog

**Arquivo:** `src/dynarec/arm64/arm64_epilog.S`.

**Mudança (1 linha):**

```diff
@@ -13,7 +13,7 @@ arm64_epilog:
     stp     x16, x17, [x0, (8 *  6)]
-    stp     x18, x19, [x0, (8 *  8)]
+    stp     x9, x19, [x0, (8 *  8)]      // guest R8 is in x9 (Strategy A)
```

**O caminho `_WIN32` foi deliberadamente preservado** (não tocado):

```asm
#ifdef _WIN32
    ldr     x18, [x0, 3104]     // TEB: x18 é o registrador de plataforma no Windows
#endif
```

Isso é o cumprimento literal do requisito "preservar o uso independente de x18 onde ele representa o TEB, não o guest R8".

**Verificação executada:** canários de callee-saved (T15), executando **o epilog real**:

```
[T15] callee-saved canaries around prolog/epilog (x19-x28, d8, d15)
  PASS  x19-x28, d8 and d15 all come back unchanged (11 canaries)
  ....  negative control reported 1 canary failure(s), first index 2
  PASS  negative control: corrupting saved x20 is detected
  PASS  negative control: exactly the x21 canary fails (index 2)
```

O **controle negativo** corrompe de propósito o slot de frame `[x28+16]` (que a leitura do prolog mostra ser o **x21** salvo — o harness inicialmente supôs x20/índice 1 e o teste pegou o erro do próprio harness: layout é `x19,x20 @0`, `x21,x22 @16`) e comprova que o verificador **não é vacuous**.

**Auditoria estática do par prolog+epilog+next (não-`_WIN32`), em `/tmp`:**

```
$ aarch64-linux-gnu-gcc -c <3 .S patched> ; objdump -d  (evidence/asm_audit_and_lock_check.txt)
 85 instruções no total
 x18: 0 ocorrências
 x9 : 6 ocorrências   (ldp do prolog, stp do epilog, 4× stp/ldp do next)
$ idem com -D_WIN32
 ldr x18, [x0, 3104]  → 3 ocorrências (epilog:1, next:2)  ← TEB preservado
```

---

## 16. Dispatcher

**Arquivo:** `src/dynarec/arm64/arm64_next.S`.

**Mudança (4 linhas, 2 pares `stp`/`ldp`):**

```diff
-    stp     x18, x27, [sp, (8 * 10)]    // also save x27(rip) to allow change in LinkNext
+    stp     x9, x27, [sp, (8 * 10)]    // also save x27(rip) to allow change in LinkNext
 ...
-    ldp     x18, x27, [sp, (8 * 10)]
+    ldp     x9, x27, [sp, (8 * 10)]
```
(o mesmo par em `arm64_next_invalid`; os `ldr x18,[x0,3104]` do bloco `_WIN32` **permanecem**).

**Por que isto é obrigatório e não cosmético:** `arm64_next` chama `LinkNext`, que é **código C compilado** — e um callee C pode usar x9 livremente. Sem o `stp/ldp` remapeado, o guest R8 morreria em **toda** transição de bloco.

**Verificação executada (T14) — 5 checagens, contra o stub C que envenena x1–x18:**

```
[T14] arm64_next: R8 across the C call to LinkNext + rip write-back
  PASS  arm64_next called LinkNext exactly once
  PASS  LinkNext received the guest rip from x27
  PASS  R8 was intact INSIDE the block reached through arm64_next
  PASS  R8 survives the LinkNext C call and the following block
  PASS  LinkNext's changed rip was written back (0x4242 -> 0x5242)
```

**Teste de mutação (prova de que a mudança sustenta peso):** build `mutant` = prolog/epilog remapeados **+ `next.S` upstream**. Resultado: **13 passaram, 2 falharam — exatamente as duas checagens de R8 do T14**:

```
=== running: mutant (next.S left upstream) ===
  PASS  arm64_next called LinkNext exactly once
  PASS  LinkNext received the guest rip from x27
  FAIL  R8 was intact INSIDE the block reached through arm64_next
  FAIL  R8 survives the LinkNext C call and the following block
  PASS  LinkNext's changed rip was written back (0x4242 -> 0x5242)
=== 13 checks passed, 2 failed ===
=== mutation detected: next.S change is load-bearing ===
```

Ou seja: a suíte **detecta** a omissão de um dos três arquivos `.S`. O `rip write-back` continua funcionando no mutante (o slot de x27 não mudou), o que mostra que a falha é específica do R8 e não ruído.

---

## 17. Helpers

### 17.1 `call_c` / `call_d` — já cobertos pelo upstream (nenhuma mudança)

`STPx_S7_offset(xR8, xR9, xEmu, offsetof(x64emu_t, regs[_R8]))` antes do `BLR` e `GO(R8,R9)` depois (`helper.c:559/571`, `611/629` na numeração upstream). Sob a Estratégia A isso continua correto **sem edição**: os emissores gravam `stp x9,x19,[x0,#64]` e recarregam `ldp x9,x19,[x0,#64]` — idêntico em forma ao que já faziam.

### 17.2 `READFLAGS` / `GRABFLAGS` (`dynarec_arm64_helper.h`, macros)

```diff
 #define READFLAGS(A) \
     if((A)!=X_PEND                                          \
     && (dyn->f==status_unk)) {                              \
+        /* defensive spill: guest R8 is in caller-saved x9 */ \
+        STRx_U12(xR8, xEmu, offsetof(x64emu_t, regs[_R8])); \
         TABLE64C(x6, const_updateflags_arm64);              \
         BLR(x6);                                            \
+        LDRx_U12(xR8, xEmu, offsetof(x64emu_t, regs[_R8])); \
         dyn->f = status_none;                               \
     } else if((A)==X_ALL) flushNative(dyn, ninst);
```
(bloco equivalente em `GRABFLAGS`; sítios em `helper.h:1008` e `:1020`).

### 17.3 `flagsCacheTransform` (`helper.c:2357-2367`)

Spill **antes** de `TABLE64C(x1, const_updateflags_arm64)` e reload **depois** de `BLR(x1)`. O comentário no código registra a medição que autorizou chamá-lo de *defensivo*: o bloco de flags gerado escreve apenas `x1..x5`/`xFlags` — mas a política da estratégia é **não depender** desse detalhe.

### 17.4 `checkCRC` (`helper.c:2997-3016`)

Spill **depois** de `MOVx_REG(x6, xEmu)` (i.e. com `xEmu` ainda válido) e reload **depois** de `MOVx_REG(xEmu, x6)`, cercando os dois `BLR(x3)` (`:3008`, `:3018`) para `const_native_crc32`/`const_native_x31`. Este era o sítio mais perigoso: o código **reutiliza `xEmu` como primeiro argumento da função C**, então o spill tinha de acontecer antes dessa reutilização — e acontece.

### 17.5 `call_n` — deliberadamente **sem** spill

`call_n` (`helper.c:678-683`) move RDI,RSI,RDX,RCX,R8,R9 para x0–x5 (`MOVx_REG(x4, xR8)` em `:671`), salva apenas `xEmu/xRBX/xRSP/xRBP` e chama com `BLR(16)`. A perda do R8 in-register é **legítima pela ABI do guest** (R8 é caller-saved no x86-64 SysV) e é exatamente o que já acontece com RAX/RCX/RDX/RSI/RDI hoje. Uniformizar o R8 com eles **é** o objetivo da estratégia.

### 17.6 Verificação de preservação em chamada de helper — 3 níveis

**(a) Código emitido (T7)** — o harness emite a sequência real de fronteira e a executa:

```
[T7] helper-call preservation across a platform-ABI host call
  ....  dumped t7_boundary_spilled        9 instructions -> ./t7_boundary_spilled.bin
  ....  dumped t7_boundary_nospill        7 instructions -> ./t7_boundary_nospill.bin
  PASS  WITH the spill (Strategy A): R8 survives a host call that clobbers its home register
  PASS  WITH the spill: R8 = 0 survives too
  PASS  WITH the spill: R8 = 0xffffffffffffffff survives too
  PASS  NEGATIVE CONTROL, no spill: R8 is lost (so the test can detect a missing spill)
```
Sequência remapeada (dump `new/t7_boundary_spilled.dis`): `str x30,[x0,#168]; ldr x9,[x0,#64]; str x9,[x0,#168+…]; ldr x3,…; blr x3; ldr x9,[x0,#64]; …; ret`.

**(b) Frame real com helper C compilado (T11/T12)** — dentro do prolog/epilog de verdade, chamando uma função C **compilada** (não asm), com o padrão exato do `call_c` (inclusive `STP/LDP` do `xEmu` em volta do `BLR`):

```
[T11] guest R8 across a real C helper call inside the frame
  PASS  the C helper really ran (poison sink written)
  PASS  spill/reload pattern keeps R8 across blr to compiled C code
[T12] negative control: same block, R8 spill removed
  ....  R8 after unspilled C call: 0x00000000deadbf00 (with the spill it would be 0x0123456789abce00)
  PASS  R8 is really lost when the spill is missing (control is sensitive)
```

**(c) Os macros do produto** — `READFLAGS`/`GRABFLAGS` foram compilados como parte de `dynarec_arm64_helper.c` (4 variantes `STEP=0..3`), `checkCRC`/`flagsCacheTransform` compilam no mesmo TU; e a auditoria de assembly do §27 confirma que o código emitido não menciona x18.

---

## 18. Signals/exceptions

**Escopo:** sinais, exceptions, `ucontext`, recuperação de fault e `longjmp`.

### 18.1 Caminho de sinais — consistente **por construção**

```
src/include/sigtools.h:10   #define CONTEXT_REG(P, X)  (P)->uc_mcontext.regs[X]     // ARM64/Linux
src/include/sigtools.h:14   #define CONTEXT_REG(P, X)  (P)->uc_mcontext.__gregs[X]  // Darwin
src/include/sigtools.h:18   #define CONTEXT_REG(P, X)  (P)->uc_mcontext.__gregs[X]
src/include/sigtools.h:22   #define CONTEXT_REG(P, X)  (P)->uc_mcontext.gp_regs[X]  // PPC64LE
$ grep -rn 'xR8\|x18' src/sigtools.c src/include/sigtools.h
   (vazio)
```

**Conclusão:** o caminho de sinais **não nomeia x18** — ele indexa `ucontext` pelo **número do registrador host**, obtido das macros do mapa (`xR8`). Remapear `arm64_mapping.h` propaga automaticamente para `copyUCTXreg2Emu`/`copyEmu2USignalCTXreg`; nenhum arquivo de sinal precisou (nem deve) ser editado. O ramo ARM64 não tem caso especial (LA64/LBT e RV64/OF2 são os únicos casos especiais do arquivo).

**Classificação:** `IMPLEMENTADA MAS NÃO VALIDADA` no que depende do kernel — a troca de contexto real (sinal entrando/saindo) só é verificável em silício Apple + iOS, o que é **`IPHONE_VALIDATION_PENDING`**.

### 18.2 `longjmp` / fault recovery — auditoria fechada

```
$ grep -rn 'longjmp\|siglongjmp' src/*.c src/emu/*.c
  src/emu/x64emu.c:618   if(emu->flags.quitonlongjmp)
  src/emu/x64emu.c:623   if(emu->flags.quitonlongjmp && emu->flags.longjmp) {
  src/emu/x64emu.c:624   if(emu->flags.quitonlongjmp==1)
  src/emu/x64emu.c:625       emu->flags.longjmp = 0;   // don't change anything because of the longjmp
$ # quais arquivos com longjmp tocam o guest R8?
$ grep -rl 'longjmp' src/ | while read f; do grep -q 'xR8\|regs\[_R8\]' "$f" && echo "TOUCHES R8: $f"; done
  (vazio)
```

**Conclusão:** o mecanismo de `longjmp` no box64 é manipulação de **flags do emu**; não reentra em bloco com estado in-register e não toca o lar do R8. Nenhum risco novo introduzido pela estratégia (a supressão do `longjmp` leva a *reiniciar* a execução, e reinício passa por `arm64_prolog`, que recarrega tudo de `emu->regs[]`).

### 18.3 Onde a fronteira de exceção **realmente** importa

O único ponto em que o modelo de registradores encontra as exceções do sistema é o `ucontext` trocado com o kernel (§18.1) — e ele é dirigido pelo mapa. Fica registrado, para honestidade, que a **validação física** dessa troca (sinal de verdade chegando no processo durante a execução de um bloco JIT) **não foi e não pode ser feita aqui** (§31).

---

## 19. Guest R8

### 19.1 A mudança, em uma linha

```diff
-#define xR8     18
+#define xR8     9
```
(acompanhada, no arquivo real, do comentário normativo transcrito em §13.1).

### 19.2 Testes de valor sobre o guest R8

Requisito: valores `0`, `1`, `0xFFFFFFFFFFFFFFFF`, `0x0123456789ABCDEF` + pseudo-aleatórios. Executado em dois níveis:

**(a) Round-trip pelo frame real (T10)** — 16 valores (os 4 exigidos + 12 pseudo-aleatórios determinísticos):

```
[T10] guest R8 round trip through the real arm64_prolog/arm64_epilog
  PASS  guest R8 survives prolog -> block -> epilog for 16 values (0,1,-1,0x0123456789abcdef + 12 pseudo-random)
```

**(b) MOV/zero-extension (T3) e aritmética (T4)** — no harness de codegen, com leitura por `LDRx_U12(xR8, xEmu, 64)` e comparação com o modelo de referência C:

```
  PASS  MOV R8 = 0xffffffff -> read back 0xffffffff
  PASS  MOV32 R8 = 0xffffffff -> read back 0xffffffff
  PASS  MOV R8 = 0xffffffff00000000 -> read back 0xffffffff00000000
  PASS  MOV32 R8 = 0xffffffff00000000 -> read back 0
  PASS  128 ALU cases executed, 0 mismatches vs reference model
```

### 19.3 Codificação verificada no nível da palavra de instrução (T2)

O T2 **não confia na leitura de volta**: decodifica os bits da instrução emitida e confere que o campo de registrador é **9**:

```
[T2] static codegen audit: every R8 access must encode host reg 9
  PASS  MOVx_REG(xR8,xEmu)         -> x9 <- x0 (expect 9 <- 0)
  PASS  STRx_U12(xR8,xEmu,64)      -> str x9,[x0,#64]
  PASS  LDRx_U12(xR8,xEmu,64)      -> ldr x9,[x0,#64]
  ...   (STPx_S7_offset, LDPx_S7_offset, MOVw_REG, ADDx_REG: idem)
```
Amostra do código emitido (`artifacts/new/t2_r8_ops.dis`):
`mov x9,x0; str x9,[x0,#64]; ldr x9,[x0,#64]; stp x9,x19,[x0,#64]; ldp x9,x19,[x0,#64]; mov w9,w0; add x9,x9,x1` — **7 instruções, 0× x18, 6× x9**; o mesmo teste no build upstream produz **0× x9, 6× x18** (§27).

---

## 20. Testes unitários

**Suítes, comandos e resultados (classificação `CONFIRMADA POR TESTE EM SIMULADOR (SIMULATOR ONLY)`):**

```
$ cd ios/box64-registers/harness
$ timeout 120 qemu-aarch64 ./artifacts/t_codegen_new . 0x5eedf04 2000
=== 45 checks passed, 0 failed ===
$ timeout 120 qemu-aarch64 ./artifacts/frame/new/t_frame_new
=== 15 checks passed, 0 failed ===
```

**Distribuição das checagens (modelo remapeado):**

| Teste | O que verifica | Checks |
|---|---|---|
| T1 | invariantes do mapa: 16 destinos distintos; **nenhum GPR em x18**; `xR8==9`; `TO_NAT(8)==xR8`; `xPLATFORM==18`; `IS_GPR` para R8/RAX/RIP e **falso** para 18, x8 e xSavedSP; imagem de `TO_NAT` = exatamente os 16 lares; **nenhum lar colide com xEmu/temporários/xLR/SP/ZR** | 10 |
| T2 | auditoria estática do codegen: decodifica a **palavra de instrução** e confere que todo acesso a R8 codifica o registrador **9** (MOV/STR/LDR/STP/LDP/MOVw/ADD) | 8 |
| T3 | MOV/zero-extension em 64 e 32 bits, ida e volta por `emu->regs[8]` | 16 |
| T4 | aritmética/lógica vs modelo de referência (§21) | 1 |
| T5 | isolamento: os outros 15 GPRs não são perturbados por tráfego em R8 | 1 |
| T6 | pressão de registradores (§24) | 3 |
| T7 | preservação em chamada de helper + controle negativo (§23) | 4 |
| T8 | persistência entre blocos A→B→C→D (§22) | 1 |
| T9 | fuzz determinístico (§26) | 1 |
| T10 | round-trip do guest R8 pelo **prolog/epilog reais**, 16 valores | 1 |
| T11 | spill/reload através de helper C **compilado**, dentro do frame real | 2 |
| T12 | controle negativo do spill (o valor realmente se perde) | 1 |
| T13 | round-trip do **arquivo de registradores inteiro** (16 GPRs + eflags + rip) pelo frame | 3 |
| T14 | dispatcher `arm64_next` + `LinkNext` (§16) | 5 |
| T15 | canários de callee-saved com controle negativo (§15) | 3 |
| | **Total** | **60** |

**Propriedade metodológica (requisito "testes que exercitam código real"):** o harness **não reimplementa** nada do produto. Ele (i) inclui o *header de mapa real* — `include/new/arm64_mapping.h` é byte-idêntico ao arquivo da árvore patcheada (`diff -q` → idêntico, conferido em §33.3); (ii) usa o *header de emissão real* (`arm64_emitter.h` upstream, não modificado) para gerar as instruções; (iii) monta e executa os *`.S` reais* (`arm64_prolog.S`, `arm64_epilog.S`, `arm64_next.S`, patcheados) no harness de frame; (iv) executa o código gerado em `qemu-aarch64`, com um `LinkNext` de teste que **envenena x1–x18** para que a preservação tenha de ser real.

---

## 21. Testes aritméticos

`T4` emite as operações com os **emissores reais** e as executa, comparando com um modelo de referência em C. Cobertura exata (do `enum` do harness):

```
OP_MOV, OP_ADD, OP_SUB, OP_XOR, OP_AND, OP_OR, OP_SHL, OP_SHR            (64 bits)
OP_MOV32, OP_ADD32, OP_SUB32, OP_XOR32, OP_AND32, OP_OR32, OP_SHL32, OP_SHR32  (32 bits)
```

Instruções emitidas: `MOVx_REG`/`MOVw_REG`, `ADDx_REG`/`ADDw_REG`, `SUBx_REG`/`SUBw_REG`, `EORx_REG`/`EORw_REG`, `ANDx_REG`/`ANDw_REG`, `ORRx_REG`/`ORRw_REG`, `LSLx_IMM`/`LSLw_IMM`, `LSRx_IMM`/`ORRw_REG_LSR`.

Resultado:

```
[T4] arithmetic/logic on R8 (64- and 32-bit) vs reference model
  PASS  128 ALU cases executed, 0 mismatches vs reference model
```

128 casos = 16 operações × 8 entradas (incluindo `0`, `1`, `0xFFFFFFFFFFFFFFFF`, `0x0123456789ABCDEF` e valores pseudo-aleatórios determinísticos). **Zero divergências.** O fuzz da §26 estende a cobertura dessas mesmas 16 operações com entradas aleatórias.

---

## 22. Testes entre blocos

`T8` constrói **quatro blocos reais** encadeados por saltos indiretos — o formato de despacho do dynarec — e segue o valor do guest R8 por cada hop:

* cada bloco **carrega** R8 de `emu->regs[8]` no estilo do prolog (`LDRx_U12(xR8, xEmu, 64)`) e o **grava de volta** no estilo do epilog;
* cada hop passa pelo encadeamento indireto (bloco → próximo bloco) e grava traço em `emu->h[]` (um slot por hop, para que a falha aponte **qual** hop perdeu o valor);
* asserção em C: o valor final é exatamente o esperado e **0 hops** divergiram.

```
[T8] cross-block persistence A->B->C->D through indirect jumps (dispatch shape)
  ....  after block A: R8 = 0x100000011 (expected 0x100000011)
  ....  after block B: R8 = 0x100002211 (expected 0x100002211)
  ....  after block C: R8 = 0x100332211 (expected 0x100332211)
  ....  after block D: R8 = 0x144332211 (expected 0x144332211)
  PASS  R8 survives A->B->C->D through indirect jumps (0 wrong hops)
```

Cada hop grava em um byte distinto (`+0x11`, `+0x22`, `+0x33`, `+0x44`), de modo que um valor perdido ou *stale* é detectável pela **posição** do lixo. O mesmo caminho de dispatcher é exercitado no frame real pelo `T14` (§16), que passa pelo `arm64_next.S` autêntico.

---

## 23. Helper-call tests

Esta é a fronteira de que a Estratégia A depende; por isso é testada em **três níveis**, com controle negativo em cada um (detalhes e transcrições em §17.6):

| Nível | Objeto | Resultado |
|---|---|---|
| Código emitido | sequência de spill/reload real (`str x9,[x0,#64] … blr … ldr x9,[x0,#64]`), executada com um callee que destrói x18/x9 | **PASS** com spill (3 valores de R8) / **PASS** no controle negativo (sem spill, R8 **se perde** — prova que o teste é sensível) |
| Frame real + C compilado | prolog→bloco→epilog autênticos, com `bl` a uma função C **compilada** e o padrão `STP/LDP xEmu` do `call_c` | **PASS** no padrão com spill; **PASS** no controle negativo (valor vira `0xdeadbf00…`) |
| Macros do produto | `READFLAGS`/`GRABFLAGS` (2 macro-sítios), `flagsCacheTransform`, `checkCRC` (2 `BLR`) compilam nos 10 TUs verificados e não emitem x18 (§27) | **PASS** (compilação + auditoria de assembly) |

**Observação honesta e relevante:** o helper C compilado do harness **não** usou x9 por acaso (`x9` ausente do seu disassembly, `evidence`/`artifacts/frame/new/hf_c_poison.dis`) — isto é, o host sequer demonstra o problema espontaneamente. É exatamente por isso que o controle negativo **força** o clobber de x9 e x18, modelando a liberdade que o ABI dá a qualquer callee, e que a decisão de projeto foi tomada por ABI (§§6–7), não por observação de host.

---

## 24. Register-pressure tests

| Teste | Construção | Resultado |
|---|---|---|
| T5 | R8 recebe tráfego (add/sub/xor…) e os demais 15 GPRs ficam carregados em seus lares | `PASS regs[0..7,9..15] unchanged while regs[8] was modified (0 disturbed)` |
| T6 | 16 GPRs vivos simultaneamente: `ldr` de todos → `add #1` em todos → `str` de todos, em **3 rodadas** | `PASS round 0/1/2: 16 GPRs loaded, incremented, stored back (0 wrong)` |
| T13 | Frame real: carrega os 16 GPRs distintos, eflags e rip, e verifica que **todos** voltam inalterados | 3× `PASS` |

Comando (excerto do log de T6, mostrando o que é exercitado):

```
LDRx_U12(TO_NAT(i), xEmu, H_REGS(i));    for i in 0..15
ADDx_REG(TO_NAT(i), TO_NAT(i), x1);      for i in 0..15
STRx_U12(TO_NAT(i), xEmu, H_REGS(i));    for i in 0..15
```

O valor de partida usa o topo da faixa (`0xabc0000000000000 + i*0x0123456789`) para que qualquer confusão de slot apareça imediatamente. Ponto de atenção coberto: sob o remap, **x9 é um temporário do emissor** e x19–x25 continuam callee-saved — o T6 e o T13 juntos provam que o R8 remapeado convive com um arquivo de registradores cheio sem derramar em ninguém.

---

## 25. Differential tests

**Hipótese estrutural do modelo adotado:** como a Estratégia A é **plataforma-independente** (uma única verdade de codegen, `0` ocorrências de `__APPLE__`), "diferencial Linux-vs-Darwin" não é mais uma comparação de dois caminhos — é a comparação entre **o modelo novo** e **o modelo upstream**, isto é, uma verificação de *não-regressão* e de *efetividade*.

**Build diferencial:** mesmos fontes de teste, `-I include/orig` (mapa upstream) + os `.S` upstream extraídos de `git show HEAD:`.

| Suíte | Modelo novo (x9) | Modelo upstream (x18) |
|---|---|---|
| Codegen (T1–T9) | **45 PASS / 0 FAIL** | **32 PASS / 12 FAIL** |
| Frame (T10–T15) | **15 PASS / 0 FAIL** | **15 PASS / 0 FAIL** |
| Mutante (`next.S` upstream + resto remapeado) | — | **13 PASS / 2 FAIL** ← falha esperada |

**As 12 falhas do build upstream são todas estruturais** (asserções sobre o modelo), não comportamentais: `xR8 == 9` (era 18), `TO_NAT(8) == xR8`, imagem de `TO_NAT` sem 18, `IS_GPR(18)` falso, e as 7 checagens do T2 que decodificam as palavras de instrução (que no modelo antigo codificam 18). Não são "bugs do upstream": são as asserções que **definem** o modelo que esta fase adotou.

**Regra "divergência comportamental = FAIL" — cumprida:**

```
$ diff <(sed -n '/^\[T3\]/,/^=== /p' artifacts/t_codegen_new.log  | grep -v '^=== ') \
       <(sed -n '/^\[T3\]/,/^=== /p' artifacts/t_codegen_orig.log | grep -v '^=== ')
   (sem saída: T3–T9 idênticos entre os dois modelos)
```

Ou seja: **todo comportamento observável (aritmética, persistência entre blocos, pressão, fuzz) é idêntico** entre os dois mapas; toda diferença é a mudança de registrador pretendida.

**Por que o build de frame passa nos dois lados:** cada modelo é internamente consistente (os `.S` e o mapa de cada build combinam). O que discrimina é o **mutante** — e ele falha exatamente nas duas checagens de R8 (§16). É a evidência de que a mudança em cada um dos três `.S` **sustenta peso**.

---

## 26. Fuzz determinístico

```
[T9] deterministic fuzz vs reference model (seed=0x5eedf04, 2000 iterations)
  PASS  2000 fuzz iterations, 0 divergences from the reference model
```

* **Gerador:** LCG determinístico com seed registrada (`0x5eedf04`, a mesma do comando em §20), reproduzível bit a bit; a mesma seed produz os mesmos 2000 casos em qualquer máquina.
* **O que é comparado:** cada iteração sorteia operação (as 16 da §21), valor inicial de R8 e operando; o harness **emite** a instrução com o emissor real, **executa** em `qemu-aarch64` e compara com o modelo de referência C. Divergência conta como falha.
* **Resultado:** 2000 iterações, **0 divergências**; nenhuma falha residual, nenhum caso pulado.
* Valores pseudo-aleatórios também são usados nas checagens de valor do guest R8 (T10: 12 valores gerados) e nas 16 GPRs simultâneas (T13).

*Ressalva:* fuzz em simulador não substitui silício (§31); ele prova consistência interna do codegen remapeado com o modelo de referência, não o comportamento da CPU Apple.

---

## 27. Disassembly audit

### 27.1 Critério

**ZERO `x18` no código produzido pelo caminho remapeado**, exceto ocorrências externas/justificadas. As justificadas são exatamente: (i) os `ldr x18,[x0,3104]` do caminho **`_WIN32`** (TEB — não existem no caminho Darwin, porque estão dentro de `#ifdef _WIN32`); (ii) menções em **comentários** e **tabelas de texto** de impressão.

### 27.2 Censo do código emitido (arquivos `.bin` dumpados pelo harness)

```
$ for d in new orig; do for f in t2_r8_ops t7_boundary_spilled t7_boundary_nospill; do
    printf '%-6s %-22s instrs=%s x18=%s x9=%s\n' ...; done; done
new    t2_r8_ops              instrs=7 x18=0 x9=6
new    t7_boundary_spilled    instrs=9 x18=0 x9=4
new    t7_boundary_nospill    instrs=7 x18=0 x9=2
orig   t2_r8_ops              instrs=7 x18=6 x9=0
orig   t7_boundary_spilled    instrs=9 x18=4 x9=0
orig   t7_boundary_nospill    instrs=7 x18=2 x9=0
```

**Leitura:** mesma forma, mesma contagem de instruções, **troca exata** de x18 por x9. Nenhuma instrução extra foi introduzida ("9 instruções" nos dois lados do teste de fronteira) e nenhuma foi perdida. O critério é atendido: **x18 = 0** no código emitido pelo modelo novo.

### 27.3 Auditoria do código de runtime remapeado (`_WIN32` excluído)

```
$ aarch64-linux-gnu-gcc -c <patched prolog/epilog/next> ; objdump -d   (evidence/asm_audit_and_lock_check.txt)
não -D_WIN32 : 85 instruções → x18 = 0, x9 = 6   (ldp do prolog, stp do epilog, 2×(stp,ldp) do next)
com -D_WIN32 : 3× ldr x18,[x0,#3104]             (epilog:1, next:2) ← TEB preservado por projeto
```

### 27.4 O que **não** é x18 acionável

As 9 609 ocorrências de `x18` no binário empacotado (§3) estão, em 99,6 %, em **código de host** (GTK/wrappers/`crtstuff`) que **não é produzido pelo dynarec** e não é objeto desta fase. No dynarec, após a mudança, x18 só existe no caminho `_WIN32` (TEB) e em comentários/texto.

**Ferramenta de reprodutibilidade:** `scripts/x18_census.py` no binário; para o código emitido, os dumps `.bin`/`.dis` ficam em `harness/artifacts/{new,orig}/` e são regenerados por `scripts/build_codegen_tests.sh`.

---

## 28. Regressão Linux

### 28.1 O que garante que Linux/Android não regridem

1. **Uma única verdade de codegen.** Não há mapa condicional: `git diff | grep -c '__APPLE__'` → `0`; `grep -rc '__APPLE__' src/dynarec/arm64/` → nenhum arquivo. O Linux passa a rodar *exatamente* o mesmo modelo que o Darwin — o que elimina a classe de bug "código validado só em um dos caminhos".
2. **Sem mudança de frame.** `arm64_prolog.S`/`arm64_epilog.S` mantêm o mesmo tamanho de frame, os mesmos saves/restores e o mesmo número de instruções (§§14–15); o `T13` comprova em execução o round-trip completo do arquivo de registradores.
3. **Sem mudança de estrutura de codegen.** Nenhum emissor de opcode foi editado; muda apenas o operando dos acessos a R8 (§13.1).
4. **Compilação verificada em 10 TUs + `dynarec_native_pass`:** `dynarec_arm64_helper.c` (STEP=0,1,2,3), `dynarec_arm64_functions.c`, `arm64_printer.c`, `dynarec_arm64_emit_{tests,math,logic,shift}.c`, `dynarec/dynarec_native_pass.c` (STEP=0 e 3) — todos **OK** (`evidence/build_tu_checks.txt`, `evidence/warnings_pass.log`).
5. **Avisos: paridade exata com o upstream** nos TUs canônicos (§28.2).
6. **`arm64_lock.S` intocado** e ortogonal: usa apenas x0–x4/w0,w3 (LSE); o erro de montagem com `-march` default é pré-existente e independente do remap.
7. **Ganho de robustez no Linux:** a premissa frágil "x18 sobrevive a chamadas C" (não garantida por ABI, ausente de `-ffixed-x18`) **desaparece** — o R8 agora é preservado **explicitamente** onde precisa.

### 28.2 Passe de avisos, diferencial upstream × patched

`-Wall -Wextra -Wpedantic -Wshadow -Wconversion`, sem silenciamento global (`git diff | grep -c '#pragma .*diagnostic\|Wno-'` → `0`):

| TU | upstream → patched |
|---|---|
| `dynarec_arm64_helper.c` STEP=0 / 1 / 2 | 179→179 / 183→183 / 191→191 — **paridade exata, 0 tipos novos** |
| `dynarec_arm64_functions.c` | 82→82 — paridade |
| `arm64_printer.c` | 173→173 — paridade |
| `emit_tests` / `emit_math` / `emit_logic` / `emit_shift` | 22→22 / 53→53 / 24→24 / 47→47 — **paridade** |
| `dynarec_arm64_helper.c` STEP=3 *(configuração não-canônica: o CMake não compila este arquivo com `-DSTEP=3`)* | 4487→4531: **0 tipos novos**; +44 **instâncias** de 3 categorias **pré-existentes do upstream** (`binary constants ... C23`, `void* arithmetic` em `dynarec_arm64_pass3.h:11`, `-1073741824` sign-conversion), todas localizadas em **corpos de macro do upstream** (`pass3.h`/`arm64_emitter.h`) expandidos pelas linhas que adicionei (`STRx_U12`, `LDRx_U12`, `TABLE64C`, `BLR`/`EMIT`) — **classificação: BENIGN (herdado)** |
| Código novo do harness | **0 avisos** após correção na origem (2.ª rodada: 8 avisos → 0; corrigidos tipos de função em vez de casts, `%lx` com `unsigned long`, literais `UL`, loop em vez de `memset` sobre `volatile`) — **sem supressão de flags** |

### 28.3 O que **não** foi feito (limitação de ambiente, declarada)

* **Build completo do box64 (link)**: `LINUX_FULL_BUILD=UNTESTED REASON=RESOURCE_LIMIT` (~350 TUs, 2 vCPU, ~1,98 GB). A fase prescreve exatamente esta escada (TU → objeto → harness) e proíbe OOM-repeat.
* **Execução do box64 Linux real** (rodar um binário x86-64): não feita — exigiria o build completo e um rootfs de teste.
* Nenhuma dessas lacunas é mascarada: elas aparecem também na §32 como risco.

---

## 29. Regressão AArch64

| Verificação | Comando / evidência | Resultado |
|---|---|---|
| Codificação de par não consecutivo em `STP`/`LDP` | `pair_test.S` + `pair_main.c` sob `qemu-aarch64` | aceito; ordem **primeiro registrador → endereço menor** (§5.2) |
| Todas as instruções emitidas são AArch64 válidas | decodificação por `aarch64-linux-gnu-objdump` dos dumps do harness | 23 instruções decodificadas, 0 inválidas |
| Campo de registrador correto por palavra de instrução | T2 (decodifica bits, não só o resultado) | 8/8 PASS |
| Nenhum registrador duplicado/perdido | T1 (16 distintos + nova checagem de não-aliasamento com xEmu/temporários/xLR/SP/ZR) | 10/10 PASS |
| Frame preserva callee-saved reais da ABI | T15 (x19–x28, d8, d15 + controle negativo) | 3/3 PASS |
| Instrução não depende de silício Apple | sonda ISA é ISA-genérica | `HIPÓTESE` até validação física — a instrução é genérica, mas **não medida em silício Apple** nesta fase |
| Mudança de um `.S` é detectável quando omitida | build mutante (§16) | 2 falhas exatas → mutação detectada |

**Nota de escopo:** "regressão AArch64" aqui é regressão do **backend ARM64 do box64**, não dos demais backends (RV64/LA64/PPC64LE não foram tocados: `git status --short` mostra apenas arquivos sob `src/dynarec/arm64/`).

---

## 30. Apple-target checks

### 30.1 Ambiente e veredito

```
$ clang --version | head -1        → Debian clang version 19.1.7
$ command -v xcrun                  → NOT PRESENT
$ find / -maxdepth 6 -path '*SDK*' -name stdio.h   → (vazio)
$ clang -target arm64-apple-ios17.0 -fsyntax-only probe.c
  /usr/include/stdint.h:26:10: fatal error: 'bits/libc-header-start.h' file not found
```

**`IOS_BUILD=UNTESTED REASON=NO_APPLE_TOOLCHAIN`** — não há SDK Apple, `libSystem`, runtime nem dispositivo nesta máquina. **Nenhuma classificação `IOS_BUILD_PASS` foi emitida** (e nenhuma será, sem SDK/link/runtime).

### 30.2 O que **foi** possível medir para o alvo Apple (transcrito em `evidence/apple_target_attempt.txt`)

| Verificação | Comando | Resultado |
|---|---|---|
| O **header alterado** compila para o alvo Apple? | `clang -target arm64-apple-ios17.0 -nostdinc -I<stub stdint.h> -Iinclude/new -fsyntax-only hdr_pos.c` | **rc=0** — o probe usa `xR8`, `TO_NAT(8)` e `IS_GPR(8)`; controle negativo (`xR8_TYPO`) é **rejeitado** (rc=1), provando que a checagem não é vacuosa |
| Mesmo teste no header **upstream** | idem, `-Iinclude/orig` | rc=0 (positivo) / rc=1 (negativo) |
| Prolog/epilog remapeados em **Mach-O** | `clang -target arm64-apple-ios17.0 -c patched/arm64_prolog.S` e `arm64_epilog.S` | **ASSEMBLED** (sintaxe Mach-O aceita) |
| `arm64_next.S` em Mach-O | `clang -target arm64-apple-ios17.0 -c patched/arm64_next.S` | **FAILED**: `assembler local symbol 'LinkNext' not defined` (2 erros) |
| O mesmo no arquivo **upstream** | `clang -target arm64-apple-ios17.0 -c include/upstream/arm64_next.S` | **FAILED idêntico** (2 erros) — as linhas `.extern` são **byte-idênticas** entre upstream e patched → **defeito pré-existente do upstream**, não introduzido por esta fase |

**Leitura honesta:** (i) o header gerado (a peça central da abstração) é **válido para o alvo Apple** em nível de sintaxe; (ii) as `.S` remapeadas usam apenas instruções e diretivas aceitas pelo assembler Apple, exceto pelo problema de nomenclatura de símbolos do `next.S`, que vem do upstream (os `.S` do box64 são escritos com convenções ELF/`.extern`; um port real precisa de uma camada de nomenclatura Mach-O ou de ajuste das diretivas). Isto está registrado como risco (§32) e **não** como sucesso de build iOS.

### 30.3 O que **não** pode ser afirmado

Nada sobre link, assinatura de código, JIT no iOS, execução ou desempenho em iPhone. `STRUCTURALLY_RESOLVED ≠ VALIDATED_ON_IPHONE`.

---

## 31. Dependências da validação física

Esta fase resolve o **modelo de registradores** (estrutural). O que continua dependendo de aparelho físico — herança direta da Fase 02, **não** reivindicada como resolvida:

| Item | Estado | Por que precisa de dispositivo |
|---|---|---|
| `MAP_JIT` | `PHYSICAL_VALIDATION_REQUIRED` | só existe no XNU/arm64; sem SDK não há como sequer linkar |
| `pthread_jit_write_protect_np` | `PHYSICAL_VALIDATION_REQUIRED` | API do libSystem do iOS |
| Mapeamento duplo RW/RX (`vm_remap`) | `PHYSICAL_VALIDATION_REQUIRED` | comportamento do VM do XNU; W^X é preferência de projeto |
| JIT executando código gerado no iPhone | `PHYSICAL_VALIDATION_REQUIRED` | entitles/código assinado; proibido contornar segurança do SO |
| Conflito 16 KiB (host) × 4 KiB (guest) | **document-only** | exige `PAGE_SIZE` real do dispositivo (`PAGE_SIZE=<valor real>`); proibido hardcode de 16384 |
| Troca de `ucontext` com o kernel em sinal real | `PHYSICAL_VALIDATION_REQUIRED` | §18.1 depende do kernel do dispositivo entregar o frame correto |
| Comportamento de x18 sob o kernel Apple | `PHYSICAL_VALIDATION_REQUIRED` | é a razão de ser da estratégia; não é observável em `qemu-aarch64` |
| Testes de frame sob a ABI Apple real (red zone, frame record) | `PHYSICAL_VALIDATION_REQUIRED` | o host Linux tem ABI próxima, não idêntica |

**Classificação transversal da fase:** `PHASE_04_STRUCTURALLY_COMPLETE` **+** `IPHONE_VALIDATION_PENDING` — as duas coexistem; a segunda não é substituível por nenhum resultado de host.

---

## 32. Riscos restantes

| # | Risco | Severidade | Mitigação / estado |
|---|---|---|---|
| 1 | Um **novo `BLR`** adicionado ao backend no futuro pode não seguir a disciplina de spill | Média | invariante escrito no próprio `arm64_mapping.h` ("guest R8 é explícito em toda fronteira") + §11 com a enumeração dos sítios + testes T7/T11/T12 que falham se o spill sumir |
| 2 | Caminho `_WIN32` (TEB em `x18` via `xPLATFORM`) é **correto por construção, mas não testado** | Média | análise em §9; `BLOQUEADA` por ausência de toolchain Windows ARM64; nenhuma alteração semântica no Windows (o TEB continua em x18) |
| 3 | **Sem build completo/link** do box64 | Média | 10 TUs + `native_pass` compilados; escada de verificação da fase respeitada; link completo é dependência de ambiente (§28.3) |
| 4 | `register_mappings[]` com `rsi/rdi/rsp/rbp` trocados (pré-existente, cosmético) | Baixa | documentado em §5.3; corrigir é mudança de outro escopo; **não** corrigido de propósito |
| 5 | `.S` do box64 usam convenções ELF (`.extern`), que não montam em Mach-O no `arm64_next.S` | Média | **pré-existente** (idêntico no upstream, §30.2); um port real precisa de camada de nomenclatura/diretivas |
| 6 | `long double` (64 bits na Apple) e varargs (passagem na pilha) | Baixa / fora de escopo | backend ARM64 não usa `long double`/`__float128`; `call_n` só serve wrappers de aridade fixa; auditoria própria em fase futura |
| 7 | **Impacto de performance não medido** | Baixa | análise: +2 instruções por bloco e +2 em 4 sítios de helper; medir exige carga real (dispositivo) |
| 8 | x18 em **código de host** (GTK/wrappers, 99,6 %) | Fora de escopo | não é produzido pelo dynarec; nenhuma ação nesta fase |
| 9 | Resultados de `qemu-aarch64` não são resultados de silício Apple | Alta (para afirmações) | toda classificação fica em `SIMULATOR ONLY` / `IPHONE_VALIDATION_PENDING`; nenhuma alegação de "funciona no iOS" |

---

## 33. Estado final do Git

### 33.1 Revisão pré-commit (automatizada, `scripts/precommit_review.sh` → `evidence/precommit_review.txt`)

12 verificações, todas **limpas**:

| # | Verificação | Resultado |
|---|---|---|
| 1 | `git status --short` da árvore de referência: nada inesperado | 8 arquivos, todos sob `src/dynarec/arm64/` |
| 2 | Tamanho do change set (substituição em massa seria enorme) | **+73/−20**, 8 arquivos |
| 3 | **O patch durável é byte-idêntico ao diff aplicado?** | **SIM** (250 linhas; sha256 `bda64167…dda76`) — a durabilidade está garantida no workspace |
| 4 | Toda linha alterada que menciona x18/x9 em contexto | inspecionada linha a linha: nenhuma substituição cega |
| 5 | Censo x18 por arquivo (upstream → patched) | `prolog 1→1`, `epilog 2→1`, `next 6→2`, `mapping 0→5` (comentários + `xPLATFORM`), `functions 3→0`, `helper 4→6` (comentários), `printer 0→0` |
| 6 | Precedente `_WIN32`/TEB preservado | 3× `ldr x18,[x0,3104]` intactos + 2 sítios simbólicos agora usam `xPLATFORM` |
| 7 | Nenhum `#ifdef` por plataforma infiltrado | `__APPLE__` → **0** no patch e 0 na árvore |
| 8 | Nenhum aviso suprimido | `#pragma diagnostic`/`-Wno-` adicionados → **0** |
| 9 | Quebra das premissas afins (`TO_NAT`/`IS_GPR`) | documentada e corrigida (161 e 2 usos reais) |
| 10 | Código morto / fallback falso | **nenhum** (nenhum `#if/#else/fallback/TODO` adicionado) |
| 11 | Testes exercitam código real | `include/new` == arquivo patcheado; harness monta os `.S` reais |
| 12 | Espelhos × árvore | 8/8 arquivos **idênticos**; `arm64_emitter.h` idêntico ao upstream |

Caça específica aos 8 alvos do requisito (o): substituição acidental de x18 → **não há**; alteração não intencional do caminho Linux → **não há** (nenhuma linha fora de `src/dynarec/arm64/`, nenhum `#ifdef` novo); ABI quebrada → **não há** (frame e classes de registrador preservadas; T13/T15 provam); registrador duplicado → **não há** (T1: 16 distintos + sem aliasamento); código morto → **nenhum**; fallback falso → **nenhum**; aviso suprimido → **nenhum**; teste que não exercita código real → **nenhum** (comandos e artefatos em §20).

### 33.2 Estado da árvore do entregável antes do commit

```
$ cd /home/user/winlator && git status --short
 D gladio
 D vortek
?? ios/

$ git diff --stat
 (vazio para arquivos rastreados — as duas entradas ' D' são pré-existentes e não integram o commit, §2)
```

### 33.3 Commit

```
$ git checkout -b ios-phase-04-box64-registers
$ git add ios                  # caminho explícito: não toca as duas deleções pré-existentes
$ git commit -m "Adapt Box64 ARM64 register model for Darwin iOS"
```

**Conteúdo do commit:** `ios/` (67 arquivos) — o patch durável, os espelhos, os headers gerados/upstream, os 5 scripts, o harness (fontes + dumps `.bin`/`.dis` + logs de execução) e as evidências. **Não** entram os binários de teste compilados: `ios/.gitignore` foi estendido para excluí-los (`t_codegen_new/orig`, `t_frame_{new,orig,mutant}`), com comentário explicando que são **produtos de build** regeneráveis pelos *scripts* — os **logs** que provam as execuções e os dumps de disassembly **são** commitados.

**Sem force, sem rebase, sem reset:** único commit, branch novo, nada sobrescrito. O hash do commit é reportado na mensagem de entrega (não pode constar deste arquivo, que faz parte do próprio commit).

### 33.4 Estado da árvore de referência (box64)

`/tmp/box64-ref` fica com os 8 arquivos modificados **não commitados** (é uma árvore de referência volátil, usada para compilar/medir). A **fonte de verdade durável** é o patch em `ios/box64-registers/patch/`, verificado byte-idêntico (§33.1, linha 3) e reaplicável de forma idempotente por `scripts/apply_strategy_a.py` — este script **aborta** se a auto-verificação das tabelas geradas falhar.

---

## 34. Recomendação para próxima fase

**Classificação final (exatamente uma):**

```
PHASE_04_STRUCTURALLY_COMPLETE
IPHONE_VALIDATION_PENDING
```

A estrutura está completa e verificada no que é verificável sem Apple SDK e sem dispositivo: modelo remapeado, 60 checagens verdes nas duas suítes, fuzz de 2000 iterações sem divergência, censo de disassembly com **zero x18** no código emitido, passe de avisos com paridade exata, revisão pré-commit limpa e classificação de risco documentada. **Nada disso é validação em iPhone** — `IPHONE_VALIDATION_PENDING` continua valendo, porque depende de hardware, SDK e runtime Apple.

**Recomendações, em ordem:**

1. **Validação física antes de qualquer nova fase de código.** Os itens da Fase 02 (`MAP_JIT`, `pthread_jit_write_protect_np`, mapeamento duplo, JIT no iPhone) e o novo item desta fase (comportamento real de x18 sob o kernel Apple) só se resolvem em dispositivo. Sem isso, qualquer avanço adicional acumula `HIPÓTESE`.
2. **Build completo do box64 em máquina com recursos** (> 4 GB, ≥ 4 vCPU) para fechar a lacuna de link (`LINUX_FULL_BUILD=UNTESTED`, §28.3) e — com um rootfs x86-64 — rodar carga Linux real. Esta é a verificação de custo/benefício mais alto depois do item 1.
3. **Auditoria recorrente das fronteiras C** ao portar atualizações do upstream: qualquer `BLR` novo precisa entrar na tabela da §11 (o invariante está escrito no código, mas a auditoria é manual).
4. **Camada de nomenclatura Mach-O** para os `.S` (achado pré-existente da §30.2) — necessária apenas quando um build iOS real for tentado.
5. **Não** iniciar Fase 05, **não** fazer fork, **não** mexer em Wine/DXVK, **não** trabalhar 16K/4K, **não** afirmar que "o Box64 funciona no iOS". A fase termina aqui, em aguardo de revisão humana.

---

## Apêndice A — Reprodução (comandos essenciais)

```bash
# 0. ambiente
apt-get install -y libc6-dev-arm64-cross qemu-user aarch64-linux-gnu-binutils clang

# 1. patch durável -> árvore de referência
cd /tmp/box64-ref && git checkout 2f130fab1d6e1a4ee8a71dc60cfdfcc839ad192a
python3 /home/user/winlator/ios/box64-registers/scripts/apply_strategy_a.py   # aplica + auto-verifica

# 2. censo x18 do binário empacotado
python3 ios/box64-registers/scripts/x18_census.py <box64 aarch64>

# 3. harnesses (build + execução + comparação diferencial)
sh ios/box64-registers/scripts/build_codegen_tests.sh      # 45/0 (novo) | 32/12 (upstream) | T3-T9 idênticos
sh ios/box64-registers/scripts/build_frame_tests.sh        # 15/0 | 15/0 | mutante 13/2 (detectado)

# 4. avisos e revisão pré-commit
BOX64=/tmp/box64-ref sh ios/box64-registers/scripts/warnings_pass.sh
BOX64=/tmp/box64-ref sh ios/box64-registers/scripts/precommit_review.sh
```

## Apêndice B — Afirmações que **não** podem ser feitas a partir deste relatório

* "O Box64 roda no iOS." — não testado; JIT no iOS não é fato desta fase.
* "O caminho Darwin foi validado." — estrutura verificada em host/qemu; dispositivo não.
* "A estratégia A está correta **no iPhone**." — está implementada e verificada em simulador; a prova física é `IPHONE_VALIDATION_PENDING`.
* "x9 é preservado através de chamadas." — é **caller-saved**; a estratégia o preserva **explicitamente** nas fronteiras (e os testes provam que, sem o spill, o valor se perde).
* "O `_WIN32` foi portado." — foi documentado e teve o sítio simbólico corrigido para `xPLATFORM`; o mecanismo em si é proibido no Darwin.
* "Build iOS passou." — `IOS_BUILD=UNTESTED REASON=NO_APPLE_TOOLCHAIN`.
* "Zero x18 no Box64 inteiro." — zero no **código emitido** pelo caminho remapeado e zero no runtime ARM64 fora do `_WIN32`; o binário empacotado contém 9 609 ocorrências de x18 em **código de host** (GTK/wrappers/runtime C), fora do escopo.
