# Bugs e decisões — JVMA v6.0 (análise, código inalterado)

Documento da subtarefa 5. **Nenhuma correção foi aplicada**: `jvm_analyzer.c`, `Makefile`, `README.md` e `docs/analise/01-baseline.md` / `02-mapa-codigo.md` / `03-refatoracao-modularidade.md` / `04-performance.md` permanecem intocados (hashes em §13). Escopo restrito à criação deste arquivo.

Data das reproduções: 2026-11-24, mesmo ambiente do baseline (`LANG=pt_BR.UTF-8`, `LC_ALL` vazio, `CLK_TCK=100`, 12 CPUs). Comandos executados a partir da raiz do repositório; saídas abreviadas quando determinísticas.

Numeração de subtarefas (base: `04-performance.md` §0, estendida pelo plano de correções 7–12): `8` = export, `9` = input/CLI, `10` = layout/buffer, `11` = Makefile/robustez do build, `15` = proc/alertas; `6` é o harness de testes e não implementa correções.

Preparação compartilhada para as reproduções com `gdb` (cópia em `/tmp`, fonte intacto):

```bash
cd /home/kd6-cowboy/tools/JVM-Analyzer
gcc -O0 -g -Wall -Wextra -Wno-unused-result -Wno-format-truncation \
  -o /tmp/opencode/jvma-dbg jvm_analyzer.c -lm
```

---

## 0. Resumo executivo

| # | Item | Linha(s) | Severidade | Status | Subtarefa |
|---|---|---|---|---|---|
| 1 | `cmdline` sem escape JSON | `jvm_analyzer.c:646` | **P0** | `corrigir` | 8 (export) |
| 2 | Notificação de export bloqueante/flicker | `:659–663`, `:686–689` | P2 | `corrigir` | 8 (export) |
| 3 | Painéis estourando o footer em 70x18 e 80x24 | `:1097–1133` | **P1** | `corrigir` | 10 (layout) |
| 4 | `%.*s` com precisão negativa em `draw_panel_hdr` | `:739–753` | P2 | `corrigir` | 10 (layout) |
| 5 | Underflow unsigned no delta de CPU | `:324` | **P1** | `corrigir` | 15 (proc) |
| 6 | `t->daemon` sempre 0 (verificado) | `:508` | P2 | `documentar` | 8/15 (documentação do schema) |
| 7 | `atoi` permissivo na CLI | `:1218` | **P1** | `corrigir` | 9 (input/CLI) |
| 8 | `volatile sig_atomic_t` / `sigaction` | `:160–164`, `:261–264`, `:1212–1214` | **P1** | `corrigir` | 9 (input/sinais) |
| 9 | `rand()` de GC gerando alertas reais | `:613–617`, `:551–556` | **P1** | `corrigir` | 15 (alertas); decisão em §12 |
| 10 | `.PHONY ncursesw` obsoleto | `Makefile:19` | P2 | `corrigir` | 11 (Makefile/robustez) |
| 11 | `setlocale(LC_ALL, "")` → decimais com vírgula | `:1211` (exports `:647–651`, `:677–681`) | **P0** | `corrigir` | 8 (export), com o item 1 |

Severidades: 2× P0 (1, 11), 5× P1 (3, 5, 7, 8, 9), 4× P2 (2, 4, 6, 10). Status: 10× `corrigir`, 1× `documentar`, 0× `não corrigir`.

---

## 1. `cmdline` sem escape JSON (`jvm_analyzer.c:646`)

- **Linha:** `:646` — `fprintf(f, "  \"uptime_s\": %lu,\n  \"cmdline\": \"%.300s\",\n", ...)`. A origem é `proc_read_cmdline()` em `:436–447`, que troca NUL por espaço, mas não escapa `"` nem `\`.
- **Esperado vs observado:** esperado `"cmdline": "weird \"cmd\\name 30"` (JSON válido e conteúdo integral); observado `"cmdline": "weird "` — a primeira aspa do argv termina a string JSON — e `python3 -m json.tool` falha com `rc=1` (`Expecting ',' delimiter: line 8 column 22`). `LC_ALL=C` foi usado para isolar este defeito do item 11.
- **Comando de reprodução:**
  ```bash
  mkdir -p /tmp/opencode/b5-json
  python3 -c 'import os; os.execv("/bin/sleep", ["weird \"cmd\\name", "30"])' & p=$!
  sleep 0.2; printf 'argv0=%s\n' "$(tr '\0' '|' </proc/$p/cmdline)"
  (sleep 0.2; printf 'eq') | LC_ALL=C timeout --preserve-status -s TERM 5s \
    ./jvma "$p" /tmp/opencode/b5-json >/dev/null 2>/dev/null
  rg -o '"cmdline": "[^"]*"' /tmp/opencode/b5-json/*.json
  python3 -m json.tool /tmp/opencode/b5-json/*.json >/dev/null; echo "json.tool rc=$?"
  kill "$p"
  ```
  Observado: `argv0=weird "cmd\name|30|`, `"cmdline": "weird "`, `json.tool rc=1`.
- **Severidade:** P0 (quebra o contrato JSON para qualquer consumidor; afeta também argv de JVMs reais com `-D...="..."`). **Status:** `corrigir`. **Subtarefa:** 8 — escapar `"`, `\` e controles antes de escrever, mantendo o limite de 300 bytes e o nome do campo; corrigir em conjunto com o item 11.

---

## 2. Notificação de export bloqueante/flicker (`:659–663`, `:686–689`)

- **Linha:** `export_json()` 659–663 e `export_csv()` 686–689: `term_restore()`, `printf()` da notificação, `usleep(800000)`, `term_raw()`.
- **Esperado vs observado:** esperado notificação rápida e não bloqueante, sem perder o frame/sampla; observado `real 1.60` para duas teclas (`e`, `c`) e duas chamadas `clock_nanosleep(CLOCK_REALTIME, 0, {tv_nsec=800000000})` — 0,8 s por export — com `term_restore()`/`term_raw()` limpando a tela (`\033[2J\033[H`) em cada transição. Duplicata funcional do item 7 de `04-performance.md` (aqui registrada pelo impacto visível ao usuário).
- **Comando de reprodução:**
  ```bash
  sleep 30 & p=$!
  printf 'ecq' | /usr/bin/time -p -o /tmp/opencode/b5-bloq.time \
    ./jvma "$p" /tmp/opencode/b5-bloq >/dev/null 2>/dev/null
  cat /tmp/opencode/b5-bloq.time
  strace -f -e trace=clock_nanosleep -o /tmp/opencode/b5-bloq.strace \
    sh -c "printf 'ecq' | ./jvma $p /tmp/opencode/b5-bloq >/dev/null"
  rg clock_nanosleep /tmp/opencode/b5-bloq.strace
  kill "$p"
  ```
  Observado: `real 1.60` e duas linhas `clock_nanosleep(... 800000000 ...)`.
- **Severidade:** P2. **Status:** `corrigir`. **Subtarefa:** 8 — mensagem ANSI idêntica, mas com "flash" por estado/expiração no render, sem sono fixo; nomes de arquivo e ordem de campos inalterados.

---

## 3. Painéis estourando o footer em 70x18, 80x24 e 240x80 (`:1097–1133`)

- **Linha:** guard em `:1097–1103` aceita 70x18 e qualquer tamanho maior, inclusive o padrão 80x24; painéis em uma coluna (w < 110) em `:1123–1126` consomem 2 (header) + 4 (CPU) + 8 (MEMORY) + 7 (THREADS) + 4 (GC/FD) = 25 linhas, deixando `row=25`; em duas colunas (w >= 110, caso de 240x80) `row=17`. `draw_footer()` em `:1132` escreve na linha `g_term_h-1` (17 em 70x18, 23 em 80x24, 79 em 240x80) **depois** dos painéis.
- **Esperado vs observado:** esperado `row <= 14` para `panel_alerts()` com 0 alertas — o helper em `:993–1011` ocupa 3 linhas (header, conteúdo, footer) e deve terminar na linha 16, livre para o footer na 17 — e `row <= 10` com 5 alertas (7 linhas); observado `row=25` tanto em 70x18 quanto em 80x24, com `panel_alerts()` suprimido porque o guard `row < g_term_h-2` em `:1129` é falso (`25 < 16` e `25 < 22`; o caso 80x24 já está registrado em `01-baseline.md:266`). O guard é insuficiente mesmo quando passa: em 70x18 aceita até `row=15`, que empurraria o footer do painel para a linha 17, e não reserva espaço para 5 alertas. Como `draw_footer()` vem depois (`:1132`), é o footer que sobrescreve a linha `Runnable`/GC — não o contrário. Em 240x80 o guard passa (`17 < 78`) e não há estouro; o tamanho entra no escopo como validação de não regressão.
- **Comando de reprodução:**
  ```bash
  gcc -O0 -g -Wall -Wextra -Wno-unused-result -Wno-format-truncation \
    -o /tmp/opencode/jvma-dbg jvm_analyzer.c -lm
  for wh in "70 18" "80 24" "240 80"; do
    set -- $wh
    gdb -q -batch /tmp/opencode/jvma-dbg \
      -ex 'set debuginfod enabled off' \
      -ex 'break render_dashboard' -ex 'run 1 </dev/null' \
      -ex "set g_term_w = $1" -ex "set g_term_h = $2" \
      -ex 'advance jvm_analyzer.c:1129' \
      -ex 'p row' -ex 'p g_term_h-2' -ex 'p row < g_term_h-2'
  done
  ```
  Observado: 70x18 → `$1 = 25`, `$2 = 16`, `$3 = 0`; 80x24 → `$1 = 25`, `$2 = 22`, `$3 = 0`; 240x80 → `$1 = 17`, `$2 = 78`, `$3 = 1`.
- **Severidade:** P1. **Status:** `corrigir`. **Subtarefa:** 10 — cobrir 70x18, 80x24 e 240x80: comprimir/ocultar painéis ou adaptar o layout para garantir `row <= 14` (0 alertas) e `row <= 10` (5 alertas) antes de `draw_footer()`, substituindo o guard insuficiente `row < g_term_h-2` por um que reserve o espaço real do `ALERTS`; em 240x80 manter o layout em duas colunas sem estouro (não regressão). Elevar apenas o mínimo do guard não resolve: 80x24 é o tamanho padrão e já falha.

---

## 4. `%.*s` com precisão negativa em `draw_panel_hdr` (`:739–753`)

- **Linha:** `:740–741` calcula `tlen`; se `w < 8`, `tlen = w - 8` é negativo. `:747` passa esse valor como precisão de `%.*s` (precisão negativa = omitida, imprime a string inteira) e `:750–751` calcula `remaining = w - 6 - tlen` positivo mesmo com `w` pequeno.
- **Esperado vs observado:** esperado cabeçalho contido em `w` colunas (com `w=4`, título truncado/vazio e 4 colunas); observado chamada direta com `w=4` produz `╭── CPU ──╮` (11 colunas: 9 glifos + 2 espaços) e 90 bytes no frame buffer, estourando a largura. Latente no caminho normal (`w >= 70` em `:1097` e `w = 64` em `render_help`, `:1045`), mas o helper é inseguro por contrato.
- **Comando de reprodução:**
  ```bash
  gdb -q -batch ./jvma -ex 'set debuginfod enabled off' \
    -ex 'break main' -ex 'run 1 /tmp/opencode </dev/null' \
    -ex 'call (void)draw_panel_hdr(0,0,4,"CPU")' \
    -ex 'x/s &g_fbuf'
  ```
  Observado (ANSI removido): `╭── CPU ──╮`.
- **Severidade:** P2. **Status:** `corrigir`. **Subtarefa:** 10 — saturar `tlen` em `>= 0` e `remaining` em `>= 0` antes do desenho; manter byte-a-byte o cabeçalho nos tamanhos já suportados.

---

## 5. Underflow unsigned no delta de CPU (`:324`)

- **Linha:** `:324` — `unsigned long long delta = (cur->utime + cur->stime) - (prev->utime + prev->stime)`. O clamp de `:332` (`pct < 0 ? 0.0`) não protege porque `delta` já é unsigned.
- **Esperado vs observado:** esperado delta negativo tratado como `0` (ou leitura consistente do mesmo processo); observado, forçando `cpu_prev.utime` maior que o valor atual, `delta ≈ 2^64`, `cpu_pct = 100` e alerta `CPU critical: 100.0% (threshold 90%)`. Causa plausível em produção: PID reaproveitado, contadores reiniciados ou leitura `racy` de `/proc/<pid>/stat`.
- **Comando de reprodução:**
  ```bash
  gcc -O0 -g -Wall -Wextra -Wno-unused-result -Wno-format-truncation \
    -o /tmp/opencode/jvma-dbg jvm_analyzer.c -lm
  LC_ALL=C timeout 20 gdb -q -batch /tmp/opencode/jvma-dbg \
    -ex 'set debuginfod enabled off' \
    -ex 'break term_raw' -ex 'run 1 /tmp/opencode </dev/null' \
    -ex 'set g_jvm.cpu_prev.utime = 1000000000000' \
    -ex 'set g_jvm.first_sample = 0' \
    -ex 'delete breakpoints' -ex 'break render_dashboard' -ex 'continue' \
    -ex 'p g_jvm.cpu_pct' -ex 'p g_jvm.nalerts' -ex 'p g_jvm.alerts[0].msg'
  ```
  Observado: `$1 = 100`, `$2 = 2`, `$3 = "CPU critical: 100.0% (threshold 90%)"`.
- **Severidade:** P1 (falso crítico de CPU, polui `alerts`, sparkline e exports). **Status:** `corrigir`. **Subtarefa:** 15 — comparar os totais antes de subtrair e usar `0` quando `cur < prev`; manter clamp 0–100 e a normalização por núcleo.

---

## 6. `t->daemon` sempre 0 — verificado (`:508`)

- **Linha:** `:502–506` concatena `/proc/<pid>/task/<tid>/status`; `:508` faz `if (strstr(buf, ": daemon")) t->daemon++;`. O campo é exposto em `:653–655` (JSON) e `:683–684` (CSV).
- **Esperado vs observado:** esperado contagem de threads daemon; **observado: sempre 0**, confirmado por duas verificações: (a) `/proc/<pid>/status` e `/proc/<pid>/task/*/status` não possuem chave `daemon` nem a sequência `": daemon"` (lista de chaves inclui `Name`, `State`, `Threads`, `Kthread`, …, mas não `daemon`); (b) renomeando uma thread para `daemon` via `prctl(PR_SET_NAME)`, o status mostra `Name:\tdaemon` (tab, não espaço) e nenhuma ocorrência de `": daemon"` aparece. Logo `t->daemon++` nunca executa em /proc padrão. Não é erro aritmético: a fonte não fornece o dado (ao contrário de `VirtualThread`, que aparece na linha `:507`).
- **Comando de reprodução:**
  ```bash
  rg -n -i '^daemon|: daemon' /proc/self/status /proc/self/task/*/status \
    || echo 'nenhum campo/ocorrencia ": daemon"'
  python3 - <<'PY'
  import ctypes, glob, os, threading, time
  libc = ctypes.CDLL("libc.so.6", use_errno=True)
  def worker():
      libc.prctl(15, ctypes.c_char_p(b"daemon"), 0, 0, 0); time.sleep(2)
  t = threading.Thread(target=worker); t.start(); time.sleep(0.2)
  pid = os.getpid()
  print("Name:", [l.strip() for l in open(f"/proc/{pid}/status") if l.startswith("Name:")])
  print("': daemon' em task/*/status:",
        sum(": daemon" in l for f in glob.glob(f"/proc/{pid}/task/*/status") for l in open(f)))
  t.join()
  PY
  ```
  Observado: `nenhum campo/ocorrencia ": daemon"`, `Name: ['Name:\tpython3']`, `0`.
- **Severidade:** P2. **Status:** `documentar` — manter o campo no schema (compatibilidade) e registrar que ele é sempre `0` por ausência de fonte em `/proc`; não inventar valor heurístico. **Subtarefa:** 8/15 (documentação do schema na correção de export/proc; ações de código deste subtarefa não são necessárias).

---

## 7. `atoi` permissivo na CLI (`:1218`)

- **Linha:** `:1218` — `if (argc >= 2) pid = atoi(argv[1]);` seguido da validação `pid <= 0` em `:1221–1224`. O menu usa `atoi` em `:1164`/`:1202`, mas o item foca o argumento posicional.
- **Esperado vs observado:** esperado `JVMA: invalid PID '1abc'` e `rc=1` para qualquer PID não numérico; observado `./jvma 1abc` e `./jvma 1x` são aceitos como PID 1 (entram na TUI, `rc=0`), assim como `'  1'`; `0x1` é rejeitado por virar `0`; `99999999999999999999` estoura `int` (comportamento indefinido) e cai na validação. `abc1` é rejeitado corretamente por acaso.
- **Comando de reprodução:**
  ```bash
  for a in 1abc 1x 0x1 '  1' 99999999999999999999 abc1; do
    printf 'arg=[%s] -> ' "$a"
    timeout --preserve-status -s TERM 0.1s ./jvma "$a" </dev/null \
      >/dev/null 2>/tmp/opencode/b5-atoi.err
    printf 'rc=%s stderr=%s\n' "$?" "$(tr '\n' ' ' </tmp/opencode/b5-atoi.err)"
  done
  ```
  Observado: `1abc`, `1x` e `'  1'` com `rc=0` e stderr vazio.
- **Severidade:** P1 (aceita argumento inválido e monitora processo não solicitado; overflow é UB). **Status:** `corrigir`. **Subtarefa:** 9 — usar `strtol` com verificação de `endptr`, faixa `1..INT_MAX` e errno; mensagem de erro idêntica.

---

## 8. `volatile sig_atomic_t` / `sigaction` (`:160–164`, `:261–264`, `:1212–1214`)

- **Linha:** `:160–161` (`static volatile int g_running/g_resize`), handler em `:261–264`, instalação via `signal()` em `:1212–1214`. `g_itvl_ms` e `g_export` em `:162–163` também são compartilhados, mas não são escritos pelo handler.
- **Esperado vs observado:** esperado flags declaradas como `volatile sig_atomic_t` (único tipo com garantia C de acesso atômico em handler) e instalação com `sigaction()` definindo máscara/flags explicitamente; observado `volatile int` (sem garantia de atomicidade) e `signal()`, cuja semântica depende da libc — no glibc o `strace` mostra `sa_flags=SA_RESTORER|SA_RESTART` para SIGINT/SIGTERM/SIGWINCH, com máscara automática do sinal. Isso torna o comportamento de `read()`/`usleep()` sob sinal implícito e não portátil.
- **Comando de reprodução:**
  ```bash
  rg -n 'volatile int|signal\(|sig_atomic_t|sigaction' jvm_analyzer.c
  strace -f -e trace=rt_sigaction -o /tmp/opencode/b5-sig.strace \
    timeout --preserve-status -s TERM 0.1s ./jvma 1 </dev/null >/dev/null 2>/dev/null
  rg 'SIGINT|SIGTERM|SIGWINCH' /tmp/opencode/b5-sig.strace
  ```
  Observado: apenas `volatile int`/`signal()` no código, e três `rt_sigaction(..., sa_flags=SA_RESTORER|SA_RESTART, ...)` no strace.
- **Severidade:** P1 (acesso a dados compartilhados fora do contrato C; diferença entre libcs pode perder resize ou saída). **Status:** `corrigir`. **Subtarefa:** 9 — trocar os dois flags por `volatile sig_atomic_t` e usar `sigaction()` com `sa_flags` explicitamente escolhidos; manter `g_running`/`g_resize` e o encerramento em `done`.

---

## 9. `rand()` de GC gerando alertas reais (`:613–617`, `:551–556`)

- **Linha:** random walk em `:613–617` (`rand() % 100`, limites 1/350, `srand()` em `:1215`, valor inicial `8.5` em `:1254`); limiares em `:551–556` (`TH_GC_W_MS=50`, `TH_GC_C_MS=200`); destino `j->nalerts` em `:623`, exports em `:657`/`:684`.
- **Esperado vs observado:** esperado alerta de GC refletir pausa medida (ou, sem JMX, não entrar no contador de alertas como observação real); observado `gc_pause_ms = 51.4` gerou `nalerts=1` e `GC pause high: 51.4ms (threshold 50ms)` — o mesmo `alerts` do JSON/CSV e o painel `ALERTS` são alimentados por `rand()`, embora a UI mostre `(est)` em `:973`. Com `stdin=/dev/null` (flood) isso ocorre em frações de segundo.
- **Comando de reprodução:**
  ```bash
  gcc -O0 -g -Wall -Wextra -Wno-unused-result -Wno-format-truncation \
    -o /tmp/opencode/jvma-dbg jvm_analyzer.c -lm
  LC_ALL=C gdb -q -batch /tmp/opencode/jvma-dbg \
    -ex 'set debuginfod enabled off' \
    -ex 'break jvm_analyzer.c:557 if j->gc_pause_ms >= 50' \
    -ex 'run 1 </dev/null' \
    -ex 'p j->gc_pause_ms' -ex 'p j->nalerts' -ex 'p j->alerts[0].msg'
  ```
  Observado: `$1 = 51.4`, `$2 = 1`, `$3 = "GC pause high: 51.4ms (threshold 50ms)"`.
- **Severidade:** P1 (falso alerta operacional em produção; `alerts` no export não distingue simulado de medido). **Status:** `corrigir` — pela decisão registrada em §12 (flag de desativação + rótulo explícito), **sem mudar o schema JSON/CSV**. **Subtarefa:** 15 (alertas/proc).

---

## 10. `.PHONY ncursesw` obsoleto (`Makefile:19`)

- **Linha:** `Makefile:19` — `.PHONY: all ncursesw install clean`. Não existe alvo `ncursesw`, receita, `-lncursesw` ou chamada a curses; `jvm_analyzer.c:5` registra "No ncurses required".
- **Esperado vs observado:** esperado `.PHONY` listando somente alvos existentes (`all`, `install`, `clean`); observado `make -n ncursesw` responde "Nada a ser feito para 'ncursesw'." com `rc=0`, isto é, um alvo fantasma que aceita invocação sem efeito e mascara erros de digitação.
- **Comando de reprodução:**
  ```bash
  make -n ncursesw; echo "rc=$?"
  rg -n 'ncurses' Makefile jvm_analyzer.c
  ```
  Observado: "Nada a ser feito para 'ncursesw'.", `rc=0`; única ocorrência em código é o comentário `No ncurses required`.
- **Severidade:** P2. **Status:** `corrigir`. **Subtarefa:** 11 (Makefile/robustez do build) — remover `ncursesw` da linha `.PHONY`; nenhuma mudança de `-Wall -Wextra`, `-lm` ou saída do build. **Justificativa da numeração:** no plano de correções 7–12 a subtarefa 6 é o harness de testes (validação, não implementação — ver §0) e a 11 cuida da robustez do build; como o defeito está em `Makefile:19`, compete à 11, não à 6.

---

## 11. `setlocale(LC_ALL, "")` → decimais com vírgula (`:1211`)

- **Linha:** `:1211` — `setlocale(LC_ALL, "")`; afeta todos os `%.1f`/`%.2f` dos exports em `:647–651` e `:677–681`. Já registrado como defeito do baseline em `01-baseline.md` §4.
- **Esperado vs observado:** esperado JSON e CSV sempre com ponto decimal, independentemente do locale do ambiente; observado, com `LANG=pt_BR.UTF-8`, `"cpu_pct": 0,00` e linha CSV `...,1,0,00,13,0,...` (campos deslocados/ambíguos), `python3 -m json.tool` com `rc=1`. Com `LC_ALL=C`, os mesmos exports saem `0.00` e `json.tool rc=0`.
- **Comando de reprodução:**
  ```bash
  mkdir -p /tmp/opencode/b5-locale
  (sleep 0.3; printf 'ecq') | LANG=pt_BR.UTF-8 LC_ALL= timeout --preserve-status -s TERM 5s \
    ./jvma 1 /tmp/opencode/b5-locale >/dev/null 2>/dev/null
  rg -o '"cpu_pct": [0-9,.]+' /tmp/opencode/b5-locale/*.json
  tail -1 /tmp/opencode/b5-locale/*.csv
  python3 -m json.tool /tmp/opencode/b5-locale/*.json >/dev/null; echo "json.tool rc=$?"
  ```
  Observado: `"cpu_pct": 0,00,`, CSV com `0,00`, `json.tool rc=1`. Repetir com `LC_ALL=C` para o controle (`0.00`, `rc=0`).
- **Severidade:** P0 (JSON inválido e CSV ambíguo em locale padrão do ambiente analisado). **Status:** `corrigir`. **Subtarefa:** 8 — preservar `setlocale(LC_ALL, "")` para a UI/hora, mas fixar `setlocale(LC_NUMERIC, "C")` antes dos exports (ou formatar os números de forma locale-independente); corrigir **em conjunto com o item 1**, mantendo nomes, ordem e tipos de campos.

---

## 12. Decisão explícita: GC simulado por padrão, com flag opcional

**Decisão:** manter o GC simulado (random walk de `:613–617`) como comportamento **padrão** e acrescentar uma forma opcional de desligá-lo: flag de linha de comando `--no-sim-gc` **e** variável de ambiente `JVMA_NO_SIM_GC` (equivalentes no efeito; em conflito, a CLI prevalece). **Nenhuma mudança no schema JSON/CSV**: mesmos nomes, ordem, tipos, precisão, 21 colunas do CSV e `jvma_version = "6.0"`.

**Definição operacional de "ativo" (sem ambiguidade):** na CLI, ativo = `--no-sim-gc` presente em `argv` (em qualquer posição). Na variável, ativo = `getenv("JVMA_NO_SIM_GC") != NULL` **e** valor exatamente `1` (string); presença sem valor, valor vazio, `0` ou qualquer outro texto **não** ativam — nem a presença sozinha, nem apenas um valor não vazio bastam.

**"CLI prevalece" quando a variável pede o oposto:** se `--no-sim-gc` está presente, o GC simulado fica desligado independentemente de `JVMA_NO_SIM_GC` estar ausente, vazia, `0` ou `1` — a variável nunca reativa o que a CLI desligou. Se a flag está ausente, a variável decide (`1` → desligado; demais valores → padrão simulado). Não há `--sim-gc` para forçar o comportamento contrário à variável: a precedência da CLI é unidirecional, sempre no sentido de desligar.

Comportamento especificado para a correção (não implementado nesta subtarefa): com a flag/variável ativa, o random walk de `gc_pause_ms` é desligado, o campo `gc_pause_ms` continua presente com `0.0`, nenhum alerta `AL_GC` é gerado por `:551–556`, e `gc_type` continua sendo detectado em `proc_detect_gc()` (`:449–459`). A UI conserva o sufixo `(est)` em `:973`.

**Justificativa:**

1. O baseline (`01-baseline.md` §3/§4) e a análise de performance (`04-performance.md` §8.1) descrevem o random walk como comportamento conhecido; consumidores de `gc_pause_ms`/`alerts` já dependem desses campos.
2. Não há agente/JMX: o próprio cabeçalho do fonte (`:5`, `:11`) e o painel (`:902`, `:973`) anunciam estimativa via `/proc`, sem JVM agent. Medir pausa real exigiria JMX ou agente e mudaria o contrato de execução ("zero external dependencies").
3. Transformar o valor simulado em "medido" sem nova fonte seria falso; removê-lo quebraria compatibilidade de schema. A flag permite ao operador suprimir o ruído sem alterar nomes/ordem dos campos.
4. A correção do item 9 é, portanto, **separar/rotular** o alerta simulado e oferecer o opt-out — não inventar medição e não mexer no formato dos exports. O item 6 segue a mesma lógica: campo preservado, porém documentado como indisponível.

---

## 13. Checklist de aceite desta análise

- 11 itens obrigatórios cobertos, cada um com linha, comando de reprodução executável, esperado vs observado, severidade P0/P1/P2 e status `corrigir`/`documentar`/`não corrigir`.
- Item 6 verificado antes de afirmar: `daemon` não existe em `/proc/*/status`, inclusive com thread nomeada `daemon`.
- Decisão sobre GC registrada em §12 (padrão simulado + `--no-sim-gc` / `JVMA_NO_SIM_GC`, schema inalterado, justificativa).
- Nenhuma alteração de código: hashes inalterados em relação ao baseline —
  `882224695a1169d22ffe194911f2c66c3817f29d81ed969e8fae6403daf4b5c9  jvm_analyzer.c`,
  `aeb0b9f6b3c7829ddd038d94cb4447f755952e1c47596b50e0877a98d8d62d8e  Makefile`,
  `68bd832d430155522ae0ad0d9dadf29b2ceb46c091eb330fdf0a8f742b68eecb  README.md`.
- Único arquivo criado: `docs/analise/05-bugs-e-decisoes.md`.
