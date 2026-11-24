# Refatoração modular — proposta (JVMA v6.0)

Documento de análise/proposta. Nenhum arquivo de código, `Makefile`, `README.md` ou `docs/analise/01-baseline.md` / `02-mapa-codigo.md` foi alterado.

Referência obrigatória: `docs/analise/02-mapa-codigo.md` (18 seções, 11 globais, mapa de acoplamentos §3 e guias de extensão §4). Os intervalos de linha citados abaixo são os daquele documento.

---

## 1. Módulos propostos (12)

Regra de setas: “Pode incluir” lista apenas módulos internos permitidos; “Pode ser incluído por” fecha o grafo. Includes de libc/POSIX (`stdio.h`, `unistd.h`, `termios.h`, `signal.h`, `dirent.h`, etc.) não alteram o grafo. `SPARK[]` (linha 81) migra para `ring.c` porque só `ring_sparkline()` o usa; deixa de ser constante no módulo de tipos.

| Módulo | Arquivos | Responsabilidade (seções/funções em `02`) | Pode incluir | Pode ser incluído por |
|---|---|---|---|---|
| `src/jvma_types` | `jvma_types.h` (sem `.c`) | §1–2: `JVMA_VERSION`, `HISTORY_LEN`, `MAX_ALERTS`, `MAX_DISCOVERED`, `DEFAULT_ITVL_MS`, `FBUF_SIZE`, `TH_*`, `CL_*`, `ring_t`, `sev_t`, `al_kind_t`, `alert_t`, `thr_t`, `cpusnap_t`, `mem_t`, `jvm_t`, `jvm_entry_t`. Somente declarações/constantes. | Nenhum módulo interno. | `ring`, `util`, `proc`, `alerts`, `sample`, `export`, `render`, `discovery`, `main`, `term` (`gc_sim` não precisa; `term` usa `FBUF_SIZE`). |
| `src/ring` | `ring.c`, `ring.h` | §7: `ring_push()`, `ring_sparkline()` e a tabela `SPARK[]`. Histórico de 60 amostras e conversão para 8 glyphs, do mais antigo ao mais recente. | `jvma_types` | `sample`, `render`. |
| `src/util` | `util.c`, `util.h` | Subconjunto puro de §12: `pct_color()`, `fmt_uptime()`, `fmt_mb()`. Sem `fb_*`, sem estado, sem I/O. | `jvma_types` | `render`. |
| `src/proc` | `proc.c`, `proc.h` | §8 (11 leitores): `proc_read_cpu`, `proc_calc_cpu`, `proc_read_mem_status`, `proc_read_smaps_rollup`, `proc_estimate_heap`, `proc_read_memtotal`, `proc_read_fd`, `proc_read_cmdline`, `proc_detect_gc`, `proc_uptime_secs`, `proc_count_threads`. Nenhum global tocado; escrita por ponteiro e falha `-1` preservadas. | `jvma_types` | `sample`, `discovery`, `main`. |
| `src/alerts` | `alerts.c`, `alerts.h` | §9: `alerts_clear()`, `alert_add()`, `eval_alerts()` (7 regras, dedupe por `kind`, teto `MAX_ALERTS`). | `jvma_types` | `sample`. (`render`/`export` leem campos de `jvm_t` via `jvma_types`, sem incluir `alerts`.) |
| `src/sample` | `sample.c`, `sample.h` | §10: `update_jvm()` — um ciclo de coleta, CPU delta, heap a cada 5 amostras, threads, FDs, uptime, rings, alertas; chama `gc_sim_step()`. | `jvma_types`, `ring`, `proc`, `alerts`, `gc_sim` | `main`. |
| `src/gc_sim` | `gc_sim.c`, `gc_sim.h` | Linhas 613–617: random walk de `gc_pause_ms` e incremento de `gc_events`. API `void gc_sim_step(double *pause_ms, long *events)`. | Nenhum módulo interno (apenas `<stdlib.h>`/`rand`). | `sample`. |
| `src/term` | `term.c`, `term.h` | §4 frame buffer (`fb_*`, `write()` único no fd 1), §5 terminal (`term_get_size`, `term_raw`, `term_restore`, `term_read_key`, `term_read_key_blocking`), §6 sinais (`term_signals_init`) e accessors de estado (`term_should_run`, `term_request_quit`, `term_consume_resize`, `term_width`, `term_height`). Dono de `g_fbuf*`, `g_orig_term`, `g_term_saved`, `g_term_w/h`, `g_running`, `g_resize`. | `jvma_types` (para `FBUF_SIZE`, usado por `g_fbuf` em `jvm_analyzer.c:167`), além de libc/POSIX. | `export`, `render`, `main`. |
| `src/export` | `export.c`, `export.h` | §11: `ensure_export_dir`, `export_json`, `export_csv`; dono de `g_export` com `export_set_dir()` / `export_ensure_dir()`. Notificação e alternância de terminal via `term_restore()`/`term_raw()`. | `jvma_types`, `term` | `main`. |
| `src/render` | `render.c`, `render.h` | §12 restante (helpers que dependem de `fb_*`), §13 painéis/footer, §14 help overlay, §15 master render. `render_dashboard(const jvm_t *j, const int *itvl_ms)` recebe o intervalo por ponteiro (`draw_footer(const jvm_t *j, const int *itvl_ms)` lê `*itvl_ms` em :1018; `handle_key(jvm_t *j, int *itvl_ms, int key)` escreve em :1147/:1149); resize e dimensões vêm de `term`. | `jvma_types`, `ring`, `util`, `term` | `main`. |
| `src/discovery` | `discovery.c`, `discovery.h` | §17: `discover_jvms()` e `show_discovery_menu()`. Varredura de `/proc`, menu por `printf`/`fgets`, auto-seleção com 1 processo. Sem globals. | `jvma_types`, `proc` | `main`. |
| `src/main` | `main.c` (sem `.h`) | §16 `handle_key()` + §18 `main()`: CLI, validação, init de `jvm_t`, loop, sinal de saída, teclas. Dono da instância `jvm_t` (local, passada por ponteiro) e de `itvl_ms` (`int itvl_ms = DEFAULT_ITVL_MS` local); passa `&itvl_ms` a `handle_key()` (escrita) e a `render_dashboard()` (leitura via `draw_footer()`), sem `g_itvl_ms` global. | `jvma_types`, `term`, `proc`, `sample`, `export`, `render`, `discovery` | Ninguém (produz `jvma`). |

### Grafo por camadas (sem volta)

- **N0** — `jvma_types`
- **N1** — `ring`, `util`, `gc_sim`, `term` (`ring`/`util`/`term` → N0 via `jvma_types`; `gc_sim` sem deps internas). `term` → `jvma_types` (`FBUF_SIZE`) é N1→N0: acíclico.
- **N2** — `proc`, `alerts` (→ N0)
- **N3** — `sample` (→ N0/N1/N2), `export` (→ N0/N1), `discovery` (→ N0/N2), `render` (→ N0/N1)
- **N4** — `main` (→ N0/N1/N2/N3)

Nenhum módulo inclui módulo da mesma camada ou de camada superior. Em particular: `term` nunca inclui `render` ou `export`; `export` nunca inclui `render`; `render` nunca inclui `export`, `alerts` ou `proc`; `sample` nunca inclui `main`.

---

## 2. Ordem de extração incremental (subtarefas 13–18)

Cada linha é um passo atômico, com `make clean && make` obrigatório ao final (rc `0`, **stdout e stderr** sem `warning`/`error`, conforme `01-baseline.md:31`) e smoke test do baseline. Se o gate falhar, o passo é revertido antes do próximo.

| Subtarefa | Módulos extraídos | Movimento | Gate de verificação |
|---|---|---|---|
| **13** | `jvma_types`, `ring`, `util`, `gc_sim` | Criar `src/`; mover §1–2, §7, `pct_color`/`fmt_uptime`/`fmt_mb` e linhas 613–617. `jvm_analyzer.c` troca definições por `#include "…"`; `update_jvm()` passa a chamar `gc_sim_step(&j->gc_pause_ms, &j->gc_events)` com as mesmas expressões e ordem de `rand()`. `Makefile`: `CFLAGS += -Isrc`, `SRC = jvm_analyzer.c $(wildcard src/*.c)` **e troca da receita** `$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)` → `$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)` (obrigatória já aqui: `$<` compilaria só `jvm_analyzer.c` e causaria `undefined reference`). | Build limpo; CLI inválida e TUI com `timeout --preserve-status -s TERM 0.2s` iguais a `01-baseline.md` §2/§5. |
| **14** | `proc`, `alerts` | Mover §8 e §9 integralmente. `main`, `update_jvm` e `discover_jvms` incluem os headers. `proc_detect_gc()` continua chamando `proc_read_cmdline()` dentro do mesmo `.c`. `Makefile`: mantém `SRC = jvm_analyzer.c $(wildcard src/*.c)` e a receita com `$(SRC)` da etapa 13 (nunca `$<`). | Build limpo; nenhum `extern` novo; mensagens de PID inexistente inalteradas. |
| **15** | `term`, `sample` | Mover §4–6 e §10 para `src/`. Globais de terminal/sinais passam a `static` em `term.c`; `term_signals_init()` ocupa exatamente o ponto dos 3 `signal()`; `main` e o render ainda no monólito usam `term_should_run()`, `term_consume_resize()`, `term_width()`, `term_height()`. `main` chama `sample_update(&g_jvm)`. `Makefile`: `SRC = jvm_analyzer.c $(wildcard src/*.c)`; receita de link confirmada com `$(SRC)` (troca de `$<` já aplicada na etapa 13). | Build limpo; `SIGTERM` via `timeout --preserve-status` termina com rc `0` e `JVMA v6.0 exited cleanly.`. |
| **16** | `export`, `discovery` | Mover §11 e §17. `g_export` vira estático em `export.c`; `export_set_dir(argv[2])` mantém a posição antiga (antes da validação) e `export_ensure_dir()` continua só após a validação do PID. Menu de descoberta copiado byte a byte, inclusive o `%.*s` de 74 bytes. `Makefile`: `SRC` cobre os novos `.c` via wildcard; receita de link com `$(SRC)`, não `$<`. | Build limpo; comando `abc <DIR>` e `999999 <DIR>` **não** criam diretório; teclas `e`/`c` via pipe geram JSON/CSV idênticos a `01-baseline.md` §4. |
| **17** | `render` | Mover §12 restante, §13–15. `render_help()` usa `term_read_key_blocking()` (o `struct termios` local deixa de aparecer fora de `term.c`) e mantém seu `fb_flush()` próprio. `main` chama `render_dashboard(&g_jvm, &g_itvl_ms)` e `handle_key(&g_jvm, &g_itvl_ms, key)` (na etapa 18, `g_itvl_ms` vira `itvl_ms` local e as chamadas passam `&itvl_ms`). `Makefile`: receita de link com `$(SRC)`, não `$<`. | Build limpo; captura ANSI 80x24 e o gate `70x18` / `panel_alerts` idênticos ao baseline §5. |
| **18** | `main` | Mover §16 e §18 para `src/main.c`; remover `jvm_analyzer.c`. `Makefile` final: `SRC = $(wildcard src/*.c)` e receita `$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)` (troca explícita de `$<` → `$(SRC)`, preservada em todas as etapas), mantendo `-lm`. Nenhuma biblioteca adicional. | `make clean && make` verde; matriz completa de CLI/TUI/JSON/CSV do baseline reexecutada; `git diff` sem alteração de contrato externo. |

### Nota: fixture `tests/fixtures/proc_status.txt` (subtarefas 6 → 15)

`proc_read_mem_status()` ainda hardcoda `/proc/%d/status`, então a fixture fica **reservada** e não é usada na subtarefa 6: o teste chama a função real com `getpid()` contra `/proc/self/status` e confere os valores com `sscanf` independente do mesmo arquivo. Na **etapa 15**, quando os leitores `/proc` forem extraídos e `proc_read_mem_status` aceitar caminho injetável, a fixture passa a ser usada **sem mudar as assertions** — apenas a origem do arquivo (de `/proc/self/status` para `tests/fixtures/proc_status.txt`).

---

## 3. Decisão: zero dependências externas e contrato idêntico

**Decisão explícita:** manter exatamente as dependências atuais — libc/POSIX do sistema e `LDFLAGS = -lm`. Não introduzir ncurses, biblioteca de JSON, parser de argumentos, framework de testes, pthread nem qualquer pacote novo. Únicas mudanças de build previstas são `-Isrc`, a lista de fontes (`SRC`) e a troca da receita de link de `$<` para `$(SRC)` (necessária: `$<` compila só o primeiro pré-requisito e gera `undefined reference`); `-O2 -Wall -Wextra -Wno-unused-result -Wno-format-truncation` permanecem.

**Justificação:** `02-mapa-codigo.md` §8 mostra que todo dado já vem de leitura direta de `/proc`; §4–§5 usam apenas `termios`, `ioctl` e ANSI/VT100; §11 serializa com `fprintf`. Nenhuma dessas tarefas exige lib nova. Adicionar libs aumentaria a superfície de instalação (`install`/`make`) e ameaçaria o build limpo registrado em `01-baseline.md` §1. Manter `-lm` preserva o link histórico mesmo sem uso de funções matemáticas hoje.

**Comportamento externo idêntico — itens que não podem mudar na refatoração:**

- CLI: `./jvma <PID> <DIR>`, `argv` extras ignorados, textos exatos de stderr, `rc` `1` nos erros e `rc` `0` em cancelamento/saída; mensagem `JVMA: Process <PID> exited.` sem `exited cleanly.` quando o processo morre.
- TUI: mesma sequência de `fb_flush()` (flush único por frame, com a exceção do help), cores 256, glyphs, limites `70x18`, layout 1/2 colunas, footer e teto visual de 5 alertas.
- Teclas: `q/Q`, `p/P`, `e/E`, `c/C`, `h/H` (bloqueante, não toggle), `+`/`=` e `-`/`_`, com os mesmos limites 500–10000 ms e o mesmo `usleep` após exports e após `Attaching`.
- JSON/CSV: mesmos nomes de arquivo, ordem de campos, cabeçalho de 21 colunas, precisão, `time(NULL)` separado no CSV e notificações ANSI.
- `setlocale(LC_ALL, "")` continua no início de `main`. O defeito pt_BR (vírgula decimal tornando JSON inválido), documentado em `01-baseline.md` §4, **não** é corrigido aqui: corrigi-lo alteraria a saída e fugiria desta subtarefa.
- `srand((unsigned)time(NULL) ^ (unsigned)getpid())` continua antes de qualquer `gc_sim_step()`, preservando a sequência de `rand()`.

---

## 4. Riscos e mitigação

### 4.1 Destino dos 11 globais (`02` §2)

| Global (`02` §3) | Dono após refatoração | Acesso substituto |
|---|---|---|
| `g_jvm` | `main.c` | `jvm_t jvm = {0}` local; `&jvm` passado a `sample`/`render`/`export`/`handle_key`. |
| `g_running` | `term.c` | `term_should_run()` / `term_request_quit()`. |
| `g_resize` | `term.c` | `term_consume_resize()` (ler e zerar, como nas linhas 1095 e 1286). |
| `g_itvl_ms` | `main.c` | `int itvl_ms = DEFAULT_ITVL_MS` local; `&itvl_ms` passado a `handle_key()` (escrita :1147/:1149) e a `render_dashboard()`/`draw_footer()` (leitura :1018), além do loop interno; sem `static` de arquivo nem global. |
| `g_export` | `export.c` | `export_set_dir()` (startup) e `export_ensure_dir()` (startup + cada export). |
| `g_orig_term` | `term.c` | `term_raw()` / `term_restore()`. |
| `g_term_saved` | `term.c` | `term_raw()` / `term_restore()`. |
| `g_term_w` | `term.c` | `term_width()` / `term_get_size()`. |
| `g_term_h` | `term.c` | `term_height()` / `term_get_size()`. |
| `g_fbuf` | `term.c` | `fb_write()` / `fb_printf()` / `fb_flush()`. |
| `g_fbuf_pos` | `term.c` | `fb_reset()` / `fb_flush()` / `fb_write()`. |

`SPARK[]` é `static const` em `ring.c`; não é estado mutável. Após cada passo, nenhum header deve declarar `extern` de variável: só funções.

### 4.2 Riscos

| Risco | Impacto | Mitigação |
|---|---|---|
| **Globais** espalhados virarem `extern` e criarem acoplamento bidirecional | Ciclos de include e init order frágil | Aplicar a tabela §4.1; `grep` por `static .*g_` após cada passo, conferindo que cada um só aparece no dono; headers expõem apenas funções. |
| **Ordem de inicialização** (setlocale, sinais, srand, `g_export`, baseline de CPU, `term_raw`) | Mensagens de erro, diretório criado cedo demais, delta de CPU errado ou terminal não restaurado | Checklist literal: (1) `setlocale`; (2) `term_signals_init()`; (3) `srand`; (4) `export_set_dir`; (5) validar PID; (6) zerar/preencher `jvm` (`first_sample=1`, `gc_pause_ms=8.5`, `heap_ctr=4`); (7) `export_ensure_dir`; (8) `proc_read_cpu` para `cpu_prev`; (9) `term_get_size`+`term_raw`; (10) loop; (11) `term_restore` em **todas** as saídas, inclusive falha de `sample_update`. |
| **Testes** inexistentes no repositório (sem framework/CI) | Refactor “verde” mas com regressão de bytes na saída | Usar `01-baseline.md` §2/§4/§5 como golden tests: `make clean && make`, PID inválido/inexistente, descoberta 0/1/2 processos, `timeout --preserve-status -s TERM`, pipe `ecq` com `LC_ALL=C` para JSON/CSV, diff de stdout/stderr/rc. Nenhuma lib de teste é adicionada. |
| **Sinais** passarem a chamar código não async-signal-safe | Deadlock ou perda de `SIGTERM`/`SIGWINCH` | `term_signals_init()` continua usando `signal()` (não `sigaction`) e o handler permanece mínimo: só escreve `static volatile int`. Sem `printf`, sem lock, sem acessor com efeito colateral. `term_consume_resize()` reproduz as duas consumições originais (render e main). |
| **Random walk / heap** alterarem sequência | Alertas e métricas mudam sem aviso | `gc_sim_step()` copia as três instruções na ordem exata; `heap_ctr` e leitura a cada 5 amostras ficam em `sample.c`; `srand` não é movido. |
| **Locale e serialização** “corrigidos” por acidente | JSON/CSV deixam de ser idênticos ao baseline | Manter `setlocale(LC_ALL, "")` e os `fprintf` originais; revisar diff das linhas de formato em cada passo. |
| **Frame buffer/help** com dois flushes | Intercalação com `printf` muda | `render_help()` conserva `fb_flush()` próprio e `term_read_key_blocking()` com `VMIN=1`/`VTIME=0` restaurando os atributos locais; não unificar com o flush do dashboard. |

---

## 5. Correspondência com `docs/analise/02-mapa-codigo.md`

- §1 (45–82) e §2 (83–155) → `jvma_types`; exceção: `SPARK[]` (81) → `ring`.
- §3 GLOBALS (156–170) e §3.2 itens 1–9 → tabela de proprietários em §4.1 deste documento; nenhum global é eliminado, apenas realocado.
- §3.1: `fb_*` 171–219, terminal 220–257 e sinais 258–265 → `term`; ring 266–297 → `ring`; readers 298–517 → `proc` (acoplamento interno `proc_detect_gc` → `proc_read_cmdline` permanece); alertas 518–587 → `alerts`; `update_jvm` 588–626 → `sample`; export 627–691 → `export`.
- §3.1: helpers 692–783 → `util` (`pct_color`, `fmt_uptime`, `fmt_mb`) + `render` (helpers com `fb_*`); painéis 784–1028, help 1029–1090 e master render 1091–1135 → `render`; input 1136–1153 e main 1207–1294 → `main`; descoberta 1154–1206 → `discovery`.
- §3.2 item 3 (delta de CPU) e item 4 (histórico) permanecem em `jvm_t`/`sample`/`ring`; item 5 (alertas) continua reconstruído por `eval_alerts()` e lido por `panel_alerts()`/`export_json()` via `jvm_t`; itens 6–9 (terminal, diretório, frame buffer, locale) são tratados em §3 e §4 deste documento.
- Guias §4.1–§4.4 do mapa continuam válidos após a extração, com os pontos de mudança agora distribuídos por `types → proc → sample → export/render` (nova métrica), `types → alerts → render/help` (novo alerta), `render → main` (novo painel/atalho) e `export → main` (novo formato). O documento 02 permanece como referência imutável.

---

## 6. Checklist de aceite desta proposta

- Tabela §1 cobre os **12** módulos com arquivos `.c`/`.h`, responsabilidade e direção de includes; camadas N0→N4 sem ciclos.
- **Nenhuma** biblioteca nova: apenas `-lm` e `-Isrc` no build.
- Plano §2 tem 6 etapas (13–18) com gate `make clean && make` verde em cada uma.
- Comportamento externo (CLI, TUI, JSON/CSV) preservado conforme §3; nenhuma correção de bug de locale nesta etapa.
- Escopo desta subtarefa restrito à criação deste arquivo; código, `Makefile`, `README.md` e docs 01/02 intactos.
