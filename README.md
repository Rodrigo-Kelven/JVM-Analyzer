# JVMA v6.0 — Advanced JVM Analyzer

Monitor profissional de processos JVM em tempo real para Linux.  
Interface colorida com 256 cores, sem dependências externas — apenas C padrão + `/proc`.

---

## Prévia do Dashboard

```
╭────────────────────  JVMA v6.0   PID:12345   G1GC   UP:2h14m03s   14:33:07  ─────────────────────╮
  CMD: java -server -XX:+UseG1GC -Xmx4g -jar myapp.jar
╭── CPU ──────────────────────────────────────────────────────────────────────────────────────────────╮
│ Usage   [████████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]  40.3%                   │
│ 60s     ▁▂▃▄▅▆▆▇▇▆▇▇▇▆▆▅▄▃▂▃▄▅▆▇▇▇▆▅▄▃▂▁▁▂▃▄▅▆▆▇▇▆▇▇▇▆▆▅▄▃▂▃▄▅▆         │
╰─────────────────────────────────────────────────────────────────────────────────────────────────────╯
╭── MEMORY ───────────────────────────────────────────────────────────────────────────────────────────╮
│ RSS     [████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]  12.4%           512MB    │
│ Heap~   [████████████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]  49.2%           398MB    │
│ VSZ:4096MB  PSS:490MB  Anon:412MB                                                                   │
│ Swap: none                                                                                          │
│ 60s     ▁▁▁▂▂▂▂▃▃▃▄▄▄▄▄▅▅▅▆▆▆▆▆▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇▇          │
│ ~ heuristic from /proc/smaps (no JMX agent required)                                               │
╰─────────────────────────────────────────────────────────────────────────────────────────────────────╯
╭── THREADS ──────────────────────────────────────────────────────────────────────────────────────────╮
│ Count   [████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]  27.7%                83   │
│ Platform 81     Virtual  2      Daemon   45                                                         │
│ Runnable 12     Waiting  70     Blocked  0                                                          │
│ Zombie   0                                                                                          │
│ 60s     ▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅          │
╰─────────────────────────────────────────────────────────────────────────────────────────────────────╯
╭── GC & FILE DESCRIPTORS ────────────────────────────────────────────────────────────────────────────╮
│ Collector G1GC             Pause    12.4ms  (est)                                                   │
│ Open FDs  [█░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]   5.2%         234/4096     │
╰─────────────────────────────────────────────────────────────────────────────────────────────────────╯
╭── ALERTS ───────────────────────────────────────────────────────────────────────────────────────────╮
│ ✓  All systems nominal                                                                              │
╰─────────────────────────────────────────────────────────────────────────────────────────────────────╯
 [Q]Quit  [P]Pause  [E]JSON  [C]CSV  [H]Help  [+/-]Speed  Int:2s
```

---

## Funcionalidades

### Métricas Reais (lidas do `/proc`)

| Métrica | Fonte no `/proc` |
|---------|-----------------|
| CPU % | `/proc/[pid]/stat` — delta entre amostras, normalizado por núcleos |
| RSS / VSZ / PSS / Swap | `/proc/[pid]/status` + `/proc/[pid]/smaps_rollup` |
| Memória anônima | `/proc/[pid]/smaps_rollup` campo `Anonymous` |
| Heap Java (estimativa~) | `/proc/[pid]/smaps` — regiões anon `rw-p` ≥ 64 MB |
| Contagem de threads | `/proc/[pid]/task/*/status` |
| Estado de threads | Campo `State` em cada `/proc/[pid]/task/[tid]/status` |
| File descriptors | Contagem de entradas em `/proc/[pid]/fd` |
| Limite de FDs | `/proc/[pid]/limits` campo `Max open files` |
| Uptime do processo | `/proc/[pid]/stat` campo `starttime` vs `/proc/uptime` |
| Tipo de GC | Flags na linha de comando via `/proc/[pid]/cmdline` |

> **Nota:** A pausa de GC (`gc_pause_ms`) é **estimada por simulação** — dado real exigiria agente JMX ou parse de GC log. O campo é sempre exibido com a label `(est)`.

---

### Interface Gráfica (TUI)

- **256 cores ANSI** — sem ncurses, sem dependências externas
- **Barras de progresso** com gradiente de cor automático:
  - Verde → `< warn`  |  Amarelo → `≥ warn`  |  Vermelho → `≥ crit`
- **Sparklines de histórico** — últimas 60 amostras usando `▁▂▃▄▅▆▇█`
  - CPU, RSS (memória física), threads
- **Layout adaptativo:**
  - Terminal `≥ 110 colunas` → duas colunas (CPU+Threads à esq., Memória à dir.)
  - Terminal `< 110 colunas` → coluna única
- **Renderização sem flicker** — frame buffer de 128 KB, escrita atômica por frame
- **Redimensionamento** — reage ao `SIGWINCH` sem reiniciar

---

### Sistema de Alertas

Alertas são avaliados a cada ciclo e deduplicados. Severidades: `[WARN]` e `[CRIT]`.

| Alerta | Warn | Crit |
|--------|------|------|
| CPU alto | > 70% | > 90% |
| Pausa de GC | > 50 ms | > 200 ms |
| Total de threads | > 150 | > 300 |
| Threads zombie | > 3 | > 8 |
| Threads bloqueadas (lock) | > 5 | — |
| File descriptors abertos | > 800 | > 3.000 |
| Swap em uso | qualquer valor | — |

---

### Controles Interativos

| Tecla | Ação |
|-------|------|
| `q` / `Q` | Sair |
| `p` / `P` | Pausar / Retomar atualizações |
| `e` / `E` | Exportar snapshot JSON agora |
| `c` / `C` | Acrescentar linha CSV agora |
| `+` / `=` | Diminuir intervalo (mais rápido, mínimo 500 ms) |
| `-` / `_` | Aumentar intervalo (mais lento, máximo 10 s) |
| `h` / `H` | Exibir/esconder overlay de ajuda |

---

### Exportação

**JSON** — snapshot completo por execução:
```
exports/jvma_<pid>_<unix_timestamp>.json
```

**CSV** — modo append, uma linha por exportação manual:
```
exports/jvma_<pid>.csv
```

---

## Compilação

Nenhuma dependência além da biblioteca matemática padrão:

```bash
# Com Makefile (recomendado)
make

# Ou diretamente com gcc
gcc -O2 -o jvma jvm_analyzer.c -lm
```

**Requisitos:**
- Linux com sistema de arquivos `/proc` ativo
- GCC ou Clang (C99 ou superior)
- Terminal com suporte a 256 cores (qualquer terminal moderno)
- Java 8+ rodando (para monitorar)

---

## Uso

### 1. Auto-descoberta de processos JVM

```bash
./jvma
```

Exibe uma lista interativa de todos os processos `java` em execução:

```
  JVMA v6.0 — JVM Process Discovery
  ──────────────────────────────────────────────────────────────────────────
  #    PID      THREADS  GC              COMMAND
  ──────────────────────────────────────────────────────────────────────────
  1    12345    83       G1GC            java -server -XX:+UseG1GC -jar app
  2    23456    234      ZGC             java -Xmx8g -XX:+UseZGC Service
  ──────────────────────────────────────────────────────────────────────────
  Select [1-2] or press Enter to cancel:
```

Se apenas um processo for encontrado, a seleção é automática.

### 2. PID específico

```bash
./jvma 12345
```

### 3. PID + diretório de exportação customizado

```bash
./jvma 12345 /var/log/jvma
```

### Encontrando PIDs de JVM

```bash
# Via jps (requer JDK instalado)
jps -l

# Via ps
ps aux | grep java
```

---

## Estrutura dos Exports

### JSON

```json
{
  "jvma_version": "6.0",
  "timestamp": "2026-04-12T23:33:07Z",
  "unix_ts": 1744500787,
  "pid": 12345,
  "gc_type": "G1GC",
  "uptime_s": 8073,
  "cmdline": "java -server -XX:+UseG1GC -Xmx4g -jar myapp.jar",
  "cpu_pct": 40.30,
  "memory": {
    "rss_mb": 512.0,
    "vsz_mb": 4096.0,
    "swap_mb": 0.0,
    "pss_mb": 490.0,
    "heap_est_mb": 398.0,
    "anon_mb": 412.0
  },
  "threads": {
    "total": 83,
    "virtual": 2,
    "platform": 81,
    "daemon": 45,
    "runnable": 12,
    "waiting": 70,
    "blocked": 0,
    "zombie": 0
  },
  "fds": {
    "open": 234,
    "limit": 4096
  },
  "gc_pause_ms": 12.4,
  "alerts": 0
}
```

### CSV

Cabeçalho gerado automaticamente na primeira exportação:

```
timestamp,pid,cpu_pct,rss_mb,vsz_mb,swap_mb,pss_mb,heap_est_mb,anon_mb,
threads,runnable,waiting,blocked,zombie,virtual,daemon,fd_count,fd_limit,
gc_type,gc_pause_ms,uptime_s
```

Exemplo de linha:
```
1744500787,12345,40.30,512.0,4096.0,0.0,490.0,398.0,412.0,83,12,70,0,0,2,45,234,4096,G1GC,12.4,8073
```

---

## Detalhes Técnicos

### Leitura de CPU

```
delta_ticks = (utime₂ + stime₂) − (utime₁ + stime₁)
cpu% = (delta_ticks / CLK_TCK / elapsed_segundos / nproc) × 100
```

- `CLK_TCK` via `sysconf(_SC_CLK_TCK)` (normalmente 100 Hz)
- `nproc` via `sysconf(_SC_NPROCESSORS_ONLN)`
- Resultado normalizado por núcleo (0–100%), mesmo critério do `htop`

### Estimativa de Heap Java

Varre `/proc/[pid]/smaps` a cada 5 ciclos (para evitar I/O excessivo) buscando:

1. **Regiões com label `[anon:java_heap]`** — disponível em kernels ≥ 5.17 com JVM ≥ 17
2. **Mapeamentos anônimos `rw-p` ≥ 64 MB com inode 0** — heurística confiável para qualquer JVM

Soma o campo `Rss` das regiões qualificadas. Exibido com `~` na UI para indicar estimativa.

### Classificação de Threads

Lê `/proc/[pid]/task/[tid]/status` para cada thread do processo:

| Estado do kernel | Classificação |
|-----------------|---------------|
| `R` | Runnable |
| `S` | Waiting / Sleeping |
| `D` | Blocked (espera por I/O) |
| `Z` | Zombie |

**Virtual Threads** (Java 21+ Project Loom): detectadas pela string `VirtualThread` no arquivo de status da thread.

### Detecção de GC

Lê `/proc/[pid]/cmdline` e verifica flags JVM:

| Flag na cmdline | GC detectado |
|----------------|-------------|
| `-XX:+UseG1GC` | G1GC |
| `-XX:+UseZGC` | ZGC |
| `-XX:+UseShenandoahGC` | ShenandoahGC |
| `-XX:+UseParallelGC` | ParallelGC |
| `-XX:+UseConcMarkSweepGC` | CMS |
| `-XX:+UseSerialGC` | SerialGC |
| *(nenhuma flag)* | Default |

### Renderização sem Flicker

Toda a saída é montada num buffer de 128 KB em memória e enviada com uma única chamada `write()` ao stdout. O cursor é posicionado com `\033[H` (home) sem apagar a tela — cada linha é sobrescrita no lugar, eliminando o flash de `clear`.

### Auto-descoberta

Varre `/proc/*/exe` com `readlink()` buscando executáveis cujo `basename` seja `java` ou `javaw`. Para cada processo encontrado, lê cmdline, detecta GC e conta threads antes de exibir o menu.

---

## Estrutura do Projeto

```
JVM-Analyzer/
├── jvm_analyzer.c   — código-fonte completo (~1.300 linhas, C99)
├── Makefile         — build com gcc, flags de otimização
├── README.md        — esta documentação
└── exports/         — criado automaticamente na primeira exportação
    ├── jvma_<pid>_<ts>.json
    └── jvma_<pid>.csv
```

---

## Histórico de Versões

### v6.0 — Reescrita completa (2026-04)

- TUI colorida com 256 cores via ANSI/VT100 — sem ncurses
- CPU real via delta de `/proc/[pid]/stat`
- Memória real via `/proc/[pid]/status` + `smaps_rollup`
- Estimativa de heap Java via varredura de `/proc/[pid]/smaps`
- Controles interativos: pausar, exportar, ajustar velocidade
- Sparklines de histórico (60 amostras, `▁▂▃▄▅▆▇█`)
- Layout adaptativo (coluna dupla ≥ 110 colunas)
- Buffer de frame atômico — sem flicker
- Auto-descoberta de JVMs via `/proc/*/exe`
- Exportação JSON + CSV
- Sistema de alertas com dois níveis (WARN / CRIT)
- Tratamento de `SIGWINCH` (redimensionamento de terminal)
- Zero dependências externas — apenas `-lm`

### v5.1

- TUI básica com `printf` e emojis
- Métricas de memória simuladas (valores aleatórios)
- Análise de threads via `/proc/[pid]/task`
- Exportação JSON simples
