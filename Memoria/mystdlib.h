#ifndef MYSTDLIB_H
#define MYSTDLIB_H

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__GNUC__) || defined(__clang__)
#define MEMORIA_FUNCAO static __attribute__((unused))
#else
#define MEMORIA_FUNCAO static
#endif

#if defined(_WIN32)
#include <windows.h>

static UINT memoria_pagina_original_ = 0;
static int memoria_utf8_ativo_ = 0;

MEMORIA_FUNCAO void memoria_restaurar_terminal_(void)
{
    if(memoria_utf8_ativo_) {
        fflush(stdout);
        fflush(stderr);
        SetConsoleOutputCP(memoria_pagina_original_);
        memoria_utf8_ativo_ = 0;
    }
}

MEMORIA_FUNCAO void memoria_ativar_utf8_(void)
{
    static int registrado = 0;

    if(memoria_utf8_ativo_) {
        return;
    }
    memoria_pagina_original_ = GetConsoleOutputCP();
    if(memoria_pagina_original_ == CP_UTF8) {
        return;
    }
    if(SetConsoleOutputCP(CP_UTF8)) {
        memoria_utf8_ativo_ = 1;
        if(!registrado) {
            atexit(memoria_restaurar_terminal_);
            registrado = 1;
        }
    }
}
#else
#include <unistd.h>
#include <sys/mman.h>
#if !defined(__APPLE__)
extern void *sbrk(intptr_t);
#endif

#if defined(__has_include)
#if __has_include(<valgrind/memcheck.h>)
#include <valgrind/memcheck.h>
#define MEMORIA_VALGRIND 1
#endif
#endif

#if defined(MEMORIA_VALGRIND)
#define MEMORIA_VG_ALOCAR(p, n) VALGRIND_MALLOCLIKE_BLOCK((p), (n), 0, 0)
#define MEMORIA_VG_DESALOCAR(p) VALGRIND_FREELIKE_BLOCK((p), 0)
#define MEMORIA_VG_REDIMENSIONAR(p, antigo, novo) VALGRIND_RESIZEINPLACE_BLOCK((p), (antigo), (novo), 0)
#define MEMORIA_VG_ACESSIVEL(p, n) ((void)VALGRIND_MAKE_MEM_DEFINED((p), (n)))
#define MEMORIA_VG_INACESSIVEL(p, n) ((void)VALGRIND_MAKE_MEM_NOACCESS((p), (n)))
#define MEMORIA_VG_INDEFINIDO(p, n) ((void)VALGRIND_MAKE_MEM_UNDEFINED((p), (n)))
#define MEMORIA_VG_LER_BITS(p, bits, n) VALGRIND_GET_VBITS((p), (bits), (n))
#define MEMORIA_VG_GRAVAR_BITS(p, bits, n) ((void)VALGRIND_SET_VBITS((p), (bits), (n)))
#define MEMORIA_VG_ATIVO() (RUNNING_ON_VALGRIND)
#endif

typedef struct MemoriaSuperbloco {
    struct MemoriaSuperbloco *proximo;
    size_t tamanho;
} MemoriaSuperbloco;

static MemoriaSuperbloco *memoria_superblocos_ = NULL;

MEMORIA_FUNCAO void memoria_ativar_utf8_(void)
{
}
#endif

#if !defined(MEMORIA_VALGRIND)
#define MEMORIA_VG_ALOCAR(p, n) ((void)0)
#define MEMORIA_VG_DESALOCAR(p) ((void)0)
#define MEMORIA_VG_REDIMENSIONAR(p, antigo, novo) ((void)0)
#define MEMORIA_VG_ACESSIVEL(p, n) ((void)0)
#define MEMORIA_VG_INACESSIVEL(p, n) ((void)0)
#define MEMORIA_VG_INDEFINIDO(p, n) ((void)0)
#define MEMORIA_VG_LER_BITS(p, bits, n) ((void)(bits), (void)(n), 0)
#define MEMORIA_VG_GRAVAR_BITS(p, bits, n) ((void)(bits), (void)(n))
#define MEMORIA_VG_ATIVO() 0
#endif

#define MEMORIA_ALINHAMENTO 16
#define MEMORIA_ARREDONDAR(n) (((n) + (MEMORIA_ALINHAMENTO - 1)) & ~((size_t)MEMORIA_ALINHAMENTO - 1))

#define MEMORIA_MAGICO 0x4D454D4Fu
#define MEMORIA_CANARIO 8
#define MEMORIA_BYTE_CANARIO 0xFD
#define MEMORIA_BYTE_NOVO 0xCD
#define MEMORIA_BYTE_LIVRE 0xDD
#define MEMORIA_BLOCO_MAPEADO 65536

typedef struct MemoriaBloco {
    size_t tamanho;
    size_t solicitado;
    const char *arquivo;
    const char *arquivo_liberacao;
    struct MemoriaBloco *proximo;
    struct MemoriaBloco *anterior;
    int livre;
    int linha;
    int linha_liberacao;
    unsigned int magico;
} MemoriaBloco;

#define MEMORIA_CABECALHO MEMORIA_ARREDONDAR(sizeof(MemoriaBloco))
#if !defined(_WIN32)
#define MEMORIA_CABECALHO_SUPERBLOCO MEMORIA_ARREDONDAR(sizeof(MemoriaSuperbloco))
#endif
#define MEMORIA_LIMITE ((size_t)-1 - MEMORIA_CABECALHO - MEMORIA_ALINHAMENTO - MEMORIA_CANARIO)

static MemoriaBloco *memoria_inicio_ = NULL;
static MemoriaBloco *memoria_fim_ = NULL;
static size_t memoria_bytes_em_uso_ = 0;
static size_t memoria_blocos_em_uso_ = 0;
static size_t memoria_total_alocacoes_ = 0;
static size_t memoria_total_liberacoes_ = 0;
static size_t memoria_pico_ = 0;
static int memoria_registrado_ = 0;
static size_t memoria_limite_simulado_ = 0;
static long memoria_alocacoes_ate_falha_ = -1;

MEMORIA_FUNCAO void memoria_aviso_(const char *arquivo, int linha, const char *formato, ...)
{
    va_list args;
    memoria_ativar_utf8_();
    fprintf(stderr, "[SISTEMA] AVISO: ");
    va_start(args, formato);
    vfprintf(stderr, formato, args);
    va_end(args);
    if(arquivo) {
        fprintf(stderr, " (%s:%d)", arquivo, linha);
    }
    fputc('\n', stderr);
}

MEMORIA_FUNCAO void memoria_encher_(void *destino, unsigned char valor, size_t n)
{
    unsigned char *d = (unsigned char *)destino;

    while(n--) {
        *d++ = valor;
    }
}

MEMORIA_FUNCAO unsigned char *memoria_dados_(MemoriaBloco *b)
{
    return (unsigned char *)b + MEMORIA_CABECALHO;
}

MEMORIA_FUNCAO void memoria_escrever_canario_(MemoriaBloco *b)
{
    unsigned char *c = memoria_dados_(b) + b->solicitado;

    MEMORIA_VG_ACESSIVEL(c, MEMORIA_CANARIO);
    memoria_encher_(c, MEMORIA_BYTE_CANARIO, MEMORIA_CANARIO);
    MEMORIA_VG_INACESSIVEL(c, MEMORIA_CANARIO);
}

MEMORIA_FUNCAO int memoria_canario_intacto_(MemoriaBloco *b)
{
    unsigned char *c = memoria_dados_(b) + b->solicitado;
    int intacto = 1;
    size_t i;

    MEMORIA_VG_ACESSIVEL(c, MEMORIA_CANARIO);
    for(i = 0; i < MEMORIA_CANARIO; i++) {
        if(c[i] != MEMORIA_BYTE_CANARIO) {
            intacto = 0;
            break;
        }
    }
    MEMORIA_VG_INACESSIVEL(c, MEMORIA_CANARIO);
    return intacto;
}

MEMORIA_FUNCAO int memoria_conferir_canario_(MemoriaBloco *b, const char *arquivo, int linha)
{
    if(memoria_canario_intacto_(b)) {
        return 1;
    }
    memoria_aviso_(arquivo, linha, "Escrita além do limite: o bloco de %lu byte(s) em %p, alocado em %s:%d, foi ultrapassado.",
                    (unsigned long)b->solicitado, (void *)memoria_dados_(b),
                    b->arquivo ? b->arquivo : "?", b->linha);
    memoria_escrever_canario_(b);
    return 0;
}

MEMORIA_FUNCAO unsigned char *memoria_byte_alterado_(MemoriaBloco *b, size_t n)
{
    unsigned char *inicio = memoria_dados_(b);
    unsigned char *p = inicio;
    unsigned char *alterado = NULL;
    size_t i;

    if(n > b->tamanho) {
        n = b->tamanho;
    }
    MEMORIA_VG_ACESSIVEL(inicio, n);
    for(i = 0; i < n; i++, p++) {
        if(*p != MEMORIA_BYTE_LIVRE) {
            alterado = p;
            break;
        }
    }
    MEMORIA_VG_INACESSIVEL(inicio, n);
    return alterado;
}

MEMORIA_FUNCAO int memoria_conferir_veneno_(MemoriaBloco *b, size_t n, const char *arquivo, int linha)
{
    unsigned char *alterado = memoria_byte_alterado_(b, n);

    if(!alterado) {
        return 1;
    }
    memoria_aviso_(arquivo, linha, "Uso após desalocar: o byte %p, numa região já desalocada (último desalocar em %s:%d), "
                                    "foi modificado. Detectado ao reutilizar a região.",
                    (void *)alterado, b->arquivo_liberacao ? b->arquivo_liberacao : "?", b->linha_liberacao);
    return 0;
}

MEMORIA_FUNCAO int memoria_permitir_(size_t acrescimo, const char *arquivo, int linha)
{
    if(memoria_alocacoes_ate_falha_ == 0) {
        memoria_alocacoes_ate_falha_ = -1;
        memoria_aviso_(arquivo, linha, "Falha simulada por falhar_apos: o pedido de %lu byte(s) foi recusado.",
                        (unsigned long)acrescimo);
        return 0;
    }
    if(memoria_limite_simulado_ != 0 &&
       (acrescimo > memoria_limite_simulado_ || memoria_bytes_em_uso_ > memoria_limite_simulado_ - acrescimo)) {
        memoria_aviso_(arquivo, linha, "Limite simulado de %lu byte(s) atingido: %lu em uso, pedido de mais %lu.",
                        (unsigned long)memoria_limite_simulado_, (unsigned long)memoria_bytes_em_uso_,
                        (unsigned long)acrescimo);
        return 0;
    }
    if(memoria_alocacoes_ate_falha_ > 0) {
        memoria_alocacoes_ate_falha_--;
    }
    return 1;
}

MEMORIA_FUNCAO int memoria_verificar_heap_(int silencioso, const char *arquivo, int linha);

MEMORIA_FUNCAO void memoria_devolver_superblocos_(void)
{
#if !defined(_WIN32)
    MemoriaSuperbloco **s = &memoria_superblocos_;
    MemoriaSuperbloco *atual;
    MemoriaBloco *b;
    MemoriaBloco *prox;
    char *inicio;
    char *fim;
    int em_uso;

    while(*s) {
        atual = *s;
        inicio = (char *)atual;
        fim = inicio + atual->tamanho;
        em_uso = 0;
        for(b = memoria_inicio_; b; b = b->proximo) {
            if((char *)b >= inicio && (char *)b < fim && !b->livre) {
                em_uso = 1;
                break;
            }
        }
        if(em_uso) {
            s = &atual->proximo;
            continue;
        }
        for(b = memoria_inicio_; b; b = prox) {
            prox = b->proximo;
            if((char *)b >= inicio && (char *)b < fim) {
                if(b->anterior) {
                    b->anterior->proximo = b->proximo;
                } else {
                    memoria_inicio_ = b->proximo;
                }
                if(b->proximo) {
                    b->proximo->anterior = b->anterior;
                } else {
                    memoria_fim_ = b->anterior;
                }
            }
        }
        *s = atual->proximo;
        free(atual);
    }
#endif
}

MEMORIA_FUNCAO void memoria_relatorio_final_(void)
{
    MemoriaBloco *b;

    memoria_verificar_heap_(1, NULL, 0);

    if(memoria_blocos_em_uso_ == 0) {
        fprintf(stderr, "[SISTEMA] Toda a memória alocada na heap foi liberada corretamente. "
                        "(%lu alocação(ões), %lu liberação(ões), pico de %lu bytes)\n",
                (unsigned long)memoria_total_alocacoes_,
                (unsigned long)memoria_total_liberacoes_,
                (unsigned long)memoria_pico_);
        memoria_devolver_superblocos_();
        return;
    }

    fprintf(stderr, "\n[SISTEMA] AVISO: Você não liberou memória alocada na heap!\n");
    for(b = memoria_inicio_; b; b = b->proximo) {
        if(!b->livre) {
            fprintf(stderr, "[SISTEMA]   -> %lu byte(s) no endereço %p, alocado(s) em %s:%d\n",
                    (unsigned long)b->solicitado,
                    (void *)((char *)b + MEMORIA_CABECALHO),
                    b->arquivo ? b->arquivo : "?", b->linha);
        }
    }
    fprintf(stderr, "[SISTEMA] Total: %lu byte(s) em %lu bloco(s) sem desalocar.\n",
            (unsigned long)memoria_bytes_em_uso_,
            (unsigned long)memoria_blocos_em_uso_);
    memoria_devolver_superblocos_();
}

MEMORIA_FUNCAO void memoria_inicializar_(void)
{
    if(!memoria_registrado_) {
        memoria_ativar_utf8_();
        atexit(memoria_relatorio_final_);
        memoria_registrado_ = 1;
    }
}

#if !defined(_WIN32)
MEMORIA_FUNCAO void *memoria_mapear_(size_t *bytes)
{
#if defined(MAP_ANONYMOUS) || defined(MAP_ANON)
    size_t tamanho;
    void *p;

    if(*bytes > (size_t)-1 - MEMORIA_BLOCO_MAPEADO) {
        return NULL;
    }
    tamanho = (*bytes + MEMORIA_BLOCO_MAPEADO - 1) / MEMORIA_BLOCO_MAPEADO * MEMORIA_BLOCO_MAPEADO;
#if defined(MAP_ANONYMOUS)
    p = mmap(NULL, tamanho, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#else
    p = mmap(NULL, tamanho, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
#endif
    if(p == MAP_FAILED) {
        return NULL;
    }
    *bytes = tamanho;
    return p;
#else
    (void)bytes;
    return NULL;
#endif
}

MEMORIA_FUNCAO void *memoria_obter_superbloco_(size_t *bytes)
{
    MemoriaSuperbloco *s;
    size_t tamanho;

    if(*bytes > (size_t)-1 - MEMORIA_BLOCO_MAPEADO - MEMORIA_CABECALHO_SUPERBLOCO) {
        return NULL;
    }
    tamanho = (*bytes + MEMORIA_CABECALHO_SUPERBLOCO + MEMORIA_BLOCO_MAPEADO - 1)
              / MEMORIA_BLOCO_MAPEADO * MEMORIA_BLOCO_MAPEADO;
    s = (MemoriaSuperbloco *)malloc(tamanho);
    if(!s) {
        return NULL;
    }
    s->proximo = memoria_superblocos_;
    s->tamanho = tamanho;
    memoria_superblocos_ = s;
    *bytes = tamanho - MEMORIA_CABECALHO_SUPERBLOCO;
    return (char *)s + MEMORIA_CABECALHO_SUPERBLOCO;
}
#endif

MEMORIA_FUNCAO void *memoria_obter_do_sistema_(size_t *bytes)
{
#if defined(_WIN32)
    return HeapAlloc(GetProcessHeap(), 0, *bytes);
#else
    char *atual;
    char *p;
    size_t ajuste;

    if(MEMORIA_VG_ATIVO()) {
        return memoria_obter_superbloco_(bytes);
    }

    atual = (char *)sbrk(0);
    if(atual == (char *)-1) {
        return memoria_mapear_(bytes);
    }
    ajuste = (MEMORIA_ALINHAMENTO - ((uintptr_t)atual % MEMORIA_ALINHAMENTO)) % MEMORIA_ALINHAMENTO;
    if(*bytes > (size_t)INTPTR_MAX - ajuste) {
        return memoria_mapear_(bytes);
    }
    p = (char *)sbrk((intptr_t)(*bytes + ajuste));
    if(p == (char *)-1) {
        return memoria_mapear_(bytes);
    }
    return p + ajuste;
#endif
}

MEMORIA_FUNCAO int memoria_adjacentes_(MemoriaBloco *a, MemoriaBloco *b)
{
    return a && b && (char *)a + MEMORIA_CABECALHO + a->tamanho == (char *)b;
}

MEMORIA_FUNCAO void memoria_fundir_com_proximo_(MemoriaBloco *b)
{
    MemoriaBloco *p = b->proximo;
    b->tamanho += MEMORIA_CABECALHO + p->tamanho;
    b->proximo = p->proximo;
    if(p->proximo) {
        p->proximo->anterior = b;
    } else {
        memoria_fim_ = b;
    }
    memoria_encher_(p, MEMORIA_BYTE_LIVRE, MEMORIA_CABECALHO);
    MEMORIA_VG_INACESSIVEL(p, MEMORIA_CABECALHO);
}

MEMORIA_FUNCAO void memoria_dividir_(MemoriaBloco *b, size_t tamanho)
{
    MemoriaBloco *novo;

    if(b->tamanho < tamanho + MEMORIA_CABECALHO + MEMORIA_ALINHAMENTO) {
        return;
    }

    novo = (MemoriaBloco*)((char *)b + MEMORIA_CABECALHO + tamanho);
    MEMORIA_VG_ACESSIVEL(novo, MEMORIA_CABECALHO);
    novo->tamanho = b->tamanho - tamanho - MEMORIA_CABECALHO;
    novo->solicitado = 0;
    novo->livre = 1;
    novo->arquivo = NULL;
    novo->linha = 0;
    novo->arquivo_liberacao = b->livre ? b->arquivo_liberacao : NULL;
    novo->linha_liberacao = b->livre ? b->linha_liberacao : 0;
    novo->magico = MEMORIA_MAGICO;
    novo->anterior = b;
    novo->proximo = b->proximo;
    if(b->proximo) {
        b->proximo->anterior = novo;
    } else {
        memoria_fim_ = novo;
    }
    b->proximo = novo;
    b->tamanho = tamanho;

    if(!b->livre) {
        MEMORIA_VG_ACESSIVEL(memoria_dados_(novo), novo->tamanho);
        memoria_encher_(memoria_dados_(novo), MEMORIA_BYTE_LIVRE, novo->tamanho);
        MEMORIA_VG_INACESSIVEL(memoria_dados_(novo), novo->tamanho);
    }

    if(novo->proximo && novo->proximo->livre && memoria_adjacentes_(novo, novo->proximo)) {
        memoria_fundir_com_proximo_(novo);
    }
}

MEMORIA_FUNCAO MemoriaBloco *memoria_novo_bloco_(size_t tamanho)
{
    MemoriaBloco *b;
    size_t bytes = MEMORIA_CABECALHO + tamanho;

    b = (MemoriaBloco *)memoria_obter_do_sistema_(&bytes);
    if(!b) {
        return NULL;
    }
    tamanho = bytes - MEMORIA_CABECALHO;

    b->tamanho = tamanho;
    b->solicitado = 0;
    b->livre = 1;
    b->arquivo = NULL;
    b->linha = 0;
    b->arquivo_liberacao = NULL;
    b->linha_liberacao = 0;
    b->magico = MEMORIA_MAGICO;
    b->proximo = NULL;
    b->anterior = memoria_fim_;
    if(memoria_fim_) {
        memoria_fim_->proximo = b;
    } else {
        memoria_inicio_ = b;
    }
    memoria_fim_ = b;
    memoria_encher_(memoria_dados_(b), MEMORIA_BYTE_LIVRE, tamanho);
    MEMORIA_VG_INACESSIVEL(memoria_dados_(b), tamanho);
    return b;
}

MEMORIA_FUNCAO MemoriaBloco *memoria_localizar_(void *ptr)
{
    MemoriaBloco *b;
    for(b = memoria_inicio_; b; b = b->proximo) {
        if((char *)b + MEMORIA_CABECALHO == (char *)ptr) {
            return b;
        }
    }
    return NULL;
}

MEMORIA_FUNCAO void memoria_registrar_uso_(MemoriaBloco *b, size_t tamanho, const char *arquivo, int linha)
{
    memoria_bytes_em_uso_ = memoria_bytes_em_uso_ - b->solicitado + tamanho;
    b->solicitado = tamanho;
    b->arquivo = arquivo;
    b->linha = linha;
    if(memoria_bytes_em_uso_ > memoria_pico_) {
        memoria_pico_ = memoria_bytes_em_uso_;
    }
    memoria_escrever_canario_(b);
}

MEMORIA_FUNCAO MemoriaBloco *memoria_obter_bloco_livre_(size_t ajustado, const char *arquivo, int linha)
{
    MemoriaBloco *b;

    for(b = memoria_inicio_; b; b = b->proximo) {
        if(b->livre && b->tamanho >= ajustado) {
            memoria_conferir_veneno_(b, ajustado, arquivo, linha);
            return b;
        }
    }
    return memoria_novo_bloco_(ajustado);
}

MEMORIA_FUNCAO void memoria_marcar_em_uso_(MemoriaBloco *b, size_t tamanho, const char *arquivo, int linha)
{
    b->livre = 0;
    b->solicitado = 0;
    memoria_registrar_uso_(b, tamanho, arquivo, linha);
    memoria_blocos_em_uso_++;
    memoria_total_alocacoes_++;
}

MEMORIA_FUNCAO void memoria_entregar_(MemoriaBloco *b, size_t tamanho)
{
    MEMORIA_VG_ALOCAR(memoria_dados_(b), tamanho);
    memoria_encher_(memoria_dados_(b), MEMORIA_BYTE_NOVO, tamanho);
    MEMORIA_VG_INDEFINIDO(memoria_dados_(b), tamanho);
}

MEMORIA_FUNCAO void *memoria_alocar_base_(size_t tamanho, const char *arquivo, int linha, int checar_limites)
{
    MemoriaBloco *b;
    size_t ajustado;

    memoria_inicializar_();

    if(tamanho == 0) {
        memoria_aviso_(arquivo, linha, "Tentativa de alocar 0 bytes. Nenhuma memória foi alocada.");
        return NULL;
    }
    if(tamanho > MEMORIA_LIMITE) {
        memoria_aviso_(arquivo, linha, "Tamanho solicitado (%lu bytes) excede o limite da heap.",
                        (unsigned long)tamanho);
        return NULL;
    }
    if(checar_limites && !memoria_permitir_(tamanho, arquivo, linha)) {
        return NULL;
    }

    ajustado = MEMORIA_ARREDONDAR(tamanho + MEMORIA_CANARIO);

    b = memoria_obter_bloco_livre_(ajustado, arquivo, linha);
    if(!b) {
        memoria_aviso_(arquivo, linha, "Falha ao alocar %lu bytes: não há memória disponível na heap.",
                        (unsigned long)tamanho);
        return NULL;
    }

    memoria_dividir_(b, ajustado);
    memoria_marcar_em_uso_(b, tamanho, arquivo, linha);
    memoria_entregar_(b, tamanho);

    return memoria_dados_(b);
}

MEMORIA_FUNCAO void *memoria_alocar_(size_t tamanho, const char *arquivo, int linha)
{
    return memoria_alocar_base_(tamanho, arquivo, linha, 1);
}

MEMORIA_FUNCAO int memoria_alocar_alinhado_(void **resultado, size_t alinhamento, size_t tamanho,
                                           const char *arquivo, int linha)
{
    MemoriaBloco *b;
    MemoriaBloco *novo;
    size_t ajustado;
    size_t total;
    size_t original;
    uintptr_t inicio;
    uintptr_t alvo;
    void *p;

    memoria_inicializar_();

    if(!resultado) {
        memoria_aviso_(arquivo, linha, "alocar_alinhado recebeu um ponteiro de destino NULL.");
        return EINVAL;
    }
    if(alinhamento == 0 || (alinhamento & (alinhamento - 1)) != 0 || alinhamento % sizeof(void *) != 0) {
        memoria_aviso_(arquivo, linha, "Alinhamento inválido (%lu): deve ser potência de 2 múltiplo de %lu.",
                        (unsigned long)alinhamento, (unsigned long)sizeof(void *));
        return EINVAL;
    }
    if(tamanho == 0) {
        memoria_aviso_(arquivo, linha, "Tentativa de alocar 0 bytes. Nenhuma memória foi alocada.");
        *resultado = NULL;
        return 0;
    }

    if(alinhamento <= MEMORIA_ALINHAMENTO) {
        p = memoria_alocar_(tamanho, arquivo, linha);
        if(!p) {
            return ENOMEM;
        }
        *resultado = p;
        return 0;
    }

    ajustado = tamanho > MEMORIA_LIMITE ? (size_t)-1 : MEMORIA_ARREDONDAR(tamanho + MEMORIA_CANARIO);
    if(ajustado > MEMORIA_LIMITE - MEMORIA_CABECALHO || alinhamento > MEMORIA_LIMITE - MEMORIA_CABECALHO - ajustado) {
        memoria_aviso_(arquivo, linha, "Tamanho (%lu) e alinhamento (%lu) solicitados excedem o limite da heap.",
                        (unsigned long)tamanho, (unsigned long)alinhamento);
        return ENOMEM;
    }
    if(!memoria_permitir_(tamanho, arquivo, linha)) {
        return ENOMEM;
    }

    total = ajustado + alinhamento + MEMORIA_CABECALHO + MEMORIA_ALINHAMENTO;

    b = memoria_obter_bloco_livre_(total, arquivo, linha);
    if(!b) {
        memoria_aviso_(arquivo, linha, "Falha ao alocar %lu bytes alinhados em %lu: não há memória disponível na heap.",
                        (unsigned long)tamanho, (unsigned long)alinhamento);
        return ENOMEM;
    }

    inicio = (uintptr_t)b + MEMORIA_CABECALHO;

    if(inicio % alinhamento == 0) {
        novo = b;
    } else {
        alvo = (inicio + MEMORIA_CABECALHO + MEMORIA_ALINHAMENTO + (alinhamento - 1)) & ~((uintptr_t)alinhamento - 1);
        original = b->tamanho;

        novo = (MemoriaBloco *)(alvo - MEMORIA_CABECALHO);
        b->tamanho = (size_t)((uintptr_t)novo - inicio);
        MEMORIA_VG_ACESSIVEL(novo, MEMORIA_CABECALHO);

        novo->tamanho = original - b->tamanho - MEMORIA_CABECALHO;
        novo->solicitado = 0;
        novo->livre = 1;
        novo->arquivo = NULL;
        novo->linha = 0;
        novo->arquivo_liberacao = b->arquivo_liberacao;
        novo->linha_liberacao = b->linha_liberacao;
        novo->magico = MEMORIA_MAGICO;
        novo->anterior = b;
        novo->proximo = b->proximo;
        if(b->proximo) {
            b->proximo->anterior = novo;
        } else {
            memoria_fim_ = novo;
        }
        b->proximo = novo;

        if(b->anterior && b->anterior->livre && memoria_adjacentes_(b->anterior, b)) {
            memoria_fundir_com_proximo_(b->anterior);
        }
    }

    memoria_dividir_(novo, ajustado);
    memoria_marcar_em_uso_(novo, tamanho, arquivo, linha);
    memoria_entregar_(novo, tamanho);

    *resultado = memoria_dados_(novo);
    return 0;
}

MEMORIA_FUNCAO void *memoria_preencher_(void *destino, int valor, size_t n, const char *arquivo, int linha);
MEMORIA_FUNCAO void *memoria_copiar_(void *destino, const void *origem, size_t n, const char *arquivo, int linha);

MEMORIA_FUNCAO void *memoria_calocar_(size_t quantidade, size_t tamanho, const char *arquivo, int linha)
{
    void *p;

    if(quantidade != 0 && tamanho > (size_t)-1 / quantidade) {
        memoria_aviso_(arquivo, linha, "Estouro no cálculo de calocar(%lu, %lu). Nenhuma memória foi alocada.",
                        (unsigned long)quantidade, (unsigned long)tamanho);
        return NULL;
    }

    p = memoria_alocar_(quantidade * tamanho, arquivo, linha);
    if(p) {
        memoria_preencher_(p, 0, quantidade * tamanho, arquivo, linha);
    }
    return p;
}

MEMORIA_FUNCAO void memoria_liberar_(void *ptr, const char *arquivo, int linha)
{
    MemoriaBloco *b;

    if(!ptr) {
        memoria_aviso_(arquivo, linha, "Tentativa de desalocar um ponteiro NULL.");
        return;
    }

    b = memoria_localizar_(ptr);
    if(!b) {
        for(b = memoria_inicio_; b; b = b->proximo) {
            if(b->livre && (char *)ptr > (char *)b &&
                (char *)ptr < (char *)b + MEMORIA_CABECALHO + b->tamanho) {
                memoria_aviso_(arquivo, linha, "Liberação dupla detectada: o endereço %p já havia sido desalocado.", ptr);
                MEMORIA_VG_DESALOCAR(ptr);
                return;
            }
        }
        memoria_aviso_(arquivo, linha, "O endereço %p não foi alocado com alocar/calocar/realocar.", ptr);
        MEMORIA_VG_DESALOCAR(ptr);
        return;
    }
    if(b->livre) {
        memoria_aviso_(arquivo, linha, "Liberação dupla detectada: o endereço %p já havia sido desalocado.", ptr);
        MEMORIA_VG_DESALOCAR(ptr);
        return;
    }

    memoria_conferir_canario_(b, arquivo, linha);

    memoria_bytes_em_uso_ -= b->solicitado;
    memoria_blocos_em_uso_--;
    memoria_total_liberacoes_++;

    MEMORIA_VG_DESALOCAR(ptr);

    b->livre = 1;
    b->solicitado = 0;
    b->arquivo = NULL;
    b->linha = 0;
    MEMORIA_VG_ACESSIVEL(memoria_dados_(b), b->tamanho);
    memoria_encher_(memoria_dados_(b), MEMORIA_BYTE_LIVRE, b->tamanho);
    MEMORIA_VG_INACESSIVEL(memoria_dados_(b), b->tamanho);

    if(b->proximo && b->proximo->livre && memoria_adjacentes_(b, b->proximo)) {
        memoria_fundir_com_proximo_(b);
    }
    if(b->anterior && b->anterior->livre && memoria_adjacentes_(b->anterior, b)) {
        b = b->anterior;
        memoria_fundir_com_proximo_(b);
    }

    b->arquivo_liberacao = arquivo;
    b->linha_liberacao = linha;
}

MEMORIA_FUNCAO void *memoria_realocar_(void *ptr, size_t tamanho, const char *arquivo, int linha)
{
    MemoriaBloco *b;
    size_t ajustado;
    void *novo;

    if(!ptr) {
        return memoria_alocar_(tamanho, arquivo, linha);
    }
    if(tamanho == 0) {
        memoria_liberar_(ptr, arquivo, linha);
        return NULL;
    }

    b = memoria_localizar_(ptr);
    if(!b || b->livre) {
        memoria_aviso_(arquivo, linha, "realocar recebeu um endereço inválido ou já desalocado (%p).", ptr);
        return NULL;
    }
    if(tamanho > MEMORIA_LIMITE) {
        memoria_aviso_(arquivo, linha, "Tamanho solicitado (%lu bytes) excede o limite da heap.",
                        (unsigned long)tamanho);
        return NULL;
    }

    memoria_conferir_canario_(b, arquivo, linha);

    if(tamanho > b->solicitado && !memoria_permitir_(tamanho - b->solicitado, arquivo, linha)) {
        return NULL;
    }

    ajustado = MEMORIA_ARREDONDAR(tamanho + MEMORIA_CANARIO);

    if(b->tamanho >= ajustado) {
        MEMORIA_VG_REDIMENSIONAR(ptr, b->solicitado, tamanho);
        memoria_dividir_(b, ajustado);
        if(tamanho > b->solicitado) {
            memoria_encher_(memoria_dados_(b) + b->solicitado, MEMORIA_BYTE_NOVO, tamanho - b->solicitado);
            MEMORIA_VG_INDEFINIDO(memoria_dados_(b) + b->solicitado, tamanho - b->solicitado);
        }
        memoria_registrar_uso_(b, tamanho, arquivo, linha);
        return ptr;
    }

    if(b->proximo && b->proximo->livre && memoria_adjacentes_(b, b->proximo) &&
       b->tamanho + MEMORIA_CABECALHO + b->proximo->tamanho >= ajustado) {
       memoria_fundir_com_proximo_(b);
       MEMORIA_VG_REDIMENSIONAR(ptr, b->solicitado, tamanho);
       memoria_dividir_(b, ajustado);
       memoria_encher_(memoria_dados_(b) + b->solicitado, MEMORIA_BYTE_NOVO, tamanho - b->solicitado);
       MEMORIA_VG_INDEFINIDO(memoria_dados_(b) + b->solicitado, tamanho - b->solicitado);
       memoria_registrar_uso_(b, tamanho, arquivo, linha);
       return ptr;
    }

    novo = memoria_alocar_base_(tamanho, arquivo, linha, 0);
    if(!novo) {
        return NULL;
    }
    memoria_copiar_(novo, ptr, b->solicitado, arquivo, linha);
    memoria_liberar_(ptr, arquivo, linha);
    return novo;
}

MEMORIA_FUNCAO int memoria_sobrepostos_(const void *a, const void *b, size_t n)
{
    uintptr_t x = (uintptr_t)a;
    uintptr_t y = (uintptr_t)b;

    if(x < y) {
        return y - x < n;
    }
    return x - y < n;
}

MEMORIA_FUNCAO void *memoria_mover_(void *destino, const void *origem, size_t n, const char *arquivo, int linha)
{
    unsigned char *d = (unsigned char *)destino;
    const unsigned char *o = (const unsigned char *)origem;

    if(n == 0) {
        return destino;
    }
    if(!destino || !origem) {
        memoria_aviso_(arquivo, linha, "mover_memoria recebeu um ponteiro NULL (destino: %p, origem: %p).",
                        destino, (void *)origem);
        return destino;
    }
    if(d == o) {
        return destino;
    }

    if((uintptr_t)d < (uintptr_t)o) {
        while(n--) {
            *d++ = *o++;
        }
    } else {
        d += n;
        o += n;
        while(n--) {
            *--d = *--o;
        }
    }
    return destino;
}

MEMORIA_FUNCAO void *memoria_copiar_(void *destino, const void *origem, size_t n, const char *arquivo, int linha)
{
    unsigned char *d = (unsigned char *)destino;
    const unsigned char *o = (const unsigned char *)origem;

    if(n == 0) {
        return destino;
    }
    if(!destino || !origem) {
        memoria_aviso_(arquivo, linha, "copiar_memoria recebeu um ponteiro NULL (destino: %p, origem: %p).",
                        destino, (void *)origem);
        return destino;
    }
    if(memoria_sobrepostos_(destino, origem, n)) {
        memoria_aviso_(arquivo, linha, "copiar_memoria recebeu regiões sobrepostas (%p e %p, %lu bytes). "
                                        "A cópia foi feita como mover_memoria.",
                        destino, (void *)origem, (unsigned long)n);
        return memoria_mover_(destino, origem, n, arquivo, linha);
    }

    while(n--) {
        *d++ = *o++;
    }
    return destino;
}

MEMORIA_FUNCAO void *memoria_preencher_(void *destino, int valor, size_t n, const char *arquivo, int linha)
{
    unsigned char *d = (unsigned char *)destino;
    unsigned char v = (unsigned char)valor;

    if(n == 0) {
        return destino;
    }
    if(!destino) {
        memoria_aviso_(arquivo, linha, "preencher_memoria recebeu um ponteiro NULL.");
        return destino;
    }

    while(n--) {
        *d++ = v;
    }
    return destino;
}

MEMORIA_FUNCAO int memoria_comparar_(const void *a, const void *b, size_t n, const char *arquivo, int linha)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;

    if(n == 0) {
        return 0;
    }
    if(!a || !b) {
        memoria_aviso_(arquivo, linha, "comparar_memoria recebeu um ponteiro NULL (%p, %p)",
                        (void *)a, (void *)b);
        return 0;
    }

    for(; n; n--, x++, y++) {
        if(*x != *y) {
            return (int)*x - (int)*y;
        }
    }
    return 0;
}

MEMORIA_FUNCAO void *memoria_procurar_(const void *ptr, int valor, size_t n, const char *arquivo, int linha)
{
    const unsigned char *p = (const unsigned char *)ptr;
    unsigned char v = (unsigned char)valor;

    if(n == 0) {
        return NULL;
    }
    if(!ptr) {
        memoria_aviso_(arquivo, linha, "procurar_memoria recebeu um ponteiro NULL.");
        return NULL;
    }

    for(; n; n--, p++) {
        if(*p == v) {
            return (void*)p;
        }
    }
    return NULL;
}

MEMORIA_FUNCAO void *memoria_duplicar_(const void *origem, size_t n, const char *arquivo, int linha)
{
    void *copia;

    if(!origem) {
        memoria_aviso_(arquivo, linha, "duplicar_memoria recebeu um ponteiro NULL.");
        return NULL;
    }

    copia = memoria_alocar_(n, arquivo, linha);
    if(copia) {
        memoria_copiar_(copia, origem, n, arquivo, linha);
    }
    return copia;
}

MEMORIA_FUNCAO char *memoria_duplicar_texto_n_(const char *texto, size_t n, const char *arquivo, int linha)
{
    size_t tamanho = 0;
    char *copia;

    if(!texto) {
        memoria_aviso_(arquivo, linha, "Tentativa de duplicar um texto NULL.");
        return NULL;
    }

    while(tamanho < n && texto[tamanho] != '\0') {
        tamanho++;
    }

    copia = (char *)memoria_alocar_(tamanho + 1, arquivo, linha);
    if(!copia) {
        return NULL;
    }
    memoria_copiar_(copia, texto, tamanho, arquivo, linha);
    copia[tamanho] = '\0';
    return copia;
}

MEMORIA_FUNCAO char *memoria_duplicar_texto_(const char *texto, const char *arquivo, int linha)
{
    return memoria_duplicar_texto_n_(texto, (size_t)-1, arquivo, linha);
}

MEMORIA_FUNCAO void memoria_limpar_segura_(void *destino, size_t n, const char *arquivo, int linha)
{
    volatile unsigned char *d = (volatile unsigned char *)destino;

    if(n == 0) {
        return;
    }
    if(!destino) {
        memoria_aviso_(arquivo, linha, "limpar_memoria_segura recebeu um ponteiro NULL.");
        return;
    }

    while(n--) {
        *d++ = 0;
    }
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" : : "r"(destino) : "memory");
#endif
}

MEMORIA_FUNCAO void *memoria_realocar_vetor_(void *ptr, size_t quantidade, size_t tamanho, const char *arquivo, int linha)
{
    if(quantidade != 0 && tamanho > (size_t)-1 / quantidade) {
        memoria_aviso_(arquivo, linha, "Estouro no cálculo de realocar_vetor(%p, %lu, %lu). O bloco original não foi alterado.",
                        ptr, (unsigned long)quantidade, (unsigned long)tamanho);
        return NULL;
    }
    return memoria_realocar_(ptr, quantidade * tamanho, arquivo, linha);
}

MEMORIA_FUNCAO size_t memoria_tamanho_bloco_(const void *ptr, const char *arquivo, int linha)
{
    MemoriaBloco *b;

    if(!ptr) {
        memoria_aviso_(arquivo, linha, "tamanho_bloco recebeu um ponteiro NULL.");
        return 0;
    }

    b = memoria_localizar_((void *)ptr);
    if(!b || b->livre) {
        memoria_aviso_(arquivo, linha, "tamanho_bloco recebeu um endereço inválido ou já desalocado (%p).", (void *)ptr);
        return 0;
    }
    return b->solicitado;
}

MEMORIA_FUNCAO void *memoria_alocar_alinhado_direto_(size_t alinhamento, size_t tamanho, const char *arquivo, int linha)
{
    void *p = NULL;

    if(memoria_alocar_alinhado_(&p, alinhamento, tamanho, arquivo, linha) != 0) {
        return NULL;
    }
    return p;
}

MEMORIA_FUNCAO int memoria_verificar_heap_(int silencioso, const char *arquivo, int linha)
{
    MemoriaBloco *b;
    MemoriaBloco *anterior = NULL;
    unsigned char *alterado;
    size_t bytes = 0;
    size_t blocos = 0;
    size_t total = 0;
    int problemas = 0;

    for(b = memoria_inicio_; b; anterior = b, b = b->proximo) {
        total++;
        if(b->magico != MEMORIA_MAGICO) {
            memoria_aviso_(arquivo, linha, "verificar_heap: cabeçalho corrompido no bloco %p (escrita antes do início "
                                            "de um bloco ou ponteiro inválido). A verificação foi interrompida.",
                            (void *)memoria_dados_(b));
            return problemas + 1;
        }
        if(b->anterior != anterior) {
            memoria_aviso_(arquivo, linha, "verificar_heap: ligação inconsistente no bloco %p.", (void *)memoria_dados_(b));
            problemas++;
        }
        if(b->livre) {
            alterado = memoria_byte_alterado_(b, b->tamanho);
            if(alterado) {
                memoria_aviso_(arquivo, linha, "verificar_heap: uso após desalocar, o byte %p numa região já desalocada "
                                                "(último desalocar em %s:%d) foi modificado.",
                                (void *)alterado, b->arquivo_liberacao ? b->arquivo_liberacao : "?", b->linha_liberacao);
                problemas++;
            }
            if(anterior && anterior->livre && memoria_adjacentes_(anterior, b)) {
                memoria_aviso_(arquivo, linha, "verificar_heap: os blocos livres vizinhos %p e %p não foram unidos.",
                                (void *)memoria_dados_(anterior), (void *)memoria_dados_(b));
                problemas++;
            }
        } else {
            bytes += b->solicitado;
            blocos++;
            if(!memoria_canario_intacto_(b)) {
                memoria_aviso_(arquivo, linha, "verificar_heap: escrita além do limite no bloco de %lu byte(s) em %p, "
                                                "alocado em %s:%d.",
                                (unsigned long)b->solicitado, (void *)memoria_dados_(b),
                                b->arquivo ? b->arquivo : "?", b->linha);
                problemas++;
            }
        }
    }

    if(anterior != memoria_fim_) {
        memoria_aviso_(arquivo, linha, "verificar_heap: o fim da lista de blocos está inconsistente.");
        problemas++;
    }
    if(bytes != memoria_bytes_em_uso_ || blocos != memoria_blocos_em_uso_) {
        memoria_aviso_(arquivo, linha, "verificar_heap: contadores divergentes (%lu bytes em %lu blocos na lista, "
                                        "%lu bytes em %lu blocos registrados).",
                        (unsigned long)bytes, (unsigned long)blocos,
                        (unsigned long)memoria_bytes_em_uso_, (unsigned long)memoria_blocos_em_uso_);
        problemas++;
    }

    if(problemas == 0 && !silencioso) {
        memoria_ativar_utf8_();
        fprintf(stderr, "[SISTEMA] verificar_heap: nenhum problema encontrado (%lu bloco(s) verificado(s))",
                (unsigned long)total);
        if(arquivo) {
            fprintf(stderr, " (%s:%d)", arquivo, linha);
        }
        fputc('\n', stderr);
    }
    return problemas;
}

MEMORIA_FUNCAO void memoria_limitar_(size_t bytes)
{
    memoria_limite_simulado_ = bytes;
}

MEMORIA_FUNCAO void memoria_falhar_apos_(long alocacoes)
{
    memoria_alocacoes_ate_falha_ = alocacoes < 0 ? -1 : alocacoes;
}

MEMORIA_FUNCAO void memoria_mostrar_(const void *ptr, size_t n, const char *arquivo, int linha)
{
    const unsigned char *p = (const unsigned char *)ptr;
    MemoriaBloco *b;
    unsigned char bits[16];
    size_t i;
    size_t j;
    size_t largura;
    int bits_salvos;

    if(!ptr) {
        memoria_aviso_(arquivo, linha, "mostrar_memoria recebeu um ponteiro NULL.");
        return;
    }

    b = memoria_localizar_((void *)ptr);
    if(b && !b->livre && n > b->solicitado) {
        memoria_aviso_(arquivo, linha, "mostrar_memoria: o bloco tem só %lu byte(s); mostrando apenas esses.",
                        (unsigned long)b->solicitado);
        n = b->solicitado;
    }

    memoria_ativar_utf8_();
    if(b && !b->livre) {
        fprintf(stderr, "[SISTEMA] Bloco de %lu byte(s) em %p, alocado em %s:%d | canário %s\n",
                (unsigned long)b->solicitado, (void *)memoria_dados_(b),
                b->arquivo ? b->arquivo : "?", b->linha,
                memoria_canario_intacto_(b) ? "intacto" : "CORROMPIDO");
    }

    for(i = 0; i < n; i += 16) {
        largura = n - i < 16 ? n - i : 16;
        bits_salvos = MEMORIA_VG_LER_BITS(p + i, bits, largura) == 1;
        if(bits_salvos) {
            MEMORIA_VG_ACESSIVEL(p + i, largura);
        }
        fprintf(stderr, "[SISTEMA]   %p  ", (void *)(p + i));
        for(j = 0; j < 16; j++) {
            if(i + j < n) {
                fprintf(stderr, "%02X ", p[i + j]);
            } else {
                fprintf(stderr, "   ");
            }
        }
        fprintf(stderr, " |");
        for(j = 0; j < 16 && i + j < n; j++) {
            fputc((p[i + j] >= 32 && p[i + j] < 127) ? p[i + j] : '.', stderr);
        }
        fprintf(stderr, "|\n");
        if(bits_salvos) {
            MEMORIA_VG_GRAVAR_BITS(p + i, bits, largura);
        }
    }
}

MEMORIA_FUNCAO void relatorio_memoria(void)
{
    MemoriaBloco *b;

    memoria_ativar_utf8_();
    fprintf(stderr, "[SISTEMA] Heap:%lu byte(s) em uso em %lu bloco(s) | %lu alocação(ões) | "
                    "%lu liberação(ões) | pico de %lu bytes\n",
            (unsigned long)memoria_bytes_em_uso_,
            (unsigned long)memoria_blocos_em_uso_,
            (unsigned long)memoria_total_alocacoes_,
            (unsigned long)memoria_total_liberacoes_,
            (unsigned long)memoria_pico_);
    for (b = memoria_inicio_; b; b = b->proximo) {
        if (!b->livre) {
            fprintf(stderr, "[SISTEMA]   -> %lu byte(s) em %p (%s:%d)\n",
                    (unsigned long)b->solicitado,
                    (void *)((char *)b + MEMORIA_CABECALHO),
                    b->arquivo ? b->arquivo : "?", b->linha);
        }
    }
}

#define alocar(tamanho) memoria_alocar_((tamanho), __FILE__, __LINE__)
#define alocar_alinhado(ponteiro, alinhamento, tamanho) memoria_alocar_alinhado_((ponteiro), (alinhamento), (tamanho), __FILE__, __LINE__)
#define calocar(quantidade, tamanho) memoria_calocar_((quantidade), (tamanho), __FILE__, __LINE__)
#define realocar(ponteiro, tamanho) memoria_realocar_((ponteiro), (tamanho), __FILE__, __LINE__)
#define desalocar(ponteiro) memoria_liberar_((ponteiro), __FILE__, __LINE__)
#define liberar(ponteiro) memoria_liberar_((ponteiro), __FILE__, __LINE__)
#define copiar_memoria(destino, origem, n) memoria_copiar_((destino), (origem), (n), __FILE__, __LINE__)
#define mover_memoria(destino, origem, n) memoria_mover_((destino), (origem), (n), __FILE__, __LINE__)
#define preencher_memoria(destino, valor, n) memoria_preencher_((destino), (valor), (n), __FILE__, __LINE__)
#define comparar_memoria(a, b, n) memoria_comparar_((a), (b), (n), __FILE__, __LINE__)
#define procurar_memoria(ptr, valor, n) memoria_procurar_((ptr), (valor), (n), __FILE__, __LINE__)
#define duplicar_memoria(origem, n) memoria_duplicar_((origem), (n), __FILE__, __LINE__)
#define duplicar_texto(texto) memoria_duplicar_texto_((texto), __FILE__, __LINE__)
#define duplicar_texto_n(texto, n) memoria_duplicar_texto_n_((texto), (n), __FILE__, __LINE__)
#define limpar_memoria_segura(destino, n) memoria_limpar_segura_((destino), (n), __FILE__, __LINE__)
#define realocar_vetor(ponteiro, quantidade, tamanho) memoria_realocar_vetor_((ponteiro), (quantidade), (tamanho), __FILE__, __LINE__)
#define tamanho_bloco(ponteiro) memoria_tamanho_bloco_((ponteiro), __FILE__, __LINE__)
#define alocar_alinhado_direto(alinhamento, tamanho) memoria_alocar_alinhado_direto_((alinhamento), (tamanho), __FILE__, __LINE__)
#define verificar_heap() memoria_verificar_heap_(0, __FILE__, __LINE__)
#define limitar_memoria(bytes) memoria_limitar_((bytes))
#define falhar_apos(alocacoes) memoria_falhar_apos_((alocacoes))
#define mostrar_memoria(ptr, n) memoria_mostrar_((ptr), (n), __FILE__, __LINE__)

#endif