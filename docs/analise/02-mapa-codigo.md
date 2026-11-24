# Mapa de código — JVMA v6.0 (`jvm_analyzer.c`, linhas 45–1294)

Documento de análise estática. Intervalos inclusive, extraídos dos marcadores `/* === */` do próprio arquivo.
Linhas 1–44 (cabeçalho e `#include`) ficam fora do intervalo mapeado. Nenhum arquivo de código foi alterado.
Referência: `docs/analise/01-baseline.md`.

---

## 1. As 18 seções lógicas

| # | Seção | Intervalo | Responsabilidade |
|---|---|---|---|
| 1 | VERSION/CONSTANTS | 45–82 | Versão, tamanhos (`HISTORY_LEN`, `MAX_ALERTS`, `MAX_DISCOVERED`, `DEFAULT_ITVL_MS`, `FBUF_SIZE`), limiares `TH_*`, paleta `CL_*` (68–78) e glyphs `SPARK[]` (81). |
| 2 | DATA STRUCTURES | 83–155 | `ring_t`, `sev_t`, `al_kind_t`, `alert_t`, `thr_t`, `cpusnap_t`, `mem_t`, `jvm_t` (estado central, 122–147) e `jvm_entry_t` (descoberta). |
| 3 | GLOBALS | 156–170 | Onze variáveis `static` de arquivo: estado do JVM, flags de loop/sinal, intervalo, diretório de export, terminal e frame buffer. |
| 4 | FRAME BUFFER | 171–219 | Acumulação em `g_fbuf` e `write()` único em `STDOUT_FILENO`; helpers de cursor, cor, atributos e texto posicionado. |
| 5 | TERMINAL MANAGEMENT | 220–257 | Dimensões via `ioctl(TIOCGWINSZ)`, modo raw, restauração e leitura de uma tecla com timeout de 100 ms. |
| 6 | SIGNALS | 258–265 | `SIGINT`/`SIGTERM` → `g_running = 0`; `SIGWINCH` → `g_resize = 1`. |
| 7 | RING/SPARKLINE | 266–297 | Buffer circular de 60 amostras e conversão para 8 níveis de bloco (`SPARK[]`), do mais antigo ao mais recente. |
| 8 | /PROC READERS | 298–517 | Leitura de CPU (`stat` + delta), memória (`status`, `smaps_rollup`, `smaps`, `meminfo`), FDs, `cmdline`, detecção de GC, uptime e contagem de threads. Nenhum global é tocado aqui. |
| 9 | ALERT SYSTEM | 518–587 | Limpeza, inserção com dedupe por `kind` e avaliação das 7 regras (CPU, GC, threads, zumbis, bloqueadas, FDs, swap). |
| 10 | STATE UPDATE | 588–626 | Um ciclo de coleta: CPU, memória (heap a cada 5 amostras), threads, FDs, uptime, random walk de `gc_pause_ms`, rings e alertas. |
| 11 | EXPORT | 627–691 | `ensure_export_dir()`, snapshot JSON e append CSV, com notificação via stdio e alternância de modo terminal. |
| 12 | DRAWING HELPERS | 692–783 | Cor por percentual, barras de progresso, linha/molduras e formatação de uptime e MB/GB. |
| 13 | DASHBOARD PANELS | 784–1028 | `panel_header_bar`, CPU, MEMORY, THREADS, GC & FILE DESCRIPTORS, ALERTS e footer; cada painel devolve a próxima linha livre. |
| 14 | HELP SCREEN | 1029–1090 | Overlay 64x25 com teclas e limiares; `fb_flush()` próprio (1080) e bloqueio até uma tecla (1082–1088). |
| 15 | MASTER RENDER | 1091–1135 | Consome resize, valida 70x18, layout de uma ou duas colunas, `panel_alerts()` condicional e flush final. |
| 16 | INPUT HANDLER | 1136–1153 | Dispatch de `q/Q`, `p/P`, `e/E`, `c/C`, `h/H`, `+`/`=` e `-`/`_`. |
| 17 | PROCESS DISCOVERY | 1154–1206 | Varredura de `/proc` por `exe` = `java`/`javaw`, menu interativo e auto-seleção com um único processo. |
| 18 | MAIN | 1207–1294 | `setlocale(LC_ALL, "")` (1211), sinais, CLI, descoberta, validação, init de `g_jvm`, terminal e loop de `g_itvl_ms` com render/input. |

---

## 2. Variáveis globais (todas)

| Global | Tipo | Definição | Quem lê | Quem escreve |
|---|---|---|---|---|
| `g_jvm` | `jvm_t` | 159 | `main()` 1269–1286 (passa `&g_jvm`); `update_jvm()` 591–625; `eval_alerts()` 542–586; `handle_key()` 1139–1152; `render_dashboard()` 1094–1134 e painéis 788–1027; `export_json()` 635–664 e `export_csv()` 666–690 | `main()` 1251–1262 (`memset`, `pid`, `first_sample`, `gc_pause_ms`, `heap_ctr`, `cmdline`, `gc_type`, `mem`, `cpu_prev`); `update_jvm()` 594–623 via ponteiro (CPU, memória, threads, FDs, uptime, GC, rings, alertas); `handle_key()` 1142 (`paused`) |
| `g_running` | `volatile int` | 160 | `main()` 1268 (`while`) e 1280 (`while` interno) | inicialização 160; `sig_handler()` 262 (`= 0`); `handle_key()` 1141 (`= 0`) |
| `g_resize` | `volatile int` | 161 | `render_dashboard()` 1095; `main()` 1286 | inicialização 161; `sig_handler()` 263 (`= 1`); `render_dashboard()` 1095 (`= 0`); `main()` 1286 (`= 0`) |
| `g_itvl_ms` | `int` | 162 | `draw_footer()` 1018 (`/1000`); `main()` 1280 (limite do loop interno) | inicialização 162 (`DEFAULT_ITVL_MS`); `handle_key()` 1147 (`-500`, mín. 500) e 1149 (`+500`, máx. 10000) |
| `g_export` | `char[256]` | 163 | `ensure_export_dir()` 632; `export_json()` 639; `export_csv()` 669 | inicialização 163 (`"./exports"`); `main()` 1219 (`strncpy` de `argv[2]`) |
| `g_orig_term` | `struct termios` | 164 | `term_restore()` 246 (`tcsetattr`) | `term_raw()` 233 (`tcgetattr`) — permanece indefinida até a primeira chamada |
| `g_term_w` | `int` | 165 | `panel_header_bar()` 798, 803, 814; `draw_footer()` 1022; `render_help()` 1034; `render_dashboard()` 1097, 1100, 1111–1126, 1130 | inicialização 165 (80); `term_get_size()` 226 (`ws_col`, fallback 80) |
| `g_term_h` | `int` | 166 | `draw_footer()` 1015, 1022; `render_help()` 1034, 1039, 1047; `render_dashboard()` 1097, 1100, 1129 | inicialização 166 (24); `term_get_size()` 227 (`ws_row`, fallback 24) |
| `g_fbuf` | `char[FBUF_SIZE]` | 167 | `fb_flush()` 177 (`write()`) | `fb_write()` 184 (`memcpy`); indireto via `fb_printf()` e todos os `fb_*`/painéis |
| `g_fbuf_pos` | `int` | 168 | `fb_flush()` 176; `fb_write()` 183 | inicialização 168; `fb_reset()` 174; `fb_flush()` 178; `fb_write()` 185 |
| `g_term_saved` | `int` | 169 | `term_restore()` 245 | inicialização 169 (0); `term_raw()` 234 (`= 1`) |

Observações: não há outras variáveis com escopo de arquivo além destas onze; `SPARK[]` (81) é `static const` e atua como tabela imutável. `g_running` e `g_resize` são as únicas marcadas `volatile`, justamente por serem escritas de `sig_handler()`.

---

## 3. Mapa de acoplamentos

### 3.1 Função → estruturas e estado

| Função (linhas) | Estruturas | Globais lidos | Globais escritos / estado compartilhado |
|---|---|---|---|
| `fb_reset` 174 | — | — | escreve `g_fbuf_pos` |
| `fb_flush` 175–180 | — | `g_fbuf`, `g_fbuf_pos` | escreve `g_fbuf_pos`; `write()` direto no fd 1 |
| `fb_write` 181–187 | — | `g_fbuf`, `g_fbuf_pos` | escreve `g_fbuf`, `g_fbuf_pos`; descarta excedente em `FBUF_SIZE - 2` |
| `fb_printf` 188–194; `fb_move`/`fb_fg`/`fb_bg`/`fb_bold`/`fb_reset_attr`/`fb_eol` 197–202; `fb_ctext`/`fb_cprintf` 205–218 | — | via `fb_write` | via `fb_write`; base de renderização de todos os painéis |
| `term_get_size` 223–229 | `struct winsize` | — | escreve `g_term_w`, `g_term_h` |
| `term_raw` 231–242 | `struct termios` | — | escreve `g_orig_term`, `g_term_saved`; esconde cursor e limpa tela |
| `term_restore` 244–249 | `struct termios` | `g_orig_term`, `g_term_saved` | chamado também por `export_json`/`export_csv` e no desligamento |
| `term_read_key` 252–256 | — | — | `read(STDIN_FILENO, ...)`, nenhum global |
| `sig_handler` 261–264 | — | — | escreve `g_running`, `g_resize` (único código assíncrono) |
| `ring_push` 269–273 / `ring_sparkline` 275–296 | `ring_t`, `SPARK[]` 81 | `ring_t` por ponteiro, `SPARK[]` | `ring_t`; chamado por `update_jvm` (619–621) e painéis (831, 893, 952) |
| Readers 301–516 (`proc_read_cpu`, `proc_calc_cpu`, `proc_read_mem_status`, `proc_read_smaps_rollup`, `proc_estimate_heap`, `proc_read_memtotal`, `proc_read_fd`, `proc_read_cmdline`, `proc_detect_gc`, `proc_uptime_secs`, `proc_count_threads`) | `cpusnap_t`, `mem_t`, `thr_t` | nenhum global | preenchem estruturas passadas por ponteiro; `proc_detect_gc` reusa `proc_read_cmdline` (451) |
| `alerts_clear` 521 / `alert_add` 523–540 | `jvm_t`, `alert_t`, `MAX_ALERTS` 50 | `j->nalerts`, `j->alerts[]` | `j->nalerts`, `j->alerts[]`; dedupe por `kind` (526–532) |
| `eval_alerts` 542–586 | `jvm_t`, `alert_t`, `TH_*` 55–65 | `j->cpu_pct`, `j->gc_pause_ms`, `j->thr`, `j->fd_count`, `j->mem.swap_kb` | `j->alerts[]`, `j->nalerts`; reinicia o conjunto a cada amostra (543) |
| `update_jvm` 591–625 | `cpusnap_t`, `mem_t`, `thr_t`, `ring_t`, `jvm_t` | todos os campos de `j`; `rand()` | campos de `j`, rings e alertas; nó central de mutação de estado |
| `ensure_export_dir` 630–633 | — | `g_export` | `stat()` + `mkdir(..., 0755)` |
| `export_json` 635–664 / `export_csv` 666–690 | `jvm_t` | `g_export`, `JVMA_VERSION`, campos de `j` | ligam/desligam o terminal via `term_restore`/`term_raw` (660–663, 686–689) |
| `pct_color` 697–701; `draw_bar` 706–728; `draw_hline` 731–736; `draw_panel_hdr` 739–754; `draw_panel_ftr` 757–764; `draw_sides` 767–770; `fmt_uptime` 772–777; `fmt_mb` 779–782 | — | `CL_*` 68–78 | dependem apenas dos `fb_*`; reutilizáveis sem alteração |
| `panel_header_bar` 788–819 | `jvm_t` | `j->uptime_s`, `j->pid`, `j->gc_type`, `j->cmdline`, `g_term_w`, `JVMA_VERSION` | usa `fmt_uptime` |
| `panel_cpu` 821–841 | `jvm_t`, `ring_t` | `j->cpu_pct`, `j->h_cpu`, `TH_CPU_W/C` | usa `ring_sparkline`, `draw_bar`, `pct_color` |
| `panel_memory` 843–907 | `jvm_t`, `ring_t` | `j->mem.*`, `j->h_rss` | usa `fmt_mb`; limites de barra literais 40/65 e 70/90 |
| `panel_threads` 909–960 | `jvm_t`, `ring_t` | `j->thr.*`, `j->h_threads`, `TH_THR_*`, `TH_ZMB_*` | — |
| `panel_gc_fd` 962–991 | `jvm_t` | `j->gc_type`, `j->gc_pause_ms`, `j->fd_count`, `j->fd_limit`, `TH_GC_*`, `TH_FD_*` | exibe `(access denied ...)` quando `!(fd_count >= 0 && fd_limit > 0)` (inclusive quando `fd_limit <= 0`) |
| `panel_alerts` 993–1012 | `jvm_t`, `alert_t` | `j->nalerts`, `j->alerts[]` | mostra no máximo 5 alertas (`i < 5`, 1000) |
| `draw_footer` 1014–1027 | `jvm_t` | `j->paused`, `g_itvl_ms`, `g_term_w`, `g_term_h` | — |
| `render_help` 1032–1089 | `struct termios` local | `g_term_w`, `g_term_h`, `JVMA_VERSION`; limiares literais 1069–1073 | `fb_flush()` próprio (1080) e `tcsetattr` local (1082–1088); não toca `g_orig_term` |
| `render_dashboard` 1094–1134 | `jvm_t` | `g_resize`, `g_term_w`, `g_term_h`, `j` | escreve `g_resize` (1095, via `term_get_size` também atualiza dimensões); chama todos os painéis e `fb_flush` |
| `handle_key` 1139–1152 | `jvm_t` | `j->paused` | escreve `g_running` (1141), `g_itvl_ms` (1147, 1149), `j->paused` (1142); chama exports e help |
| `discover_jvms` 1157–1184 | `jvm_entry_t` | — | preenche `out[]`; reutiliza `proc_read_cmdline`, `proc_detect_gc`, `proc_count_threads` |
| `show_discovery_menu` 1186–1205 | `jvm_entry_t` | `list[]` | `printf`/`fgets` diretos, sem globals |
| `main` 1210–1294 | `jvm_t`, `jvm_entry_t`, `cpusnap_t` | `argv`, `g_running`, `g_itvl_ms`, `g_resize`, `g_jvm` | escreve `g_export` (1219), `g_jvm` (1251–1262), `g_resize` (1286); `g_itvl_ms` e `g_running` também via `handle_key` |

### 3.2 Estado compartilhado

1. **`g_jvm` é o barramento único** entre coleta (`update_jvm`), render (`render_dashboard` → painéis) e export. Trafega por ponteiro; não há locks porque tudo é single-thread.
2. **Sinais**: `sig_handler` escreve apenas os `volatile int` (`g_running`, `g_resize`). A flag de resize é consumida e zerada em dois pontos: `render_dashboard()` 1095 e `main()` 1286.
3. **Delta de CPU**: `cpu_prev`/`cpu_cur` e `first_sample` (128–130) persistem entre iterações em 594–597; o primeiro sample é suprimido.
4. **Histórico**: `h_cpu`, `h_rss`, `h_threads` (144) são escritos em 619–621 e lidos pelos sparklines em 831, 893 e 952.
5. **Alertas**: `alerts[]`/`nalerts` (141–142) são reconstruídos por `eval_alerts()` a cada amostra; lidos por `panel_alerts()` 995–1006 e por `export_json()` 657 (apenas a contagem). Teto visual de 5 em 1000; capacidade 24 em 50.
6. **Terminal**: `g_orig_term`/`g_term_saved` são alternados por `term_raw`/`term_restore`; `export_*` e `render_help` mudam o modo no meio do ciclo, enquanto `fb_flush` usa o fd 1 e as notificações usam `printf` (stdio) — origem da intercalação observada no baseline §2.
7. **Diretório de export**: `g_export` é definido em 1219 e lido por `ensure_export_dir()`, `export_json()` e `export_csv()`; criado no startup (1259) e novamente antes de cada export (636, 667).
8. **Frame buffer**: `g_fbuf`/`g_fbuf_pos` são compartilhados por todos os `fb_*`. Normalmente o flush acontece só no `render_dashboard()` 1133, mas `render_help()` 1080 faz flush próprio.
9. **Locale**: `setlocale(LC_ALL, "")` em 1211 afeta todo `fprintf`/`fb_printf` com ponto flutuante — achado registrado no baseline §4 (vírgula decimal em pt_BR quebra JSON/CSV).

---

## 4. Guias de extensão

Regra aplicada a todos: cada guia lista no máximo 5 locais de mudança, com linha de referência. Helpers de `fb_*` (171–219) e de desenho (692–783) são reutilizados e não precisam ser alterados.

### 4.1 Nova métrica lida do `/proc` (5 locais)

1. **Estrutura de destino** — `DATA STRUCTURES` 87–154: acrescentar campo em `mem_t` (115–120), `thr_t` (105–108) ou `jvm_t` (122–147); para nova série histórica, usar `ring_t` (87–90).
2. **Leitor** — `/PROC READERS` 298–517: novo `proc_read_xyz(int pid, ...)`, seguindo o padrão de falha de 305/339 (`return -1`) e o de escrita por ponteiro de 411–434.
3. **Coleta** — `update_jvm()` 591–625: chamar o leitor em 599–611 e, se aplicável, `ring_push()` em 619–621.
4. **JSON** — `export_json()` 644–657: incluir o novo campo no objeto serializado.
5. **CSV** — `export_csv()` 673–684: acrescentar coluna ao cabeçalho (674–676) e valor à linha (677–684), preservando a ordem existente.

> Exibição na TUI, se desejada, segue o guia 4.3. Decimais exportados herdam o locale de 1211 (ver baseline §4).

### 4.2 Novo alerta (5 locais)

1. **Limiares** — 55–65: novas constantes `TH_*` (ou literal documentado, como `> 5` em 572).
2. **Identidade do alerta** — `al_kind_t` 93–96: novo valor do enum; `alert_add()` 523–540 já faz dedupe por `kind` e não precisa mudar.
3. **Regra** — `eval_alerts()` 542–586: novo bloco `if/else if` com `alert_add(...)` nas severidades `SEV_CRIT`/`SEV_WARN`.
4. **Visibilidade** — `panel_alerts()` 993–1012: avaliar o teto de 5 linhas visíveis em 1000 se o número de alertas crescer.
5. **Ajuda** — `render_help()` 1063–1073: espelhar o limiar com linha `TH(...)` (textos são literais, não interpolam `TH_*`).

> O campo `alerts` do JSON (657) é apenas `nalerts` e se atualiza sozinho; não requer alteração.

### 4.3 Novo painel TUI (5 locais)

1. **Função do painel** — `DASHBOARD PANELS` 784–1028: novo `static int panel_x(const jvm_t *j, int row, int col, int w)`, inserido antes de `draw_footer()` (1014), retornando a próxima linha livre como em 840/906/959/990/1011.
2. **Layout e orçamento vertical** — `render_dashboard()` 1094–1134: chamar nos dois caminhos (1117–1121 e 1123–1126), atualizando `row`/`lr`/`rr`; respeitar o mínimo 70x18 (1097–1103), o gate de ALERTS `row < g_term_h - 2` (1129–1130) e o footer fixo em `g_term_h - 1`.
3. **Atalho** — `handle_key()` 1139–1152: novo `case` se o painel tiver tecla própria.
4. **Rodapé** — `draw_footer()` 1014–1027: incluir o rótulo da nova tecla em 1017.
5. **Ajuda** — `render_help()` 1051–1061: incluir a nova tecla em uma linha `HL(...)`.

### 4.4 Novo formato de export (4 locais)

1. **Função de export** — `EXPORT` 627–691: novo `export_<fmt>(const jvm_t *j)` após `export_csv()` (690), reaproveitando `ensure_export_dir()` 630–633 e `g_export` (163, 1219) sem alteração; padrão de notificação em 660–663 e 686–689.
2. **Dispatch** — `handle_key()` 1139–1152: novo `case` de tecla chamando a função.
3. **Rodapé** — `draw_footer()` 1014–1027: adicionar o rótulo da tecla em 1017.
4. **Ajuda** — `render_help()` 1051–1061: adicionar linha `HL(...)` descrevendo o formato.

> Atenção: qualquer decimal escrito pelo novo formato segue o locale definido em 1211 (defeito pt_BR documentado no baseline §4).
