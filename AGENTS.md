# MM10 Recomp

- `WiiRecomp` and `robox-recompiled` are third-party clones; do not modify them.
- Record commands that worked here. Commit only after the corresponding step works.
- Source DOL: copy only from `C:\Projetos\MegaMan10-Recomp\decomp\dtk-template-main\orig\WRXE\main.dol`; never modify the original.

# Economia de limite (obrigatório)
- Nunca imprimir mais de 60 linhas de C gerado. Use `rg -n -m 5 -C 2` e leia trechos pequenos.
- Resumir saídas longas em tabelas curtas; salvar logs em build/ e citar só o caminho.
- Não reler arquivos já lidos nesta sessão nem repetir buscas.
- Um objetivo por sessão; ao terminar, commitar e parar.

# Estado do projeto (18/11/2026)
- Boot passa por OSInit, EXI, áudio (HLE), threads e DVDInit; MEM2/arena corrigidas.
- Parada atual: leitura VI em 0xCC00206E, LR 0x80019F2C (código do jogo).
- Armadilhas: o nome da função em ppc_trace fica desatualizado dentro da função; o BSS do DOL cobre data6/data7; os campos da memória baixa em 0x80003100+ importam.

- Rode cada comando git sozinho, sem ';', '&&' nem 'if ($LASTEXITCODE...)'. Assim o prefixo fica curto e as regras valem.
