# MMIO e caminho de boot (Marco 14)

## 1. Funções distintas após `OSInit`

O ring foi zerado no retorno de `OSInit` (continuação em `0x80004174`) e terminou na leitura VI `0xCC00206E`, LR `0x80019F2C`. Foram observadas **93 funções distintas** até a parada; portanto, a janela contém menos de 200. `fn_...` indica ausência de símbolo atribuído. A coluna MMIO mostra atribuições da telemetria dinâmica, não inferências estáticas.

| # | Endereço | Nome | Região | Bloco MMIO observado |
|---:|---:|---|---|---|
| 1 | `0x80004040` | __get_debug_bba | fora da faixa | — |
| 2 | `0x801455C0` | __init_user | SDK | — |
| 3 | `0x801455E0` | __init_cpp | SDK | — |
| 4 | `0x80160340` | __init_cpp_exceptions | SDK | — |
| 5 | `0x801603B0` | __register_fragment | SDK | — |
| 6 | `0x80020050` | fn_80020050 | jogo | — |
| 7 | `0x8023DB80` | fn_8023DB80 | SDK | — |
| 8 | `0x80244850` | fn_80244850 | SDK | — |
| 9 | `0x80244F30` | fn_80244F30 | SDK | — |
| 10 | `0x80246D00` | fn_80246D00 | SDK | — |
| 11 | `0x8023B930` | fn_8023B930 | SDK | — |
| 12 | `0x8023BA20` | fn_8023BA20 | SDK | — |
| 13 | `0x80258970` | fn_80258970 | SDK | — |
| 14 | `0x8024A8A0` | fn_8024A8A0 | SDK | — |
| 15 | `0x8023CA10` | fn_8023CA10 | SDK | — |
| 16 | `0x8013F070` | fn_8013F070 | SDK | — |
| 17 | `0x80140C90` | OSInitThreadQueue | SDK | — |
| 18 | `0x8015FA2C` | fn_8015FA2C | SDK | — |
| 19 | `0x8001F5FC` | fn_8001F5FC | jogo | — |
| 20 | `0x80171930` | fn_80171930 | SDK | — |
| 21 | `0x80232670` | fn_80232670 | SDK | — |
| 22 | `0x80232810` | fn_80232810 | SDK | — |
| 23 | `0x802328D0` | fn_802328D0 | SDK | — |
| 24 | `0x802337D0` | fn_802337D0 | SDK | — |
| 25 | `0x8023A310` | fn_8023A310 | SDK | — |
| 26 | `0x80242E80` | fn_80242E80 | SDK | — |
| 27 | `0x8024C0C0` | fn_8024C0C0 | SDK | — |
| 28 | `0x80251D90` | fn_80251D90 | SDK | — |
| 29 | `0x80251290` | fn_80251290 | SDK | — |
| 30 | `0x80256A60` | fn_80256A60 | SDK | — |
| 31 | `0x8025D440` | fn_8025D440 | SDK | — |
| 32 | `0x8025D590` | fn_8025D590 | SDK | — |
| 33 | `0x8025D5A0` | fn_8025D5A0 | SDK | — |
| 34 | `0x80273F90` | fn_80273F90 | SDK | — |
| 35 | `0x802594F0` | fn_802594F0 | SDK | — |
| 36 | `0x8025B360` | fn_8025B360 | SDK | — |
| 37 | `0x802B0A70` | fn_802B0A70 | SDK | — |
| 38 | `0x802B6C20` | fn_802B6C20 | SDK | — |
| 39 | `0x802B7170` | fn_802B7170 | SDK | — |
| 40 | `0x802B8750` | fn_802B8750 | SDK | — |
| 41 | `0x802BAC50` | fn_802BAC50 | SDK | — |
| 42 | `0x802C3610` | fn_802C3610 | SDK | — |
| 43 | `0x802BD790` | fn_802BD790 | SDK | — |
| 44 | `0x802BDCF0` | fn_802BDCF0 | SDK | — |
| 45 | `0x8015F4B4` | __register_global_object | SDK | — |
| 46 | `0x8001C97C` | main | jogo | — |
| 47 | `0x8001EFF4` | fn_8001EFF4 | jogo | — |
| 48 | `0x80183720` | SCCheckStatus | SDK | — |
| 49 | `0x80185110` | fn_80185110 | SDK | — |
| 50 | `0x801851F0` | fn_801851F0 | SDK | — |
| 51 | `0x80185260` | SCGetLanguage | SDK | — |
| 52 | `0x80185A30` | fn_80185A30 | SDK | — |
| 53 | `0x801858B0` | fn_801858B0 | SDK | — |
| 54 | `0x801852E0` | fn_801852E0 | SDK | — |
| 55 | `0x801853A0` | fn_801853A0 | SDK | — |
| 56 | `0x801846F0` | fn_801846F0 | SDK | — |
| 57 | `0x801857A0` | fn_801857A0 | SDK | — |
| 58 | `0x801857F0` | fn_801857F0 | SDK | — |
| 59 | `0x801848B0` | fn_801848B0 | SDK | — |
| 60 | `0x8005F698` | fn_8005F698 | jogo | — |
| 61 | `0x801D75D0` | fn_801D75D0 | SDK | — |
| 62 | `0x801D6000` | ESP_InitLib | SDK | — |
| 63 | `0x8017DE70` | IOS_Open | SDK | — |
| 64 | `0x8017F2B0` | iosAllocAligned | SDK | — |
| 65 | `0x8017F0B0` | fn_8017F0B0 | SDK | — |
| 66 | `0x8013DE00` | OSDisableInterrupts | SDK | SI |
| 67 | `0x8013DE40` | OSRestoreInterrupts | SDK | — |
| 68 | `0x80138270` | OSRegisterVersion | SDK | — |
| 69 | `0x8013A110` | OSReport | SDK | — |
| 70 | `0x8016792C` | vprintf | SDK | — |
| 71 | `0x80166EF0` | __pformatter | SDK | — |
| 72 | `0x801657B8` | parse_format | SDK | — |
| 73 | `0x8015F3EC` | __va_arg | SDK | — |
| 74 | `0x80169508` | strchr | SDK | — |
| 75 | `0x8015F3D0` | strlen | SDK | — |
| 76 | `0x8016779C` | __FileWrite | SDK | — |
| 77 | `0x80164870` | __fwrite | SDK | — |
| 78 | `0x8016DCC4` | fwide | SDK | — |
| 79 | `0x801657A8` | __stdio_atexit | SDK | — |
| 80 | `0x801653CC` | __memrchr | SDK | — |
| 81 | `0x80004364` | memcpy | fora da faixa | — |
| 82 | `0x80164434` | __flush_buffer | SDK | — |
| 83 | `0x8016DD3C` | fn_8016DD3C | SDK | — |
| 84 | `0x80136F90` | OSGetConsoleType | SDK | — |
| 85 | `0x8017B980` | fn_8017B980 | SDK | — |
| 86 | `0x80178068` | fn_80178068 | SDK | — |
| 87 | `0x801799B8` | fn_801799B8 | SDK | — |
| 88 | `0x8001C070` | fn_8001C070 | jogo | — |
| 89 | `0x80138EC0` | OSSetMEM1ArenaLo | SDK | — |
| 90 | `0x80138ED0` | OSSetMEM2ArenaLo | SDK | — |
| 91 | `0x80019EB8` | fn_80019EB8 | jogo | — |
| 92 | `0x80019F04` | fn_80019F04 | jogo | — |
| 93 | `0x8013CD60` | fn_8013CD60 | SDK | VI |

## 2. Desmontagem resumida

A função com `0x80019F28` é `fn_80019F04`; o recorte termina no `bl` para `0x8013CD60`. A busca estática de chamadas encontrou um único chamador direto distinto, `main` (`0x8001C97C`), com dois call sites (`0x8001C9AC` e `0x8001C9BC`). Assim, não há três chamadores diretos para desmontar.

### `fn_80019F04` — `0x80019F04` (até `bl` em `0x80019F28)

- Cria frame de `0x20` bytes e salva LR e registradores não voláteis.
- Testa `r3`; só entra no ramo observado quando o argumento é zero.
- Nesse ramo, chama `fn_8013CD60` em `0x8013CD60`.
- `main` chama a função com `r3=0` e depois com `r3=1`, sugerindo configuração condicional.
- Antes do `bl`, não há acesso MMIO; a leitura VI ocorre dentro do chamado SDK.

### `main` — `0x8001C97C` (chamador direto)

- Executa inicializadores do jogo antes da sequência de vídeo/dispositivo.
- Chama `fn_8001C070` e `fn_80019EB8` antes da primeira chamada a `fn_80019F04`.
- Chama `fn_80019F04` duas vezes, com `r3=0` e `r3=1`.
- Entre essas chamadas aparecem `fn_8001C380`, `fn_8001CDE8` e `fn_800220D4`.
- A rotina envolve setup de subsistemas; o trecho não permite afirmar que seja PAD ou GX.

### `__start` — `0x80004050` (chama `main`)

- Entrada do DOL; prepara runtime e chama `OSInit` (`0x801378B0`).
- O marcador do ring foi posto imediatamente após o retorno de `OSInit`, no ponto `0x80004174`.
- Continua a inicialização do processo e alcança `main` (`0x8001C97C`).
- A parada reportada vem depois, na rotina chamada pelo `bl` em `0x80019F28`.
- Não é chamador direto de `fn_80019F04`; está um nível acima de `main`.

### Relação com a parada

- O trace e a pilha situam a leitura `0xCC00206E` no caminho `main` → `fn_80019F04` → `fn_8013CD60`.
- O LR `0x80019F2C` confirma o retorno do `bl` em `0x80019F28`.
- `fn_8013CD60` está na região SDK segundo o limite fornecido.
- Os dados observados são compatíveis com início de configuração de vídeo, mas não identificam chamadas PAD/GX antes da parada.
- Não foi inventado um terceiro chamador: as referências estáticas diretas apontam somente para `main`.

## 3. Varredura estática MMIO

Gerada por `python tools/find_hw.py private/main.dol --func-list games/megaman10/func_list.json`. O scanner acompanha padrões imediatos comuns (`lis/addis`, `addi`, `ori`) e acessos load/store com deslocamento; referências indiretas por sequências não reconhecidas podem faltar. Cada bloco tem uma tabela.

### CP

| Endereço da função | Nome | Bytes |
|---:|---|---:|
| — | — | — |

### PE

| Endereço da função | Nome | Bytes |
|---:|---|---:|
| — | — | — |

### VI

| Endereço da função | Nome | Bytes |
|---:|---|---:|
| `0x8013C170` | fn_8013C170 | 288 |
| `0x8013CD60` | fn_8013CD60 | 76 |
| `0x8013CDB0` | OSSetFontEncode | 120 |
| `0x8013D140` | fn_8013D140 | 280 |
| `0x8013D520` | fn_8013D520 | 648 |
| `0x8013DA20` | fn_8013DA20 | 480 |
| `0x8013DC00` | fn_8013DC00 | 328 |
| `0x8013DD50` | fn_8013DD50 | 176 |
| `0x80142D10` | fn_80142D10 | 124 |
| `0x80142D90` | __OSHotReset | 116 |
| `0x8014E7E0` | fn_8014E7E0 | 2060 |
| `0x8014F1B0` | fn_8014F1B0 | 512 |
| `0x8014F3B0` | fn_8014F3B0 | 1352 |
| `0x80150AF0` | fn_80150AF0 | 156 |
| `0x80150B90` | fn_80150B90 | 144 |
| `0x80150C80` | fn_80150C80 | 96 |
| `0x80150CE0` | fn_80150CE0 | 60 |
| `0x80174810` | SISetSamplingRate | 212 |

### PI

| Endereço da função | Nome | Bytes |
|---:|---|---:|
| `0x8013B500` | fn_8013B500 | 1876 |
| `0x8013DEA0` | __OSInterruptInit | 196 |
| `0x8013DF70` | SetInterruptMask | 604 |
| `0x8013E2D0` | fn_8013E2D0 | 680 |
| `0x8013FE70` | OSGetResetCode | 48 |
| `0x80143000` | __OSStateEventHandler | 340 |
| `0x8015ABF0` | fn_8015ABF0 | 356 |
| `0x8015AD60` | fn_8015AD60 | 436 |
| `0x801799E4` | fn_801799E4 | 36 |

### DSP

| Endereço da função | Nome | Bytes |
|---:|---|---:|
| `0x80139150` | __OSInitAudioSystem | 464 |
| `0x80139320` | __OSStopAudioSystem | 208 |
| `0x8013A560` | __OSUnhandledException | 764 |
| `0x8013DF70` | SetInterruptMask | 604 |
| `0x8013E2D0` | fn_8013E2D0 | 680 |
| `0x80153190` | fn_80153190 | 124 |
| `0x80153210` | fn_80153210 | 20 |
| `0x80153230` | fn_80153230 | 16 |
| `0x80153240` | fn_80153240 | 24 |
| `0x80153260` | fn_80153260 | 16 |
| `0x80153400` | fn_80153400 | 168 |
| `0x80157960` | fn_80157960 | 16 |
| `0x80157970` | fn_80157970 | 16 |
| `0x80157980` | fn_80157980 | 20 |
| `0x801579A0` | fn_801579A0 | 20 |
| `0x801579C0` | fn_801579C0 | 192 |
| `0x80157B00` | fn_80157B00 | 188 |
| `0x80157C10` | fn_80157C10 | 1068 |

### AI

| Endereço da função | Nome | Bytes |
|---:|---|---:|
| — | — | — |

### GX FIFO

| Endereço da função | Nome | Bytes |
|---:|---|---:|
| `0x8001A1D8` | fn_8001A1D8 | 236 |
| `0x8001A55C` | fn_8001A55C | 288 |
| `0x8001BE60` | fn_8001BE60 | 528 |
| `0x800210A4` | fn_800210A4 | 540 |
| `0x80021A34` | fn_80021A34 | 284 |
| `0x80021B50` | fn_80021B50 | 652 |
| `0x80021FA8` | fn_80021FA8 | 240 |
| `0x800227A0` | fn_800227A0 | 280 |
| `0x801585D0` | fn_801585D0 | 368 |
| `0x80158740` | __GXInitRevisionBits | 304 |
| `0x80158870` | GXInit | 1284 |
| `0x8015A290` | fn_8015A290 | 176 |
| `0x8015A820` | fn_8015A820 | 128 |
| `0x8015A8A0` | fn_8015A8A0 | 64 |
| `0x8015A8E0` | fn_8015A8E0 | 16 |
| `0x8015AD60` | fn_8015AD60 | 436 |
| `0x8015AF20` | fn_8015AF20 | 204 |
| `0x8015AFF0` | fn_8015AFF0 | 36 |
| `0x8015B370` | fn_8015B370 | 640 |
| `0x8015B5F0` | fn_8015B5F0 | 332 |
| `0x8015B740` | fn_8015B740 | 52 |
| `0x8015B780` | fn_8015B780 | 52 |
| `0x8015B7C0` | fn_8015B7C0 | 60 |
| `0x8015B830` | fn_8015B830 | 60 |
| `0x8015B870` | fn_8015B870 | 36 |
| `0x8015BCE0` | fn_8015BCE0 | 204 |
| `0x8015BDB0` | fn_8015BDB0 | 120 |
| `0x8015BE30` | fn_8015BE30 | 484 |
| `0x8015C040` | fn_8015C040 | 316 |
| `0x8015C180` | fn_8015C180 | 344 |
| `0x8015C2E0` | fn_8015C2E0 | 56 |
| `0x8015CBF0` | fn_8015CBF0 | 356 |
| `0x8015CDF0` | fn_8015CDF0 | 148 |
| `0x8015CF60` | fn_8015CF60 | 72 |
| `0x8015CFF0` | fn_8015CFF0 | 144 |
| `0x8015D1F0` | __GXSetTmemConfig | 848 |
| `0x8015D540` | fn_8015D540 | 104 |
| `0x8015D5B0` | fn_8015D5B0 | 320 |
| `0x8015D6F0` | fn_8015D6F0 | 256 |
| `0x8015D7F0` | fn_8015D7F0 | 200 |
| `0x8015D940` | __GXSetIndirectMask | 48 |
| `0x8015D970` | __GXFlushTextureState | 36 |
| `0x8015D9A0` | fn_8015D9A0 | 148 |
| `0x8015DA40` | fn_8015DA40 | 64 |
| `0x8015DA80` | fn_8015DA80 | 64 |
| `0x8015DAC0` | fn_8015DAC0 | 88 |
| `0x8015DB20` | fn_8015DB20 | 88 |
| `0x8015DB80` | fn_8015DB80 | 96 |
| `0x8015DBE0` | fn_8015DBE0 | 100 |
| `0x8015DC50` | fn_8015DC50 | 92 |
| `0x8015DCB0` | fn_8015DCB0 | 80 |
| `0x8015DD00` | fn_8015DD00 | 80 |
| `0x8015DD50` | fn_8015DD50 | 60 |
| `0x8015DD90` | fn_8015DD90 | 124 |
| `0x8015DE10` | fn_8015DE10 | 56 |
| `0x8015DE50` | fn_8015DE50 | 132 |
| `0x8015DEE0` | fn_8015DEE0 | 348 |
| `0x8015E070` | fn_8015E070 | 556 |
| `0x8015E2A0` | fn_8015E2A0 | 272 |
| `0x8015E3B0` | fn_8015E3B0 | 80 |
| `0x8015E400` | fn_8015E400 | 44 |
| `0x8015E430` | fn_8015E430 | 44 |
| `0x8015E460` | fn_8015E460 | 52 |
| `0x8015E4A0` | fn_8015E4A0 | 48 |
| `0x8015E4D0` | fn_8015E4D0 | 168 |
| `0x8015E580` | fn_8015E580 | 44 |
| `0x8015E5B0` | fn_8015E5B0 | 48 |
| `0x8015E5E0` | fn_8015E5E0 | 52 |
| `0x8015E620` | fn_8015E620 | 112 |
| `0x8015E690` | fn_8015E690 | 68 |
| `0x8015E7A0` | fn_8015E7A0 | 80 |
| `0x8015E7F0` | fn_8015E7F0 | 88 |
| `0x8015E870` | fn_8015E870 | 180 |
| `0x8015EA30` | fn_8015EA30 | 104 |
| `0x8015EAA0` | fn_8015EAA0 | 60 |
| `0x8015EAE0` | fn_8015EAE0 | 40 |
| `0x8015EB10` | fn_8015EB10 | 136 |
| `0x8015EBA0` | fn_8015EBA0 | 2076 |
| `0x8025FA10` | fn_8025FA10 | 1452 |
| `0x802A2160` | fn_802A2160 | 452 |
| `0x802B0FA0` | fn_802B0FA0 | 1376 |
| `0x802BC530` | fn_802BC530 | 644 |

### DI

| Endereço da função | Nome | Bytes |
|---:|---|---:|
| `0x80138260` | fn_80138260 | 16 |
| `0x8013A560` | __OSUnhandledException | 764 |

### SI

| Endereço da função | Nome | Bytes |
|---:|---|---:|
| `0x801737D0` | fn_801737D0 | 756 |
| `0x80173AD0` | SIInterruptHandler | 1004 |
| `0x80173EC0` | SIInit | 192 |
| `0x80173F80` | fn_80173F80 | 432 |
| `0x80174130` | SISetXY | 92 |

### EXI

| Endereço da função | Nome | Bytes |
|---:|---|---:|
| `0x8013C290` | OSFatal | 488 |
| `0x8013DF70` | SetInterruptMask | 604 |
| `0x8013E2D0` | fn_8013E2D0 | 680 |
| `0x80153280` | fn_80153280 | 384 |
| `0x80153520` | fn_80153520 | 460 |
| `0x80172B70` | EXIInit | 452 |
| `0x80179CF4` | fn_80179CF4 | 744 |
| `0x80179FDC` | fn_80179FDC | 188 |
| `0x8017A098` | fn_8017A098 | 292 |
| `0x8017A1BC` | fn_8017A1BC | 260 |
| `0x8017A2C0` | fn_8017A2C0 | 224 |
| `0x8017A3A0` | fn_8017A3A0 | 224 |

## Reprodução

```powershell
cmake --build build --parallel 4
.\build\mm10.exe 2>&1 | Tee-Object -FilePath build\marco14-run-03.log
rg -n -g 'funcs_*.c' 'func_80019f04\(\);' runtime/src/generated
python tools\find_hw.py private\main.dol --func-list games\megaman10\func_list.json
```

Trace completo: `build/marco14-run-03.log`. Saída estática: `build/marco14-static.md`.
