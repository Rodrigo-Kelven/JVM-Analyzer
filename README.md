# JVMA v6.0 — Advanced JVM Analyzer

Monitor profissional de processos JVM em tempo real para Linux.
TUI colorida com 256 cores ANSI/VT100, zero dependências externas — apenas C99 + `/proc` + `-lm`.

---

## Prévia do Dashboard

```
╭────────────────────  JVMA v6.0   PID:12345   G1GC   UP:2h14m03s   14:33:07  ─────────────────────╮
  CMD: java -server -XX:+UseG1GC -Xmx4g -jar myapp.jar
╭── CPU ──────────────────────────────────────────────────────────────────────────────────────────────╮
│ Usage   [████████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░]  40.3%                     │
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
- GCC ou Clang com suporte a C99
- Terminal com suporte a 256 cores (qualquer terminal moderno)
- Java 8+ em execução (para monitorar)
- Tamanho mínimo de terminal: 70 × 18

---

## Uso

### 1. Auto-descoberta de processos JVM

```bash
./jvma
```

Varre `/proc/*/exe` procurando por executáveis chamados `java` ou `javaw`. Exibe um menu interativo com até 48 processos encontrados. Se apenas um processo for encontrado, a seleção é automática:

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

### 2. PID específico

```bash
./jvma 12345
```

### 3. PID + diretório de exportação customizado

```bash
./jvma 12345 /var/log/jvma
```

O diretório de exportação padrão é `./exports` (criado automaticamente na primeira exportação).

### Encontrando PIDs de JVM

```bash
# Via jps (requer JDK instalado)
jps -l

# Via ps
ps aux | grep java
```

---

## Funcionalidades

### Métricas Reais (lidas do `/proc`)

| Métrica | Fonte |
|---------|-------|
| CPU % | `/proc/[pid]/stat` — delta de `utime + stime` entre amostras, normalizado por núcleos via `sysconf` |
| RSS / VSZ / Swap / VmPeak | `/proc/[pid]/status` |
| PSS / Memória anônima | `/proc/[pid]/smaps_rollup` |
| Heap Java (estimativa~) | `/proc/[pid]/smaps` — regiões `java_heap` ou mapeamentos anon `rw-p` ≥ 64 MB |
| Contagem e estado de threads | `/proc/[pid]/task/[tid]/status` para cada thread |
| Virtual Threads (Loom) | String `VirtualThread` no `status` da thread |
| File descriptors abertos | Contagem de entradas em `/proc/[pid]/fd` |
| Limite de FDs | `/proc/[pid]/limits` campo `Max open files` |
| Uptime do processo | Campo `starttime` de `/proc/[pid]/stat` vs `/proc/uptime` |
| Tipo de GC | Flags JVM na linha de comando via `/proc/[pid]/cmdline` |
| Memória total do sistema | `/proc/meminfo` campo `MemTotal` |

> **Nota sobre GC Pause:** O campo `gc_pause_ms` é **estimado por simulação de passeio aleatório** — um valor real exigiria agente JMX ou parse de GC log. O campo é sempre exibido com a label `(est)` na interface.

---

### Interface TUI

**Paleta de 256 cores ANSI/VT100** — sem ncurses, sem dependências externas.

**Painéis:**
- **CPU** — barra de progresso + sparkline de 60 amostras
- **MEMORY** — barras de RSS e Heap~, linha de VSZ/PSS/Anon, aviso de Swap, sparkline de 60 amostras
- **THREADS** — barra de contagem, breakdown Platform/Virtual/Daemon, estados Runnable/Waiting/Blocked/Zombie, sparkline de 60 amostras
- **GC & FILE DESCRIPTORS** — tipo de coletor, pausa estimada, barra de uso de FDs
- **ALERTS** — até 5 alertas ativos com severidade `[WARN]` ou `[CRIT]`, ou "✓ All systems nominal"

**Barras de progresso** com gradiente automático de cor:
- Verde → abaixo do threshold de aviso
- Amarelo → entre warn e crit
- Vermelho → acima do threshold crítico

**Sparklines** usando `▁▂▃▄▅▆▇█` — normalização automática min/max por janela.

**Layout adaptativo:**
- Terminal `≥ 110 colunas` → duas colunas (CPU + Threads + GC à esquerda, Memória à direita)
- Terminal `< 110 colunas` → coluna única
- Terminal `< 70×18` → mensagem de erro em vez do dashboard

**Renderização sem flicker** — frame buffer de 128 KB em memória, escrita atômica com único `write()`. Cursor posicionado com `\033[H` sem limpar a tela; cada linha é sobrescrita in-place.

**Redimensionamento** — reage ao `SIGWINCH` e atualiza o dashboard imediatamente.

---

### Sistema de Alertas

Alertas são avaliados a cada ciclo de atualização e deduplicados por tipo (no máximo 24 ativos, exibidos até 5 por vez). Severidades: `[WARN]` e `[CRIT]`.

| Alerta | Warn | Crit |
|--------|------|------|
| CPU alto | > 70% | > 90% |
| Pausa de GC | > 50 ms | > 200 ms |
| Total de threads | > 150 | > 300 |
| Threads zombie | > 3 | > 8 |
| Threads bloqueadas (lock contention) | > 5 | — |
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
| `h` / `H` | Exibir overlay de ajuda (fecha com qualquer tecla) |

Intervalo padrão: **2 segundos**. O intervalo atual é exibido no rodapé (`Int:Xs`). Quando pausado, o rodapé exibe `[PAUSED]` em amarelo.

---

### Exportação

**JSON** — snapshot completo, um arquivo por exportação, nome único por timestamp:

```
exports/jvma_<pid>_<unix_timestamp>.json
```

**CSV** — modo append, uma linha por exportação manual. O cabeçalho é gerado automaticamente apenas na primeira linha:

```
exports/jvma_<pid>.csv
```

Após cada exportação, o terminal exibe brevemente uma confirmação do caminho do arquivo salvo antes de retornar ao dashboard.

#### Exemplo de JSON exportado

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

#### Cabeçalho CSV

```
timestamp,pid,cpu_pct,rss_mb,vsz_mb,swap_mb,pss_mb,heap_est_mb,anon_mb,
threads,runnable,waiting,blocked,zombie,virtual,daemon,fd_count,fd_limit,
gc_type,gc_pause_ms,uptime_s
```

---

## Detalhes Técnicos

### Cálculo de CPU

```
delta_ticks = (utime₂ + stime₂) − (utime₁ + stime₁)
cpu% = (delta_ticks / CLK_TCK / wall_seconds / nproc) × 100
```

- `CLK_TCK` via `sysconf(_SC_CLK_TCK)` (normalmente 100 Hz)
- `nproc` via `sysconf(_SC_NPROCESSORS_ONLN)`
- `wall_seconds` medido com `clock_gettime(CLOCK_MONOTONIC)`
- Resultado normalizado por núcleo, intervalo 0–100%, mesmo critério do `htop`
- Primeira amostra sempre retorna 0% (sem delta disponível)

### Estimativa de Heap Java

Executa varredura de `/proc/[pid]/smaps` a cada 5 ciclos (para evitar I/O excessivo). Identifica regiões Java por dois critérios, nesta ordem:

1. **Label `[anon:java_heap]` ou `JavaHeap`** — disponível em kernels ≥ 5.17 com JVM ≥ 17
2. **Mapeamentos anônimos `rw-p` com inode 0 e `Size` ≥ 64 MB** — heurística compatível com qualquer JVM

Soma o campo `Rss` (memória física residente) das regiões qualificadas. Se a varredura retornar zero, usa como fallback `anon_kb * 2/3`. Exibido com `~` na UI para indicar estimativa.

### Classificação de Threads

Lê `/proc/[pid]/task/[tid]/status` para cada thread do processo. Aceita tanto o formato `State:\tX` quanto `State: X` (variações entre kernels):

| Estado do kernel | Classificação |
|-----------------|---------------|
| `R` | Runnable |
| `S` | Waiting / Sleeping |
| `D` | Blocked (I/O wait) |
| `Z` | Zombie |

**Virtual Threads (Java 21+ Project Loom):** detectadas pela presença de `VirtualThread` no arquivo de status da thread. `Platform = Total − Virtual`.

**Threads daemon:** detectadas pela string `: daemon` no arquivo de status.

### Detecção de GC

Lê `/proc/[pid]/cmdline` e verifica flags JVM em ordem:

| Flag na cmdline | GC detectado |
|----------------|-------------|
| `-XX:+UseG1GC` | G1GC |
| `-XX:+UseZGC` | ZGC |
| `-XX:+UseShenandoahGC` | ShenandoahGC |
| `-XX:+UseParallelGC` | ParallelGC |
| `-XX:+UseConcMarkSweepGC` | CMS |
| `-XX:+UseSerialGC` | SerialGC |
| *(nenhuma flag)* | Default |

### File Descriptors

Abre `/proc/[pid]/fd` e conta entradas com nome iniciando por dígito. O limite soft é lido de `/proc/[pid]/limits` (campo `Max open files`), com fallback de 4096. Se o acesso for negado (processo de outro usuário), exibe `(access denied — run as same user)`.

### Renderização sem Flicker

Toda a saída de um frame é montada num buffer de 128 KB em memória via `fb_printf`/`fb_write` e enviada com uma única chamada `write(STDOUT_FILENO, ...)`. O cursor é posicionado com `\033[H` (home position) sem apagar a tela — cada linha é sobrescrita no lugar com `\033[K` (erase to end of line). O cursor é ocultado durante o monitoramento com `\033[?25l` e restaurado ao sair.

### Auto-descoberta

Varre `/proc/*/exe` com `readlink()` buscando executáveis cujo `basename` seja `java` ou `javaw`. Para cada processo encontrado, lê cmdline, detecta GC e conta threads antes de exibir o menu. Suporta até 48 processos simultâneos.

### Gerenciamento de Terminal

Usa `termios` para colocar o terminal em modo raw (sem `ICANON` nem `ECHO`), com timeout de leitura de 100 ms (`VTIME=1`). O estado original é restaurado via `SIGINT`, `SIGTERM` e ao sair normalmente. O `SIGWINCH` define uma flag `g_resize` que é verificada antes de cada frame.

---

## Estrutura do Projeto

```
JVM-Analyzer/
├── jvm_analyzer.c   — código-fonte completo (~1.295 linhas, C99)
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
- CPU real via delta de `/proc/[pid]/stat` com `CLOCK_MONOTONIC`
- Memória real via `/proc/[pid]/status` + `smaps_rollup`
- Estimativa de heap Java via varredura de `/proc/[pid]/smaps` (a cada 5 ciclos)
- Análise de threads: Platform, Virtual (Loom), Daemon, Runnable, Waiting, Blocked, Zombie
- File descriptor monitoring com leitura do limite soft via `/proc/[pid]/limits`
- Controles interativos: pausar, exportar JSON/CSV, ajustar velocidade, overlay de ajuda
- Sparklines de histórico (60 amostras, `▁▂▃▄▅▆▇█`) para CPU, RSS e threads
- Layout adaptativo (duas colunas ≥ 110 colunas, coluna única abaixo)
- Buffer de frame de 128 KB — renderização atômica sem flicker
- Auto-descoberta de até 48 JVMs via `/proc/*/exe`
- Sistema de alertas deduplicado com dois níveis (WARN / CRIT)
- Tratamento de `SIGWINCH` (redimensionamento de terminal)
- Zero dependências externas — apenas `-lm`

### v5.1

- TUI básica com `printf` e emojis
- Métricas de memória simuladas (valores aleatórios)
- Análise de threads via `/proc/[pid]/task`
- Exportação JSON simples
