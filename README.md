# JVMA v5.1

**JVMA v5.1** é um analisador avançado de JVM em C que fornece monitoramento em tempo real de memória heap/non-heap e análise precisa de threads (incluindo Virtual Threads do Java 21+).  

Monitora processos JVM via `/proc` do Linux com dashboard interativo e exportação JSON.

---

## ✨ Principais Funcionalidades

### 🧵 Thread Analysis
- ✅ Contagem precisa de threads via `/proc/[pid]/task`
- ✅ Detecção de Virtual Threads (Java 21+ Loom)
- ✅ Classificação: Zombie, Blocked, Runnable, Waiting
- ✅ Daemon threads
- ✅ Platform vs Virtual threads

### 💾 Memory Monitoring
- ✅ Heap breakdown (Eden, Survivor, OldGen)
- ✅ Non-heap: Metaspace, Thread Stacks, Code Cache, Direct Memory
- ✅ JVM Total Memory
- ✅ Percentuais de uso

### 🚨 Alert System
- 🔥 High CPU (>85%)
- 💥 Heap Exhaustion (>92%)
- 🔒 Lock Contention
- ⏱️ Long GC Pauses (>100ms)
- 👥 Thread Explosion (>100)
- ☠️ Zombie Threads (>5)
- 🎭 High Virtual Threads (>50%)

### 📊 Dashboard
- ✅ Real-time TUI (Text User Interface)
- ✅ Auto-refresh (2s)
- ✅ GC Type detection (G1GC, ZGC, Shenandoah, etc.)

### 💾 Export
- ✅ JSON exports automáticos
- ✅ Timestamped files

---

## 🎯 Exemplo de Dashboard

    ╔══════════════════════════════════════════════════════════════════════╗
    ║ 🕐 Sun Mar 29 23:16:32 2026
      🎯 JVMA v5.1 - Memory + Threads                        ║
    ║ JVM: /usr/java/jdk-21-oracle-x64/bin/java                    ║
    ║ PID: 80981 | ⏱️      5s | 💻 24.2% | 🧱 73.2% | 👥   77 ║
    ╚══════════════════════════════════════════════════════════════════════╝
    
    ╔══════════════════════════════════════════════════════════════════════╗
    ║ 💾 JVM MEMORY (Total: 3226 MB | 61.8% used)                        ║
    ╚══════════════════════════════════════════════════════════════════════╝
    
    🧱 HEAP (2048 MB | 73.2% used):
      🟢 EDEN:  611 MB (41% Heap) | 🟡 SURV:  161 MB (11%) | 🔴 OLD:  728 MB (49%)
    
    📦 NON-HEAP:
      💎 Metaspace:  128 MB | 🧵 Stacks:   77 MB | ⚙️ Code:   32 MB | 🌐 Direct:  256 MB
    
    ╔══════════════════════════════════════════════════════════════════════╗
    ║ 👥 THREADS ANALYSIS (Total: 77)                                      ║
    ╚══════════════════════════════════════════════════════════════════════╝
      TOTAL     :   77  |  VIRTUAL :    1  |  PLATFORM:   76
      DAEMON    :    0  |  ZOMBIE  :    0🚨 |  BLOCKED :    0🔒
      RUNNABLE  :    0🟢|  WAITING :    0💤
    
    🗑️ GC: ParallelGC | Pause: 28.1ms | 🔒 Locks: 0
    
    ✅ ALL SYSTEMS NOMINAL 🟢




---

## 🚀 Instalação e Uso

### Pré-requisitos


    Linux com /proc filesystem ativo
    GCC ou Clang
    JVM rodando (Java 8+)

## Compilação

    gcc -O2 -w -o jvm_analyzer jvm_analyzer.c -lm


## Uso

### 1. Encontre JVM PIDs
    jps -l
    
    12345 com.example.MyApp

### 2. Execute o monitor
    ./jvma 12345

### 3. Dashboard aparecerá com refresh automático a cada 2s
    Ctrl+C para sair


## 📁 Estrutura de Exportação JSON

    {
    "timestamp": 1733938645,
    "pid": 12345,
    "gc_type": "G1GC",
    "threads": {
        "total": 245,
        "virtual": 89,
        "platform": 156,
        "daemon": 123,
        "zombie": 2,
        "blocked": 7,
        "runnable": 34,
        "waiting": 202
    },
    "heap": {"total_mb": 2048.0},
    "jvm_total_mb": 4096.0,
    "alert_count": 0
    }

#### Arquivos salvos em:
    ./exports/jvma_[timestamp].json

## 🔍 Análise Técnica Detalhada
### 1. Thread Analysis Precisa
    // Lê /proc/[pid]/task/* para cada thread
    // Detecta Virtual Threads por padrões Java 21+ Loom
    int is_virtual_thread(const char* status_content) {
        return (strstr(status_content, "virtual") || 
                strstr(status_content, "loom") ||
                strstr(status_content, "VirtualThread"));
    }

#### Estados detectados:

    Z - Zombie threads (potencial memory leak)
    D - Blocked (lock contention)
    R - Runnable (CPU usage)
    S - Waiting/Sleeping

#### 2. Memory Regions Monitoradas
    HEAP: Eden + Survivor + OldGen
    NON-HEAP: Metaspace + Thread Stacks + Code Cache + Direct Memory
    TOTAL JVM: HEAP + NON-HEAP

#### 3. Alert Thresholds

| Alerta           | Threshold | Ícone |
| ---------------- | --------- | ----- |
| High CPU         | 85%       | 🔥    |
| Heap Exhaustion  | 92%       | 💥    |
| Lock Contention  | 8 blocked | 🔒    |
| Long GC          | 100ms     | ⏱️    |
| Thread Explosion | 100       | 👥    |
| Zombie Threads   | 5         | ☠️    |
| High Virtual     | 50%       | 🎭    |

---
## ⚙️ Configuração Avançada
### Variáveis Customizáveis
    static char export_dir[1024] = "./exports";  // Diretório de export
    // Refresh rate: sleep(2) = 2 segundos
    // Thresholds editáveis em update_metrics()

### Compilação com Flags
    # Produção
    gcc -O3 -DNDEBUG jvm_analyzer.c -o jvma

    # Debug
    gcc -g -Wall -Wextra -fsanitize=address jvm_analyzer.c -o jvma
