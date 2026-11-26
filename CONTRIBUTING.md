# Contribuindo

1. `make` compila; `make check` roda os testes; `make asan` roda com ASAN/UBSAN.
2. Mudanças de render alteram os hashes `ANSI_GOLDEN` em `tests/test_unit.c`;
   atualize-os apenas quando a mudança visual for intencional.
3. Sem dependências externas: apenas C padrão + `/proc`.
4. Abra uma issue antes de mudanças grandes. Relatos de segurança: use um
   security advisory privado do GitHub.
