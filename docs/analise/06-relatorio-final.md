# 06 — Relatório final (JVMA)

## 1. Objetivo original e critérios de aceite
- **Objetivo:** evoluir o JVMA monolítico para base modular, corrigindo bugs/robustez e preservando contrato externo.
- **Aceite:** build/gates limpos (`make clean && make`, `make check`, ASAN), cobertura de testes (247 checks + 8 ANSI), exports JSON/CSV com schema/ordem de campos preservados (byte-idêntico no cenário comum sob `LC_ALL=C`; ressalva: escape e ponto decimal sob `pt_BR`), capturas ANSI nos 4 tamanhos, CLI 3 formatos, `Not found`/`invalid PID`, sinais, `-lm` única lib, sem alteração de comportamento fora do escopo.

## 2. O que mudou
- **Correções/robustez (docs/analise/05, itens 1–11):** hardening de export/CLI/input/layout/proc/alertas; mapeamento por subtarefas 8 (export), 9 (input/CLI/sinais), 10 (layout/buffer), 11 (Makefile/build), 15 (proc/alertas); decisões aplicadas sem quebrar schema.
- **Modularização 13–18:** extração do monólito para módulos em `src/` com `main` orquestrador; monólito `jvm_analyzer.c` removido (staged deletion).
- **Arquivos-chave:** `Makefile`, `src/main.c`, `src/jvma_types.h`, `src/{util,ring,proc,alerts,term,sample,export,discovery,render,gc_sim}.{c,h}`, `tests/run_tests.sh`, `tests/test_unit.c`, `tests/fixtures/proc_{stat,limits,smaps_rollup}.txt`, `docs/analise/01..06`.

## 3. Evidências
- **Build/gates:** `make clean && make` rc 0; `make check` rc 0; ASAN rc 0 (gates aprovados na subtarefa 18).
- **Testes:** `247` checks + `ansi 8` (golden/fmt) em `tests/run_tests.sh`.
- **stdin não-TTY (correção bloqueante):** `printf 'ecq' | ./jvma <PID> <DIR>` gera JSON válido e CSV com 21 colunas; `q` sai com rc 0; `</dev/null` não gera arquivos e mantém CPU ~0%; help retorna em EOF e em `SIGTERM`.
- **Contrato/golden:** JSON/CSV com schema/ordem de campos preservados; byte-idêntico no cenário comum sob `LC_ALL=C` (ressalva: escape e ponto decimal sob `pt_BR`); golden `fmt` preservado.
- **Capturas ANSI:** 4 tamanhos validados.
- **CLI/comportamento:** 3 formatos CLI; `Not found`/`invalid PID`; sinais tratados; exports JSON/CSV verificados.
- **Dependências:** `LDLIBS = -lm` (única lib no `Makefile`).

## 4. Decisões
- **GC simulado:** `--no-sim-gc` desativa por presença em qualquer `argv`; `JVMA_NO_SIM_GC` desativa apenas com valor exatamente `"1"` (ausente/vazio/`"0"`/outros não ativam). Default inalterado (simulação ativa). Ref: `src/gc_sim.c:29-36`.
- **Input:** modo raw restrito a TTY; leitura de teclas habilitada também para pipe/arquivo via `poll/read` (`POLLIN` precede `POLLHUP`, sem perder bytes), com desativação em EOF/HUP e sleep integral do restante do intervalo. PTY e sinais inalterados.
- **Performance:** parte aplicada conforme `docs/analise/04-performance.md`; propostas não aplicadas permanecem registradas (ex.: `strcat` em `ring.c`, `sysconf` por amostra, contagem/reabertura de threads por `task/*/status`, cadência de `smaps`). Contrato inalterado.
- **Deps:** zero dependências externas além de libc/libm; sem ncurses.
- **Contrato:** schema JSON/CSV e comportamento externo preservados.

## 5. Estado final do repo e recomendações não bloqueantes
- **Estado:** modularização concluída em `src/`; `jvm_analyzer.c` em deleção staged; testes/fixtures/docs de análise presentes; árvore com alterações não commitadas (`git status` mostra M/D/??).
- **Recomendações não bloqueantes:**
  - Wildcard de fontes sem ordenação explícita (`$(wildcard src/*.c)`); considerar ordem determinística se necessário.
  - README: alinhar texto legado e explicitar limiares `>` vs `>=`, `h` como toggle de ajuda e exemplo `daemon:45`; build atual está limpo (`make` rc 0).
  - Concluir/commit da staged deletion de `jvm_analyzer.c` e demais arquivos novos/modificados conforme fluxo de release.
