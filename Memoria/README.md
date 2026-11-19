# mystdlib.h

Biblioteca de alocação de memória em C, em um único header, com **detecção de erros** voltada para aprendizado e depuração. Ela substitui `malloc`/`calloc`/`realloc`/`free` por funções em português que registram cada bloco e avisam quando algo dá errado.

## Uso

Basta incluir o header (não há `.c` para compilar nem biblioteca para linkar):

```c
#include "mystdlib.h"

int main(void) {
    int *v = calocar(5, sizeof(int));   /* zerado */
    v[0] = 42;
    v = realocar(v, 10 * sizeof(int));
    desalocar(v);
    return 0;
}
```

Ao final do programa é exibido um relatório automático:

```
[SISTEMA] Toda a memória alocada na heap foi liberada corretamente. (2 alocação(ões), 2 liberação(ões), pico de 40 bytes)
```

## Funções

### Alocação

| Função | Equivalente padrão | Observação |
|---|---|---|
| `alocar(tamanho)` | `malloc` | Memória preenchida com `0xCD` (não zerada) |
| `calocar(qtd, tamanho)` | `calloc` | Memória zerada; detecta estouro em `qtd * tamanho` |
| `realocar(ptr, tamanho)` | `realloc` | Cresce no lugar quando possível; `realocar(p, 0)` desaloca |
| `realocar_vetor(ptr, qtd, tamanho)` | `reallocarray` | Detecta estouro na multiplicação |
| `alocar_alinhado(&ptr, alinhamento, tamanho)` | `posix_memalign` | Retorna `0`, `EINVAL` ou `ENOMEM` |
| `alocar_alinhado_direto(alinhamento, tamanho)` | `aligned_alloc` | Retorna o ponteiro ou `NULL` |
| `desalocar(ptr)` / `liberar(ptr)` | `free` | Os dois nomes são equivalentes |
| `tamanho_bloco(ptr)` | `malloc_usable_size` | Retorna o tamanho pedido originalmente |

### Manipulação de memória

| Função | Equivalente padrão |
|---|---|
| `copiar_memoria(dest, orig, n)` | `memcpy` (se as regiões se sobrepõem, avisa e faz como `memmove`) |
| `mover_memoria(dest, orig, n)` | `memmove` |
| `preencher_memoria(dest, valor, n)` | `memset` |
| `comparar_memoria(a, b, n)` | `memcmp` |
| `procurar_memoria(ptr, valor, n)` | `memchr` |
| `duplicar_memoria(orig, n)` | — (aloca e copia) |
| `duplicar_texto(s)` / `duplicar_texto_n(s, n)` | `strdup` / `strndup` |
| `limpar_memoria_segura(dest, n)` | `explicit_bzero` (não é removida pelo otimizador) |

### Depuração

| Função | O que faz |
|---|---|
| `verificar_heap()` | Percorre todos os blocos e confere cabeçalhos, canários e regiões livres |
| `relatorio_memoria()` | Mostra bytes/blocos em uso, totais e pico |
| `mostrar_memoria(ptr, n)` | Hex dump de um bloco, com informação de onde foi alocado |
| `limitar_memoria(bytes)` | Simula uma heap com limite máximo (`0` desativa) |
| `falhar_apos(n)` | Faz a alocação de número `n + 1` falhar, para testar o tratamento de `NULL` (`-1` desativa) |

## Erros detectados

Todos os avisos saem em `stderr` com o arquivo e a linha da chamada:

- **Vazamento de memória**: no fim do programa, lista cada bloco não desalocado e onde foi alocado.
- **Escrita além do limite**: 8 bytes-canário (`0xFD`) após cada bloco, conferidos ao desalocar/realocar.
- **Uso após desalocar**: regiões livres são preenchidas com `0xDD`; se forem modificadas, o erro é apontado ao reutilizá-las ou em `verificar_heap()`.
- **Liberação dupla** e **ponteiro não alocado** passado a `desalocar`.
- **Cabeçalho corrompido** (escrita antes do início de um bloco).
- Alocação de **0 bytes**, ponteiros **NULL** e **estouro** no cálculo de tamanhos.

Os padrões de bytes facilitam identificar o estado da memória em um dump:

| Byte | Significado |
|---|---|
| `0xCD` | Recém-alocado, ainda não inicializado |
| `0xDD` | Livre (já desalocado) |
| `0xFD` | Canário (fim do bloco) |

## Como funciona

- Alocador *first-fit* com lista duplamente encadeada de blocos; cada bloco tem um cabeçalho com tamanho, estado, arquivo/linha e um número mágico.
- Blocos livres vizinhos são fundidos e blocos grandes são divididos.
- Memória obtida do sistema via `sbrk` (Linux/macOS), com `mmap` como alternativa, ou `HeapAlloc` no Windows.
- **Valgrind**: se `<valgrind/memcheck.h>` estiver disponível, os blocos são registrados no Memcheck, que passa a reportar leituras de memória não inicializada e acessos inválidos nos blocos desta biblioteca.
- **Windows**: o console é colocado em UTF-8 para exibir os acentos das mensagens, e restaurado na saída.

## Limitações

- **Não é thread-safe**: não use com várias threads alocando ao mesmo tempo.
- Todas as funções são `static`, então **cada arquivo `.c` que inclui o header tem sua própria heap**. Um ponteiro alocado em um `.c` e desalocado em outro será reportado como "não alocado". Em projetos com vários arquivos, concentre alocação e desalocação de um mesmo bloco no mesmo `.c`.
- Busca linear nos blocos: feita para depuração e estudo, não para desempenho.
- Não misture com `malloc`/`free` no mesmo ponteiro.
- Diferente de `free`, `desalocar(NULL)` emite um aviso.
