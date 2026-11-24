# Baseline — JVMA v6.0 (antes de qualquer mudança)

Data da captura: 2026-11-24. Repositório: `/home/kd6-cowboy/tools/JVM-Analyzer`.
Commit: `1f3fc3d` (`remocao do arquivo binario`). Estado inicial do `git status --short`: `?? jvma`.

Ambiente observado:

```
gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
Linux kd6-3.7 7.0.0-43-generic #43-Ubuntu SMP PREEMPT_DYNAMIC Fri Oct 23 13:25:25 UTC 2026 x86_64
LANG=pt_BR.UTF-8   LC_ALL=(vazio)   LC_NUMERIC=(vazio => pt_BR.UTF-8)
```

Hashes dos arquivos que **não** devem ser alterados nesta subtarefa:

```
882224695a1169d22ffe194911f2c66c3817f29d81ed969e8fae6403daf4b5c9  jvm_analyzer.c
aeb0b9f6b3c7829ddd038d94cb4447f755952e1c47596b50e0877a98d8d62d8e  Makefile
68bd832d430155522ae0ad0d9dadf29b2ceb46c091eb330fdf0a8f742b68eecb  README.md
```

---

## 1. Build reproduzível

```bash
cd /home/kd6-cowboy/tools/JVM-Analyzer
(make clean && make) > /tmp/opencode/jvma-full.out 2> /tmp/opencode/jvma-full.err; echo $?
cat /tmp/opencode/jvma-full.out
cat /tmp/opencode/jvma-full.err
grep -Ei 'warning|error' /tmp/opencode/jvma-full.out /tmp/opencode/jvma-full.err || echo 'sem warnings'
```

Saída observada (código de saída `0`):

```
--- stdout ---
rm -f jvma
gcc -O2 -Wall -Wextra -Wno-unused-result -Wno-format-truncation -o jvma jvm_analyzer.c -lm
--- stderr ---
(vazio)
--- warnings/errors ---
(nenhum)
```

Observações:

- `make clean` isolado: stdout `rm -f jvma`, stderr vazio, `rc=0`.
- O build usa `-Wall -Wextra` mas suprime `-Wno-unused-result` e `-Wno-format-truncation`; ainda assim não há nenhum diagnosticado emitido.
- Binário gerado: `jvma` (não rastreado), `sha256 cc6021232aea543b799927aa8eed6a0b4c4f73030937667fd0688514c0bc0f39`.
- `exports/` já existia no baseline ( proprietário `root`, modo `0755`).

---

## 2. Matriz do contrato externo da CLI

> **Correção de referência:** a forma correta é `./jvma <PID> <DIR>` (terceiro argumento opcional = diretório de export). Não existe `./jma`.

Todas as execuções abaixo foram feitas com `stdin </dev/null` quando não indicado. Saídas em forma de TUI foram capturadas com `timeout --preserve-status -s TERM` para evitar loop infinito.

| Comando | Pré-condição / estímulo | stdout observado | stderr observado | `rc` |
|---|---|---|---|---|
| `./jvma` | nenhum processo com `exe` basename `java`/`javaw` (`pgrep -a java` vazio) | vazio | `JVMA v6.0: No Java processes found.`<br>`Usage: ./jvma <PID>` | `1` |
| `./jvma` | 1 processo fake `/tmp/opencode/java` (PID `2308212`) | menu de descoberta + `Auto-selecting PID 2308212` + `Attaching to PID 2308212...` + `JVMA v6.0 exited cleanly.` (Term enviado durante o `usleep(400000)`); com stdout redirecionado, esse texto stdio aparece **depois** dos bytes ANSI escritos diretamente no fd | vazio | `0` |
| `./jvma` | 2 processos fake (PIDs `2308110`, `2308111`), `stdin=/dev/null` ou linha vazia | menu + `Select [1-2] or press Enter to cancel:` + `  Cancelled.` | vazio | `0` |
| `./jvma` | 2 processos, entrada `0` ou `9` | mesmo menu; seleção fora de `[1-n]` cai em `  Cancelled.` | vazio | `0` |
| `./jvma abc` | PID não numérico | vazio | `JVMA: invalid PID 'abc'` | `1` |
| `./jvma 0` | PID zero | vazio | `JVMA: invalid PID '0'` | `1` |
| `./jvma -1` | PID negativo | vazio | `JVMA: invalid PID '-1'` | `1` |
| `./jvma --help` | não há flag de ajuda; `atoi` → `0` | vazio | `JVMA: invalid PID '--help'` | `1` |
| `./jvma 999999` | PID inexistente | vazio | `JVMA: Process 999999 not found.` | `1` |
| `./jvma 1` | PID existente, não-Java (`/sbin/init splash`) | TUI ANSI (ver §5); última linha `JVMA v6.0 exited cleanly.` quando Term é tratado | vazio | `0` com `timeout --preserve-status`; `124` com `timeout` sem `--preserve-status` |
| `./jvma 1 /tmp/opencode/jvma-cli-dir` | forma `./jvma <PID> <DIR>` | mesma TUI; cria o diretório `0755` já no startup (`ensure_export_dir()`, linha 1259) | vazio | `0` com `--preserve-status` |
| `./jvma 999999 /tmp/opencode/jvma-cli-dir2` | PID inexistente + DIR | vazio | `JVMA: Process 999999 not found.` | `1`; diretório **não** criado |
| `./jvma abc /tmp/opencode/jvma-cli-invalid-dir` | PID inválido + DIR | vazio | `JVMA: invalid PID 'abc'` | `1`; diretório **não** criado |
| `./jvma 999999 /tmp/opencode/ignored extra` | argumentos além de `argv[2]` | vazio | `JVMA: Process 999999 not found.` | `1`; args extras ignorados |

Saída exata do menu com 2 processos:

```

  JVMA v6.0 — JVM Process Discovery
  ────────────────────────…
  #    PID      THREADS  GC              COMMAND
  ────────────────────────…
  1    2308110  1        Default         /tmp/opencode/java
  2    2308111  1        Default         /tmp/opencode/javaw
  ────────────────────────…
  Select [1-2] or press Enter to cancel:   Cancelled.
```

Nota: as linhas separadoras usam `printf("%.*s", 74, "────...")`, ou seja, 74 **bytes** sobre uma string multi-byte (3 bytes por `─`); o último caractere sai truncado (bytes `e2 94` sem continuação) — visível como `…`/`￼` na captura acima. Código: linhas 1188-1196.

Comportamento de saída do processo monitorado: se o PID morre durante a coleta, `update_jvm()` retorna `-1` e o programa faz `return 0` **antes** do rótulo `done`, imprimindo apenas `\nJVMA: Process <PID> exited.\n` no stderr (sem `exited cleanly`). Observado com `sleep 1` (PID `2305131`): `rc=0`, stderr com essa linha.

---

## 3. Teclas do help e limiares de alerta

### Teclas (código: `handle_key`, linhas 1139-1152; overlay: 1032-1089; footer: 1014-1027)

| Tecla no código | Ação real |
|---|---|
| `q` / `Q` | `g_running = 0`; sai do loop e imprime `JVMA v6.0 exited cleanly.` (linhas 1290-1293) |
| `p` / `P` | alterna `j->paused`; footer mostra `[P]Pause` ↔ `[P]Resume` e `[PAUSED]` |
| `e` / `E` | `export_json(j)`; imprime ` JSON saved: <path>` e dorme 800 ms |
| `c` / `C` | `export_csv(j)`; imprime ` CSV appended: <path>` e dorme 800 ms |
| `+` / `=` | diminui intervalo em 500 ms, mínimo `500` ms |
| `-` / `_` | aumenta intervalo em 500 ms, máximo `10000` ms |
| `h` / `H` | `render_help()`: desenha overlay 64x25 e **bloqueia** até qualquer tecla; não é um toggle booleano (contrariando "Exibir/esconder" do README) |
| qualquer outra | ignorada |

Padrão: `DEFAULT_ITVL_MS = 2000` (linha 52). O footer exibe `Int:2s` (divisão inteira de `g_itvl_ms/1000`).

### Limiares lidos do código (linhas 55-65 e `eval_alerts`, 542-586)

| Alertas | Constante | Comparação no código | Severidade | Mensagem formatada |
|---|---|---|---|---|
| CPU | `TH_CPU_W 70.0`, `TH_CPU_C 90.0` | `>= 70` / `>= 90` | WARN / CRIT | `CPU high: %.1f%% (threshold %.0f%%)` / `CPU critical: %.1f%% (threshold %.0f%%)` |
| GC pause | `TH_GC_W_MS 50.0`, `TH_GC_C_MS 200.0` | `>= 50` / `>= 200` ms | WARN / CRIT | `GC pause high: %.1fms...` / `GC pause critical: %.1fms...` |
| Total de threads | `TH_THR_W 150`, `TH_THR_C 300` | `>= 150` / `>= 300` | WARN / CRIT | `Thread count high: %d...` / `Thread explosion: %d threads...` |
| Zumbis | `TH_ZMB_W 3`, `TH_ZMB_C 8` | `>= 3` / `>= 8` | WARN / CRIT | `Zombie threads: %d...` / `Zombie threads critical: %d...` |
| Threads bloqueadas | literal `> 5` | `> 5` | WARN apenas | `Lock contention: %d threads blocked` |
| FDs abertos | `TH_FD_W 800`, `TH_FD_C 3000` | `>= 800` / `>= 3000` | WARN / CRIT | `FD high: %d open...` / `FD critical: %d open...` |
| Swap | `j->mem.swap_kb > 0` | `> 0` KB | WARN apenas | `Swap in use: %.0f MB` |

Diferenças em relação ao README: o código usa `>=` (não `>`) para todos os limiares numéricos; bloqueadas é `> 5`. As cores das barras também usam `>= crit` / `>= warn` (`draw_bar`, linhas 697-699). `gc_pause_ms` é **simulado** por random walk (linhas 613-617), não medido via JMX.

---

## 4. Schema exato dos exports

Arquivos: `<DIR>/jvma_<pid>_<unix_ts>.json` e `<DIR>/jvma_<pid>.csv`, com `<DIR>` padrão `./exports`. O diretório é criado com `mkdir(..., 0755)` apenas se `stat()` falhar (sem criação recursiva). Código: linhas 630-690.

### JSON — campos e tipos, na ordem exata em que são escritos (linhas 644-657)

| Campo | Tipo / formato | Origem / observação |
|---|---|---|
| `jvma_version` | string | constante `"6.0"` |
| `timestamp` | string | `%Y-%m-%dT%H:%M:%SZ`, UTC via `gmtime()` |
| `unix_ts` | inteiro (`long`, `time_t`) | `time(NULL)` no momento do export |
| `pid` | inteiro (`int`) | PID monitorado |
| `gc_type` | string | um de `G1GC`, `ZGC`, `ShenandoahGC`, `ParallelGC`, `CMS`, `SerialGC`, `Default` (linhas 449-459) |
| `uptime_s` | inteiro não negativo (`unsigned long`) | `starttime` de `/proc/<pid>/stat` vs `/proc/uptime` |
| `cmdline` | string | máx. 300 bytes (`%.300s`), NULs convertidos em espaço; **sem escape JSON** |
| `cpu_pct` | número com 2 casas | 0–100, normalizado por núcleo |
| `memory.rss_mb` | número com 1 casa | `VmRSS` / 1024 |
| `memory.vsz_mb` | número com 1 casa | `VmSize` / 1024 |
| `memory.swap_mb` | número com 1 casa | `VmSwap` / 1024 |
| `memory.pss_mb` | número com 1 casa | `Pss` de `smaps_rollup` / 1024 |
| `memory.heap_est_mb` | número com 1 casa | heurística `smaps` (ou `anon_kb * 2/3`) / 1024 |
| `memory.anon_mb` | número com 1 casa | `Anonymous` de `smaps_rollup` / 1024 |
| `threads.total` | inteiro | contagem de `/proc/<pid>/task` |
| `threads.virtual` | inteiro | presença de `VirtualThread` no status |
| `threads.platform` | inteiro | `total - virtual` |
| `threads.daemon` | inteiro | `: daemon` no status |
| `threads.runnable` | inteiro | estado `R` |
| `threads.waiting` | inteiro | estado `S` |
| `threads.blocked` | inteiro | estado `D` |
| `threads.zombie` | inteiro | estado `Z` |
| `fds.open` | inteiro | entrada em `/proc/<pid>/fd` (`-1` se sem acesso) |
| `fds.limit` | inteiro | soft limit de `/proc/<pid>/limits`; fallback `4096` |
| `gc_pause_ms` | número com 1 casa | random walk (estimativa) |
| `alerts` | inteiro | quantidade de alertas ativos (~~ 0..7; limite `MAX_ALERTS=24`), **não** é array |

Exemplo real obtido com `LC_ALL=C` (PID `2305440`, `sleep 30`, teclas `e`, `c`, `q` via pipe):

```json
{
  "jvma_version": "6.0",
  "timestamp": "2026-11-24T17:24:31Z",
  "unix_ts": 1795541071,
  "pid": 2305440,
  "gc_type": "Default",
  "uptime_s": 0,
  "cmdline": "sleep 30",
  "cpu_pct": 0.00,
  "memory": { "rss_mb": 7.6, "vsz_mb": 17.4, "swap_mb": 0.0,
    "pss_mb": 3.4, "heap_est_mb": 0.9, "anon_mb": 1.3 },
  "threads": { "total": 1, "virtual": 0, "platform": 1,
    "daemon": 0, "runnable": 0, "waiting": 1, "blocked": 0, "zombie": 0 },
  "fds": { "open": 3, "limit": 524288 },
  "gc_pause_ms": 8.7,
  "alerts": 0
}
```

**Defeito baseline:** com o locale ambiente `pt_BR.UTF-8` (por causa de `setlocale(LC_ALL, "")` na linha 1211), os números são impressos com vírgula decimal. Exemplo real (PID `2305265`):

```json
  "cpu_pct": 0,00,
  "memory": { "rss_mb": 7,8, "vsz_mb": 17,4, "swap_mb": 0,0,
```

Isso torna o JSON inválido e o CSV ambíguo. Com `LC_ALL=C`, os campos saem com ponto decimal, como no exemplo canônico acima.

### CSV — cabeçalho exato (21 colunas) e linha

```
timestamp,pid,cpu_pct,rss_mb,vsz_mb,swap_mb,pss_mb,heap_est_mb,anon_mb,threads,runnable,waiting,blocked,zombie,virtual,daemon,fd_count,fd_limit,gc_type,gc_pause_ms,uptime_s
```

Tipos/ordem: `timestamp` (long), `pid` (int), `cpu_pct` (2 casas), `rss_mb`/`vsz_mb`/`swap_mb`/`pss_mb`/`heap_est_mb`/`anon_mb` (1 casa), `threads`/`runnable`/`waiting`/`blocked`/`zombie`/`virtual`/`daemon`/`fd_count`/`fd_limit` (int), `gc_type` (string sem aspas), `gc_pause_ms` (1 casa), `uptime_s` (unsigned long). Atenção: o CSV **não** contém `threads.platform`; a ordem dos campos de threads difere do JSON (`total,runnable,waiting,blocked,zombie,virtual,daemon`).

Linha real do mesmo export `LC_ALL=C`:

```
1795541072,2305440,0.00,7.6,17.4,0.0,3.4,0.9,1.3,1,0,1,0,0,0,0,3,524288,Default,8.7,0
```

O cabeçalho só é escrito quando `access(path, F_OK) != 0`; escrita sempre em modo append (`fopen(path, "a")`). O `timestamp` da linha CSV vem de uma chamada separada a `time(NULL)` (aqui `...72`, enquanto o JSON usou `...71`).

Notificações na saída (stdout, com ANSI): `\033[32;1m JSON saved: <path>\033[0m\n` e `\033[32;1m CSV appended: <path>\033[0m\n`.

---

## 5. Captura de referência com PID real (sem TTY interativo)

Comando reproduzível (PID real iniciado pelo próprio shell):

```bash
sleep 30 & p=$!
echo "$p"                       # 2304610 nesta execução
timeout --preserve-status -s TERM 0.2s ./jvma "$p" </dev/null \
  > /tmp/opencode/ref-sleep.out 2> /tmp/opencode/ref-sleep.err
rc=$?                            # 0
kill "$p" && wait "$p" 2>/dev/null || true
wc -c /tmp/opencode/ref-sleep.out /tmp/opencode/ref-sleep.err
cat /tmp/opencode/ref-sleep.err
```

Observado:

- `rc=0`; stderr vazio; stdout com **1.122.020 bytes** de sequências ANSI/VT100.
- Sem TTY, `ioctl(TIOCGWINSZ)` falha e `term_get_size()` mantém o padrão `80x24`. `term_raw()` chama `tcgetattr/tcsetattr` em stdin, que falham silenciosamente; não há mensagem de erro.
- Com `stdin=/dev/null`, `term_read_key()` retorna `-1` imediatamente (EOF). O laço `while (elapsed < g_itvl_ms)` soma 100 ms por iteração **sem dormir**, gerando frames em alta velocidade; por isso é obrigatório limitar com `timeout` e redirecionar a saída. (`timeout` sem `--preserve-status` devolve `124`; com ele, o app devolve `0`.)
- Primeiro frame reconstruído (ANSI removido; 80x24):

```
      JVMA v6.0   PID:2304610   Default          UP:0s            14:23:36
 CMD: sleep 30
╭── CPU ───────────────────────────────────────────────────────────────────────╮
│ Usage  [░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]   0,0%         │
│ 60s ▄                                                                        │
╰──────────────────────────────────────────────────────────────────────────────╯
╭── MEMORY ────────────────────────────────────────────────────────────────────╮
│ RSS    [░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]   0,0%      8MB  │
│ Heap~  [░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]   0,0%      1MB  │
│ VSZ:17MB  PSS:4MB  Anon:1MB                                                  │
│ Swap: none                                                                   │
│ 60s ▄                                                                        │
│ ~ heuristic from /proc/smaps (no JVM agent required)                         │
╰──────────────────────────────────────────────────────────────────────────────╯
╭── THREADS ───────────────────────────────────────────────────────────────────╮
│ Count  [░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]   0,3%      1  │
│ Platform 1     Virtual  0     Daemon   0                                     │
│ Runnable 0     Waiting  1     Blocked  0                                     │
│ Zombie   0                                                                   │
│ 60s ▄                                                                        │
╰──────────────────────────────────────────────────────────────────────────────╯
╭── GC & FILE DESCRIPTORS ─────────────────────────────────────────────────────╮
│ Collector Default        Pause    8,4ms (est)                                │
 [Q]Quit  [P]Pause  [E]JSON  [C]CSV  [H]Help  [+/-]Speed  Int:2s
```

Com 24 linhas, a barra de FDs e o painel `ALERTS` não ficam visíveis: o painel de GC começa na linha 22 e o footer (linha 24) sobrescreve a linha de FDs; `panel_alerts()` só é chamado se `row < g_term_h - 2`.

Também foi possível exercitar os exports sem TTY apenas injetando bytes no pipe (`printf 'ecq' | ... ./jvma <PID> <DIR>`): `JSON saved`, `CSV appended` e `JVMA v6.0 exited cleanly.` aparecem na saída, com os arquivos descritos na §4. A tecla `h` não foi exercitada neste modo porque `render_help()` bloqueia em `read(STDIN_FILENO, ...)`.

---

## Conclusão do baseline

- Build limpo, sem warnings, `rc=0`.
- Código-fonte, Makefile e README permanecem intocados (hashes acima).
- Pontos de atenção já registrados para as próximas subtarefas: locale `pt_BR` quebra JSON/CSV; laço sem sleep quando stdin está em EOF; help não é toggle; separadores UTF-8 truncados no menu; limiares no código usam `>=`.
