# Performance — JVMA v6.0 (análise, código inalterado)

Documento de análise da subtarefa 4. Nenhuma correção foi aplicada: `jvm_analyzer.c`, `Makefile`, `README.md` e `docs/analise/01-baseline.md` / `02-mapa-codigo.md` / `03-refatoracao-modularidade.md` permanecem intocados.

Referências: `01-baseline.md` §1/§5 (build e captura com `stdin=/dev/null`), `02-mapa-codigo.md` §3.1/§3.2 (funções, globais, acoplamentos) e `03-refatoracao-modularidade.md` §1/§2 (módulos donos e etapas de extração).

Metodologia das medições (2026-11-24, mesmo ambiente do baseline, `nproc=12`, `CLK_TCK=100`): binário `jvma` do baseline, `timeout --preserve-status -s TERM`, stdout redirecionado para `/dev/null` ou `/tmp/opencode/`; contagens de syscall com `strace -f`. Números são ordens de grandeza — variam com carga, número de threads e tamanho do terminal.

**Numeração das subtarefas:** `9` = correção de `input`, `8` = correção de `export`, `10` = correção de `layout`/buffer, `15` = correção de `proc`; `6` é a subtarefa de **testes** e nenhum item desta análise é atribuído a ela (ela valida as correções, não as implementa). As etapas de extração citadas no texto seguem `03-refatoracao-modularidade.md` §2 (`13` = `ring`/`types`, `14` = `proc`/`alerts`, `15` = `term`/`sample`, `16` = `export`/`discovery`, `17` = `render`, `18` = `main`) e **não** são a subtarefa de correção — a etapa 15 também é a subtarefa de correção de `proc`. A correção deve ser aplicada no módulo indicado quando a extração já tiver ocorrido.

---

## 0. Resumo executivo

| ID | Ponto de performance | Linha(s) | Impacto | Sev. | Subtarefa |
|---|---|---|---|---|---|
| 1 | `read()`/VTIME com EOF → busy-wait e frame flood | 231–239, 251–256, 1279–1287 | CPU e saída saturados quando `stdin=/dev/null` | **P0** | **9** |
| 2 | `proc_count_threads()` abre `task/*/status` por thread a cada amostra | 485–516 (chamada em 609) | `N` `open()`+`fclose()`+leitura por amostra | **P1** | **15** |
| 3 | `discover_jvms()` conta threads de todos os PIDs e lê `cmdline` duas vezes | 1157–1184 (1175–1178) | `M×T` aberturas no startup | **P1** | **15** |
| 4 | `fb_write()` com `strlen`+`memcpy` por fragmento; desenhos caractere a caractere | 181–187; 717, 721, 734, 746, 749, 751, 761, 798, 1041 | 60–100 mil chamadas/s no flood | **P1** | **10** |
| 5 | `strcat()` repetido em `ring_sparkline()` | 275–296 (294) | varredura O(n²) do prefixo, 3× por frame | **P2** | **10** |
| 6 | Buffer fixo de 128 KB sem descarte medido | 53, 167, 183–186 | ~9,4 KB/frame em 240×80 cabem no buffer | **P2** | **10** |
| 7 | `usleep(800000)` por export | 662, 688 | +0,8 s de bloqueio por tecla `e`/`c` | **P2** | **8** |
| 8a | Custo por amostra de `update_jvm()` | 591–625 | ≈ `10+N` aberturas por ciclo | **P1** | **15** |
| 8b | `sysconf()` dentro de `proc_calc_cpu()` | 322–333 (328–329) | 2 chamadas por amostra, valor constante | **P2** | **15** |
| 8c | `/proc/<pid>/smaps` a cada 5 ciclos | 602–607; 365–397 | 0,3–4 MB e 2–30 ms por varredura | **P1** | **15** |

---

## 1. Loop de teclas com `read()`/VTIME — busy-wait / frame flood

- **Local:** `term_raw()` configura `VMIN=0`, `VTIME=1` em 237–238; `term_read_key()` chama `read(STDIN_FILENO, &ch, 1)` em 251–256 e devolve `-1` tanto para "sem tecla" quanto para EOF; `main()` usa `elapsed += 100` em 1280–1287 sem dormir. Com `/dev/null` (ou EOF), `read()` retorna `0` imediatamente: os 20 passos de 100 ms de um intervalo de 2 s ocorrem em microssegundos e o `while` externo renderiza sem pausa.
- **Impacto:** ~100% de um núcleo em CPU e fluxo contínuo de ANSI no stdout; menu de descoberta sem TTY gera centenas de MB em poucos segundos (`01-baseline.md` §2/§5). Sinais continuam funcionando, mas a janela de processamento é dominada pelo flood.
- **Comando de reprodução:**
  ```bash
  sleep 30 & p=$!
  timeout --preserve-status -s TERM 0.2s ./jvma "$p" </dev/null \
    > /tmp/opencode/perf-eof.out 2>/tmp/opencode/perf-eof.err; echo $?   # 0
  wc -c /tmp/opencode/perf-eof.out /tmp/opencode/perf-eof.err
  strace -f -c -e trace=read,write,openat,close,getdents64 \
    -o /tmp/opencode/perf-eof.strace \
    timeout --preserve-status -s TERM 0.2s ./jvma "$p" </dev/null >/dev/null 2>/dev/null
  kill "$p"
  ```
- **Estimativa de custo:** 2.606.760 bytes em 0,2 s nesta execução (≈13 MB/s; baseline registrou 1.122.020 bytes no mesmo cenário). Sob `strace`: 1.581 `read()` (≈7,9 mil/s) e 42 `write()` (um por frame) em 0,2 s; sem `strace` a taxa é maior. Cada iteração desperdiça uma syscall e reexecuta o ciclo de amostragem completo.
- **Correção proposta:** não usar só o retorno de `read()` para separar "sem tecla" de EOF — com `VMIN=0`/`VTIME=1`, **timeout e EOF devolvem `0`**. Detectar `stdin` sem TTY com `isatty(STDIN_FILENO)` (ou tratar o fd explicitamente) e, nesse caso, fazer o `main()` esperar 100 ms por iteração com `clock_nanosleep()`/`poll()`, em vez de girar o loop; com TTY, manter `read()`/`VTIME` e usar `poll()` (`POLLIN`/`POLLHUP`) para distinguir ausência de tecla de EOF. Preservar `g_running`, o resize e o intervalo; manter `handle_key()` e o desenho idênticos.
- **Severidade:** P0. **Subtarefa:** 9 (correção de `input`; implementação em `src/term` e no loop de `src/main`, etapas de extração 15/18).

---

## 2. `proc_count_threads()` abre `/proc/[pid]/task/*/status` para cada thread a cada amostra

- **Local:** `proc_count_threads()` em 485–516: `opendir` do `task` (488), um `fopen()` por thread (497), `fgets`/`strncat` de até 2 KB (499–505) e quatro `strstr()` (507–512); chamada incondicional em `update_jvm()` 609.
- **Impacto:** custo linearmente proporcional ao número de threads `N`; em JVM com centenas de threads é a maior fonte de `open()`/`close()` do ciclo e mantém `/proc` constantemente quente.
- **Comando de reprodução:**
  ```bash
  sleep 30 & p=$!
  ls /proc/"$p"/task | wc -l
  strace -f -e trace=openat -o /tmp/opencode/perf-threads.open \
    timeout --preserve-status -s TERM 0.2s ./jvma "$p" </dev/null >/dev/null 2>/dev/null
  rg -c '/task/[0-9]+/status' /tmp/opencode/perf-threads.open
  rg -o '/task/[0-9]+/status' /tmp/opencode/perf-threads.open | sort -u | wc -l
  kill "$p"
  ```
- **Estimativa de custo:** com 1 thread, 29 aberturas do **mesmo** `status` em 0,2 s (≈145/s) e 1 arquivo único — evidência de reabertura por frame/amostra. Por amostra: `N` `open()` + `N` `close()` + no máximo `N × ~2 KB` lidos (`buf[2048]`, `len < sizeof(buf)-256`, em 499–505); `N=500` ≈ 500 aberturas/amostra (≈250/s a 2 s, ≈1.000/s a 500 ms) e ≤ ~1 MB de `/proc` lidos, não 1–4 MB.
- **Correção proposta:** `opendir`/`readdir` continua barato; mover a leitura dos `status` para cadência menor (ex.: a cada 5 amostras, como o heap) ou ler `/proc/<pid>/status` (`Threads:`) para o total e detalhar apenas threads novas/mudadas; manter `thr_t` e os limiares de alerta inalterados entre atualizações (histerese). Aplicar em `src/proc` e orquestrar a cadência em `src/sample`.
- **Severidade:** P1. **Subtarefa:** 15 (correção de `proc`; consumo orquestrado em `src/sample`, etapa de extração 15).

---

## 3. `discover_jvms()` chama `proc_count_threads()` para todos os PIDs

- **Local:** loop em 1157–1184; para cada `exe` com basename `java`/`javaw`: `proc_read_cmdline()` 1175, `proc_detect_gc()` 1176 (que reabre `cmdline` em 451) e `proc_count_threads()` 1178 **antes** da seleção do usuário.
- **Impacto:** o custo é pago por todos os JVMs descobertos, inclusive quando o usuário cancela o menu; `cmdline` é lido duas vezes por candidato.
- **Comando de reprodução:**
  ```bash
  # pré-condição: M processos com exe basename java/javaw (ver 01-baseline.md §2)
  /tmp/opencode/java & j1=$!; /tmp/opencode/javaw & j2=$!
  strace -f -e trace=openat,readlink -o /tmp/opencode/perf-disc.strace \
    ./jvma </dev/null >/dev/null 2>/dev/null            # stdin EOF cancela o menu
  rg -c '/task/[0-9]+/status' /tmp/opencode/perf-disc.strace
  rg -c '/cmdline' /tmp/opencode/perf-disc.strace
  kill "$j1" "$j2"
  ```
- **Estimativa de custo:** observado com `M=2`, `T=1`: **2** aberturas de `task/*/status` e **4** de `cmdline` (duplicação confirmada). Fórmula: `M×T` aberturas de status + `2M` aberturas de cmdline; `M=48` (`MAX_DISCOVERED`) e `T=500` ≈ 24.000 aberturas no startup, tipicamente 0,1–1 s (mais em /proc frio ou container).
- **Correção proposta:** adiar `proc_count_threads()` até depois da seleção do PID (menu mostra a contagem obtida sob demanda ou `—`), limitar a varredura por tempo, e passar o `cmdline` já lido para `proc_detect_gc()` (ou detectar GC a partir do buffer em memória) em vez de reabrir o arquivo.
- **Severidade:** P1. **Subtarefa:** 15 (correção de `proc`; chamadores em `src/discovery`, etapa de extração 16).

---

## 4. `fb_write()` com `strlen`+`memcpy` por fragmento; writes por caractere nos desenhos

- **Local:** `fb_write()` 181–187 faz `strlen(s)`, testa `g_fbuf_pos + l < FBUF_SIZE - 2` e `memcpy()` a cada chamada; `fb_printf()` 188–194 adiciona `vsnprintf()` em buffer temporário. Loops por caractere: barra preenchida 717 (`█`) e vazia 721 (`░`), `draw_hline()` 734 (`─`), `draw_panel_hdr()` 751 (`─`), `draw_panel_ftr()` 761 (`─`), `panel_header_bar()` 798 (espaços), `render_help()` 1041 (espaços); writes isolados de espaço em `draw_panel_hdr()` 746/749. Cada glyph UTF-8 tem 3 bytes, então `strlen` e `memcpy` operam sobre 1–3 bytes por chamada.
- **Impacto:** overhead de chamada de função + `strlen` + `memcpy` multiplicado por centenas de células por frame; agrava o flood do item 1 e participa do custo de CPU mesmo em operação normal.
- **Comando de reprodução:**
  ```bash
  rg -n 'fb_write\("█"\)|fb_write\("░"\)|fb_write\("─"\)|fb_write\(" "\)' jvm_analyzer.c
  # casa 717/721/734/746/749/751/761/798 e também 1041 (loop de render_help)
  sleep 30 & p=$!
  strace -c -e trace=write -o /tmp/opencode/perf-fb.strace \
    timeout --preserve-status -s TERM 0.2s ./jvma "$p" </dev/null >/dev/null 2>/dev/null
  cat /tmp/opencode/perf-fb.strace
  kill "$p"
  ```
- **Estimativa de custo:** 80×24 com duas barras (~50 células cada), três molduras e header ≈ 300–500 `fb_write()` por frame; a ~200 frames/s (item 1) → 60–100 mil chamadas/s, ≈ 1–3% de um núcleo só nesse overhead. O `write()` final já é único por frame (177) — o problema é a montagem, não a syscall.
- **Correção proposta:** helper `fb_repeat(const char *glyph, int n)` que calcula o tamanho uma vez (macro com literal, `sizeof(s)-1`) e faz **um** `memcpy` do padrão repetido respeitando o limite do buffer; trocar os loops 717/721/734/751/761/798/1041 e os writes isolados 746/749 por esse helper, preservando as trocas de cor. Manter a sequência exata de bytes ANSI e o flush único.
- **Severidade:** P1. **Subtarefa:** 10 (correção de `layout`/buffer; aplicação em `src/render`/`src/term`, etapas de extração 17/15).

---

## 5. `strcat()` repetido em `ring_sparkline()`

- **Local:** `ring_sparkline()` 275–296: laço 288–295 chama `strcat(out, SPARK[lv])` uma vez por amostra visível (até `HISTORY_LEN=60`). `strcat` percorre o prefixo já escrito a cada chamada → O(n²). Chamado por `panel_cpu()` 831, `panel_memory()` 893 e `panel_threads()` 952.
- **Impacto:** varredura redundante do buffer de saída três vezes por frame; pequeno em valor absoluto, mas é o padrão mais caro dentro do render.
- **Comando de reprodução:**
  ```bash
  rg -n 'strcat\(out, SPARK\[lv\]\)' jvm_analyzer.c          # linha 294
  # perf stat falha aqui (perf_event_paranoid=4): usar /usr/bin/time -v e strace -c -f
  /usr/bin/time -v -o /tmp/opencode/perf-spark.time \
    timeout --preserve-status -s TERM 0.2s ./jvma 1 </dev/null >/dev/null 2>/dev/null
  rg 'User time|System time|Elapsed|Maximum resident' /tmp/opencode/perf-spark.time
  strace -f -c -e trace=read,write -o /tmp/opencode/perf-spark.strace \
    timeout --preserve-status -s TERM 0.2s ./jvma 1 </dev/null >/dev/null 2>/dev/null
  rg 'read|write|total' /tmp/opencode/perf-spark.strace
  ```
- **Estimativa de custo:** com `n=60`, 3 bytes por glyph: `3 × (1+…+60) ≈ 5.490` bytes varridos por sparkline; `3 sparklines × ~200 frames/s ≈ 3,3 MB/s` de leituras redundantes. Custa poucos percentuais de CPU, mas crescem com a taxa de frames do item 1.
- **Correção proposta:** substituir `strcat` por escrita com cursor explícito: `memcpy(out + p, SPARK[lv], 3); p += 3;` e `out[p] = '\0'` no fim; dimensionar `out` para `HISTORY_LEN*3 + 1`. Preservar ordem oldest→newest e os 8 níveis.
- **Severidade:** P2. **Subtarefa:** 10 (correção de `layout`/buffer; aplicação em `src/ring`, etapa de extração 13).

---

## 6. Buffer fixo de 128 KB e descarte latente (não observado em 240×80)

- **Local:** `#define FBUF_SIZE (128*1024)` em 53; `g_fbuf[FBUF_SIZE]` em 167; `fb_write()` 183 descarta o fragmento inteiro quando `g_fbuf_pos + l >= FBUF_SIZE - 2`, sem `else`, sem log e sem checar o retorno de `write()` em 177 (escrita parcial/`EINTR` também passam despercebidos).
- **Impacto:** risco funcional latente (perda silenciosa do fim do frame), **não observado** em 240×80: ~9,4 KB/frame cabem folgados em 131.072 bytes e footer/`ALERTS` aparecem em 434/435 frames. A condição sem `else` em 183 e o descarte do retorno de `write()` em 177 continuam sendo fragilidades, mas não há evidência de perda neste cenário.
- **Comando de reprodução:**
  ```bash
  sleep 30 & p=$!
  script -qec "stty rows 80 cols 240; \
    timeout --preserve-status -s TERM 0.2s ./jvma $p </dev/null" \
    /tmp/opencode/perf-big.tscript >/dev/null 2>&1
  wc -c /tmp/opencode/perf-big.tscript
  rg -o '\[Q\]Quit' /tmp/opencode/perf-big.tscript | wc -l   # frames com footer
  rg -o 'ALERTS' /tmp/opencode/perf-big.tscript | wc -l      # frames com painel de alertas
  # este comando MED o tamanho do frame; NÃO demonstra descarte em 183
  # (para provar descarte seria preciso instrumentar g_fbuf_pos/fb_write)
  kill "$p"
  ```
- **Estimativa de custo:** 80×24 cabe folgado (~5–15 KB/frame); 240×80 ≈ 19.200 células, mas 4.076.992 bytes / 435 frames ≈ 9,4 KB/frame (ordem confirmada em execução repetida) — muito abaixo de 131.072 bytes. Footer e `ALERTS` presentes em 434/435 frames: nenhum descarte medido. O custo é o buffer fixo de 128 KB reservados, não CPU nem perda de saída.
- **Correção proposta:** manter `FBUF_SIZE` fixo — não há evidência que justifique `realloc`; endurecer `fb_flush()` com `write()` em loop tratando retorno parcial/`EINTR` e, se um fragmento não couber, contabilizar e reportar uma vez no stderr. Reavaliar o teto apenas com medição de pior caso real. Preservar byte-a-byte o frame em 80×24/240×80 e o flush único.
- **Severidade:** P2 (latente, sem impacto medido). **Subtarefa:** 10 (correção de `layout`/buffer; dono do buffer `src/term`, etapa de extração 15).

---

## 7. `usleep(800000)` por export

- **Local:** `export_json()` 662 e `export_csv()` 688 bloqueiam 800 ms após `term_restore()` e `printf()` da notificação, antes de `term_raw()`.
- **Impacto:** cada tecla `e`/`c` congela a TUI e o ciclo de amostragem; o atraso é fixo e não depende do tamanho do arquivo nem do disco.
- **Comando de reprodução:**
  ```bash
  sleep 30 & p=$!
  printf 'ecq' | /usr/bin/time -p ./jvma "$p" /tmp/opencode/perf-export >/dev/null \
    2>/tmp/opencode/perf-export.time
  cat /tmp/opencode/perf-export.time                  # real ≈ 1.60
  # -f é obrigatório: jvma é filho do sh -c
  strace -f -e trace=clock_nanosleep -o /tmp/opencode/perf-sleep.strace \
    sh -c "printf 'ecq' | ./jvma $p /tmp/opencode/perf-export >/dev/null"
  rg clock_nanosleep /tmp/opencode/perf-sleep.strace
  kill "$p"
  ```
- **Estimativa de custo:** 2 exports (`e`+`c`) = 1,6 s de sono por sessão de 3 teclas (`time -p` mede `real 1.60`); 10 exports = 8 s. O `strace -f` mostra 2 chamadas `clock_nanosleep(CLOCK_REALTIME, 0, {tv_sec=0, tv_nsec=800000000}, NULL)` — uma por tecla; `nanosleep` não aparece porque o glibc usa `clock_nanosleep`. Em disco lento, soma-se o `fprintf`/`fclose`.
- **Correção proposta:** eliminar o sono: manter a mensagem ANSI exata, mas fazer o "flash" por estado com expiração baseada em `clock_gettime()` no footer/render de 1 frame (ou, no mínimo, reduzir para 100–150 ms). Nomes de arquivo, ordem de campos e `time(NULL)` separado no CSV permanecem idênticos.
- **Severidade:** P2. **Subtarefa:** 8 (correção de `export`; aplicação em `src/export`, etapa de extração 16).

---

## 8. Custos adicionais obrigatórios

### 8.1 Custo por amostra de `update_jvm()` — P1, subtarefa 15 (correção de `proc`; amostragem em `src/sample`, etapa de extração 15)

- **Local:** `update_jvm()` 591–625 encadeia `proc_read_cpu()` (593), `proc_read_mem_status()` (599), `proc_read_smaps_rollup()` (600), `proc_estimate_heap()` condicional (602–607), `proc_count_threads()` (609), `proc_read_fd()` (610), `proc_uptime_secs()` (611), `rand()` (614–617), três `ring_push()` (619–621) e `eval_alerts()` (623).
- **Impacto:** é o nó central de I/O do ciclo; nenhuma amostra é incremental — tudo é reaberto do zero. Com `paused`, o custo cai a zero (1269), confirmando que a amostragem é o gargalo.
- **Comando de reprodução:**
  ```bash
  sleep 30 & p=$!
  strace -f -c -e trace=openat,read,getdents64,stat,close \
    -o /tmp/opencode/perf-sample.strace \
    timeout --preserve-status -s TERM 1s ./jvma "$p" </dev/null >/dev/null 2>/dev/null
  cat /tmp/opencode/perf-sample.strace
  kill "$p"
  ```
- **Estimativa de custo:** ≈ `10 + N` aberturas por amostra (`stat`, `stat`, `status`, `smaps_rollup`, `smaps` 1/5, `task` + `N × status`, `fd` + `limits`, `uptime`) e ≈ `15 + N + linhas_de_smaps` leituras. `N=50`: ~5–15 ms/amostra; `N=500`: ~20–100 ms. A 2 s isso é 0,3–5% de um núcleo; no flood do item 1, multiplica por ~200.
- **Correção proposta:** distribuir leitores caros entre ciclos (round-robin), cachear valores estáveis (`mem_total`, GC, cmdline já são do startup), reaproveitar a leitura de `/proc/<pid>/stat` feita por `proc_read_cpu()` em `proc_uptime_secs()` (ambas abrem o mesmo arquivo em 303 e 463), e aplicar a cadência dos itens 2/8.3. Orquestração em `src/sample`.

### 8.2 `sysconf()` dentro de `proc_calc_cpu()` — P2, subtarefa 15 (correção de `proc`; leitor em `src/proc`, etapa de extração 14)

- **Local:** `proc_calc_cpu()` 322–333 chama `sysconf(_SC_CLK_TCK)` em 328 e `sysconf(_SC_NPROCESSORS_ONLN)` em 329 a cada amostra, embora ambos sejam constantes durante a execução.
- **Impacto:** duas consultas redundantes por ciclo; baixo custo isolado, mas desperdiçado e mais visível sob o flood do item 1.
- **Comando de reprodução:**
  ```bash
  # ltrace não existe e perf stat falha aqui (perf_event_paranoid=4)
  # sysconf não vira syscall direta confiável: medir o sysfs induzido e o custo agregado
  strace -f -c -e trace=openat,read -o /tmp/opencode/perf-sysconf.strace \
    timeout --preserve-status -s TERM 0.2s ./jvma 1 </dev/null >/dev/null 2>/dev/null
  cat /tmp/opencode/perf-sysconf.strace
  strace -f -e trace=openat -o /tmp/opencode/perf-sysconf-detail.strace \
    timeout --preserve-status -s TERM 0.2s ./jvma 1 </dev/null >/dev/null 2>/dev/null
  rg -c '/sys/devices/system/cpu/online' /tmp/opencode/perf-sysconf-detail.strace
  /usr/bin/time -v -o /tmp/opencode/perf-sysconf.time \
    timeout --preserve-status -s TERM 0.2s ./jvma 1 </dev/null >/dev/null 2>/dev/null
  rg 'User time|System time|Elapsed' /tmp/opencode/perf-sysconf.time
  getconf CLK_TCK; nproc
  ```
- **Estimativa de custo:** 2 chamadas/amostra; ~1–20 µs cada (mais em container, onde `_SC_NPROCESSORS_ONLN` consulta sysfs — `rg -c` acima conta as reaberturas de `/sys/devices/system/cpu/online`). A 2 s: ~1 chamada/s de cada — desprezível; sob 200 amostras/s: ~400 `sysconf()`/s. A duplicação em si é provada pela leitura de 328–329, não por `ltrace`/`perf stat`.
- **Correção proposta:** resolver uma vez com `static long clk_hz` / `static long ncpu` (inicialização lazy em `proc.c`) e reutilizar; manter o fallback `ncpu < 1 → 1` e o clamp 0–100. Observar que `proc_uptime_secs()` 480 faz o mesmo para `_SC_CLK_TCK`.

### 8.3 Leitura de `/proc/smaps` a cada 5 ciclos — P1, subtarefa 15 (correção de `proc`; leitor em `src/proc`, etapa de extração 14)

- **Local:** `heap_ctr` (133) começa em `4` em 1255, de modo que a **primeira** amostra já faz o scan; `update_jvm()` 602–607 dispara `proc_estimate_heap()` quando `heap_ctr >= 5`, e a função 365–397 varre `/proc/<pid>/smaps` linha a linha com `fgets`/`sscanf`/`strstr`.
- **Impacto:** pico de I/O e de parsing a cada 5 amostras (≈10 s no intervalo padrão de 2 s), proporcional ao número de mapeamentos da JVM; causa jitter mesmo quando a média é baixa.
- **Comando de reprodução:**
  ```bash
  sleep 30 & p=$!
  wc -c /proc/"$p"/smaps /proc/"$p"/smaps_rollup
  strace -f -e trace=openat,read -o /tmp/opencode/perf-smaps.strace \
    timeout --preserve-status -s TERM 1s ./jvma "$p" </dev/null >/dev/null 2>/dev/null
  rg -c '/smaps"' /tmp/opencode/perf-smaps.strace
  kill "$p"
  ```
- **Estimativa de custo:** `/proc/self/smaps` medido aqui com 44.404 bytes (698 bytes no `smaps_rollup`); JVMs com 500–4.000 mapeamentos produzem ~0,3–4 MB e 5.000–40.000 linhas por varredura, com 2–30 ms de parsing. Fallback em 606 (`anon_kb * 2/3`) já existe quando a heurística não encontra regiões.
- **Correção proposta:** manter a cadência 5, mas limitar o trabalho: pré-filtrar por `/proc/<pid>/maps` (muito menor) e abrir `smaps` apenas para intervalos candidatos (`java_heap`/anon `rw-p` ≥ 64 MB), com teto de bytes/tempo por varredura; preservar `heap_est_mb` e o `~ heuristic` da UI.

---

## Checklist de aceite desta análise

- 8 itens solicitados cobertos (item 8 com três subpontos obrigatórios), cada um com linha, comando de reprodução, estimativa de custo, correção proposta, severidade e subtarefa.
- Severidades: 1× P0 (item 1), 5× P1 (2, 3, 4, 8.1, 8.3), 4× P2 (5, 6, 7, 8.2).
- Nenhuma alteração de `jvm_analyzer.c`, `Makefile`, `README.md` ou dos docs 01/02/03; escopo restrito à criação deste arquivo.
