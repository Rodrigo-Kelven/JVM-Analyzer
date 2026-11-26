#!/bin/sh
# JVMA v6.0 — orquestração dos testes (subtarefa 6 + regressão stdin não-TTY).
# Uso: tests/run_tests.sh [binario_de_teste]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

bin=${1:-tests/test_unit}
dump=$(mktemp "${TMPDIR:-/tmp}/jvma-golden.XXXXXX")
work=
empty=
trap 'rm -f "$dump"; [ -z "$work" ] || rm -rf "$work"; [ -z "$empty" ] || rm -rf "$empty"; exit' EXIT HUP INT TERM

LC_ALL=C
export LC_ALL

"$bin"
"$bin" --ansi
"$bin" --dump > "$dump"
diff -u tests/golden/fmt.txt "$dump"

# Integração: teclas via pipe/arquivo não exigem TTY (contrato 01-baseline.md:268).
if [ ! -x ./jvma ]; then
    ${MAKE:-make} -s jvma
fi

fail() {
    printf 'tests: integração stdin: %s\n' "$1" >&2
    exit 1
}

work=$(mktemp -d "${TMPDIR:-/tmp}/jvma-stdin.XXXXXX")
pid=$$

if ! printf 'ecq' | ./jvma "$pid" "$work" >/dev/null 2>/dev/null; then
    fail "printf 'ecq' | jvma rc != 0"
fi

json=
for f in "$work"/jvma_*.json; do
    if [ -f "$f" ]; then json=$f; break; fi
done
[ -n "$json" ] || fail "JSON não gerado via pipe"
grep -q '^{' "$json" || fail "JSON inválido (abertura)"
grep -q '^}$' "$json" || fail "JSON inválido (fechamento)"
grep -q '"jvma_version": "6.0"' "$json" || fail "JSON sem jvma_version"
grep -q '"pid": '"$pid"',' "$json" || fail "JSON com pid divergente"
grep -q '"cpu_pct":' "$json" || fail "JSON sem cpu_pct"
grep -q '"memory":' "$json" || fail "JSON sem memory"
grep -q '"threads":' "$json" || fail "JSON sem threads"
grep -q '"fds":' "$json" || fail "JSON sem fds"

csv="$work/jvma_$pid.csv"
[ -f "$csv" ] || fail "CSV não gerado via pipe"
awk -F, 'NR == 1 { h = NF } NR == 2 { d = NF } END { exit !(NR == 2 && h == 21 && d == 21) }' "$csv" \
    || fail "CSV sem 21 colunas"

# stdin vazio (</dev/null): sem exports e sem spin; SIGTERM encerra com rc 0.
empty=$(mktemp -d "${TMPDIR:-/tmp}/jvma-null.XXXXXX")
if ! timeout --preserve-status -s TERM 2 ./jvma "$pid" "$empty" </dev/null >/dev/null 2>/dev/null; then
    fail "jvma </dev/null não terminou com rc 0"
fi
[ -z "$(ls -A "$empty")" ] || fail "stdin vazio gerou arquivos"
rm -rf "$empty"
empty=

printf 'tests: %s OK\n' "$bin"
