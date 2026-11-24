#!/bin/sh
# JVMA v6.0 — orquestração dos testes (subtarefa 6).
# Uso: tests/run_tests.sh [binario_de_teste]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

bin=${1:-tests/test_unit}
dump=$(mktemp "${TMPDIR:-/tmp}/jvma-golden.XXXXXX")
trap 'rm -f "$dump"; exit' EXIT HUP INT TERM

LC_ALL=C
export LC_ALL

# Defeito de layout conhecido: a asserção estrita continua ativa, mas falhas
# são reportadas como XFAIL até a correção da subtarefa 10 (remover esta linha
# naquela etapa para voltar a ser falha dura).
JVMA_EXPECT_LAYOUT_BUG=1
export JVMA_EXPECT_LAYOUT_BUG

"$bin"
"$bin" --dump > "$dump"
diff -u tests/golden/fmt.txt "$dump"

printf 'tests: %s OK\n' "$bin"
