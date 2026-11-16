/* Nome_do_Aluno1 Matricula Turma */
/* Nome_do_Aluno2 Matricula Turma */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "gera_codigo.h"

#define MAX_FUNCS   50
#define MAX_LINHA   256
#define MAX_TOKENS  8
#define TAM_TOKEN   32

/*
 * Layout do registro de ativacao de toda funcao LBS:
 *
 *    v0 -> -4(%rbp)     v1 -> -8(%rbp)     v2 -> -12(%rbp)
 *    v3 -> -16(%rbp)    v4 -> -20(%rbp)    p0 -> -24(%rbp)
 *
 * Sao 24 bytes usados, arredondados para 32 para manter %rsp
 * alinhado em 16 bytes (exigencia da ABI antes de um call).
 *
 * O p0 chega em %edi e e copiado para a pilha logo no prologo,
 * porque %edi e reaproveitado para passar argumento em cada call.
 *
 * Registradores usados no codigo gerado: %eax e %edi, ambos
 * caller-saved, entao nao e preciso salvar nada alem de %rbp.
 */
#define OFFSET_P0   (-24)

typedef enum { OPND_VAR, OPND_PARAM, OPND_CONST } TipoOperando;

typedef struct {
  TipoOperando tipo;
  int valor;            /* indice da variavel (vi) ou valor da constante */
} Operando;


/* ------------------------------------------------------------------ */
/* Emissao de bytes                                                    */
/* ------------------------------------------------------------------ */

static void emite_byte(unsigned char code[], int *pos, unsigned char b) {
  code[(*pos)++] = b;
}

/* inteiro de 32 bits em little-endian */
static void emite_int(unsigned char code[], int *pos, int v) {
  unsigned int u = (unsigned int)v;
  int i;
  for (i = 0; i < 4; i++)
    emite_byte(code, pos, (unsigned char)(u >> (8 * i)));
}

/* deslocamento de 8 bits (complemento a 2) do operando em relacao a %rbp */
static unsigned char desloc(Operando op) {
  if (op.tipo == OPND_PARAM)
    return (unsigned char)OFFSET_P0;
  return (unsigned char)(-4 * (op.valor + 1));
}


/* ------------------------------------------------------------------ */
/* Leitura da linha                                                    */
/* ------------------------------------------------------------------ */

/*
 * Quebra a linha em tokens. Funciona com ou sem espacos entre os
 * simbolos ("v0 = p0 + $1" e "v0=p0+$1" geram os mesmos tokens).
 * O '-' logo depois de '$' e sinal da constante, nao operador.
 */
static int tokeniza(const char *s, char tok[][TAM_TOKEN]) {
  int n = 0;

  while (*s && n < MAX_TOKENS) {
    int k = 0;

    if (isspace((unsigned char)*s)) { s++; continue; }

    if (isalpha((unsigned char)*s)) {                 /* function, end, v0, p0, call... */
      while (isalnum((unsigned char)*s) && k < TAM_TOKEN - 1)
        tok[n][k++] = *s++;
    }
    else if (*s == '$') {                             /* $10, $-10 */
      tok[n][k++] = *s++;
      if (*s == '-') tok[n][k++] = *s++;
      while (isdigit((unsigned char)*s) && k < TAM_TOKEN - 1)
        tok[n][k++] = *s++;
    }
    else if (isdigit((unsigned char)*s)) {            /* numero da funcao no call */
      while (isdigit((unsigned char)*s) && k < TAM_TOKEN - 1)
        tok[n][k++] = *s++;
    }
    else {                                            /* = + - * */
      tok[n][k++] = *s++;
    }

    tok[n][k] = '\0';
    n++;
  }
  return n;
}

static Operando le_operando(const char *s) {
  Operando op;
  if (s[0] == 'v') {
    op.tipo = OPND_VAR;
    op.valor = atoi(s + 1);
  }
  else if (s[0] == 'p') {
    op.tipo = OPND_PARAM;
    op.valor = 0;
  }
  else {                                  /* '$' */
    op.tipo = OPND_CONST;
    op.valor = atoi(s + 1);
  }
  return op;
}


/* ------------------------------------------------------------------ */
/* Blocos de codigo de maquina                                         */
/* ------------------------------------------------------------------ */

static void emite_prologo(unsigned char code[], int *pos) {
  emite_byte(code, pos, 0x55);                        /* push %rbp           */
  emite_byte(code, pos, 0x48);                        /* mov  %rsp,%rbp      */
  emite_byte(code, pos, 0x89);
  emite_byte(code, pos, 0xe5);
  emite_byte(code, pos, 0x48);                        /* sub  $32,%rsp       */
  emite_byte(code, pos, 0x83);
  emite_byte(code, pos, 0xec);
  emite_byte(code, pos, 0x20);
  emite_byte(code, pos, 0x89);                        /* mov  %edi,-24(%rbp) */
  emite_byte(code, pos, 0x7d);
  emite_byte(code, pos, (unsigned char)OFFSET_P0);
}

static void emite_finalizacao(unsigned char code[], int *pos) {
  emite_byte(code, pos, 0xc9);                        /* leave */
  emite_byte(code, pos, 0xc3);                        /* ret   */
}

/* %eax = operando */
static void carrega_eax(unsigned char code[], int *pos, Operando op) {
  if (op.tipo == OPND_CONST) {
    emite_byte(code, pos, 0xb8);                      /* mov $imm32,%eax     */
    emite_int(code, pos, op.valor);
  }
  else {
    emite_byte(code, pos, 0x8b);                      /* mov disp8(%rbp),%eax */
    emite_byte(code, pos, 0x45);
    emite_byte(code, pos, desloc(op));
  }
}

/* %edi = operando (argumento de um call) */
static void carrega_edi(unsigned char code[], int *pos, Operando op) {
  if (op.tipo == OPND_CONST) {
    emite_byte(code, pos, 0xbf);                      /* mov $imm32,%edi     */
    emite_int(code, pos, op.valor);
  }
  else {
    emite_byte(code, pos, 0x8b);                      /* mov disp8(%rbp),%edi */
    emite_byte(code, pos, 0x7d);
    emite_byte(code, pos, desloc(op));
  }
}

/* variavel = %eax */
static void salva_eax(unsigned char code[], int *pos, Operando var) {
  emite_byte(code, pos, 0x89);                        /* mov %eax,disp8(%rbp) */
  emite_byte(code, pos, 0x45);
  emite_byte(code, pos, desloc(var));
}

/* %eax = %eax <op> operando */
static void opera_eax(unsigned char code[], int *pos, char op, Operando b) {
  if (b.tipo == OPND_CONST) {
    switch (op) {
      case '+': emite_byte(code, pos, 0x05); break;   /* add  $imm32,%eax      */
      case '-': emite_byte(code, pos, 0x2d); break;   /* sub  $imm32,%eax      */
      case '*': emite_byte(code, pos, 0x69);          /* imul $imm32,%eax,%eax */
                emite_byte(code, pos, 0xc0); break;
    }
    emite_int(code, pos, b.valor);
  }
  else {
    switch (op) {
      case '+': emite_byte(code, pos, 0x03); break;   /* add  disp8(%rbp),%eax */
      case '-': emite_byte(code, pos, 0x2b); break;   /* sub  disp8(%rbp),%eax */
      case '*': emite_byte(code, pos, 0x0f);          /* imul disp8(%rbp),%eax */
                emite_byte(code, pos, 0xaf); break;
    }
    emite_byte(code, pos, 0x45);
    emite_byte(code, pos, desloc(b));
  }
}


/* ------------------------------------------------------------------ */
/* Traducao de cada comando LBS                                        */
/* ------------------------------------------------------------------ */

/* ret a */
static void traduz_ret(unsigned char code[], int *pos, Operando a) {
  carrega_eax(code, pos, a);
  emite_finalizacao(code, pos);
}

/* zret a b : se a == 0, retorna b */
static void traduz_zret(unsigned char code[], int *pos, Operando a, Operando b) {
  int inicio_bloco;

  /* Se a condicao e constante, ja sabemos o resultado na hora de compilar */
  if (a.tipo == OPND_CONST) {
    if (a.valor == 0)
      traduz_ret(code, pos, b);       /* sempre retorna */
    return;                           /* nunca retorna: nao gera nada */
  }

  emite_byte(code, pos, 0x83);        /* cmpl $0,disp8(%rbp) */
  emite_byte(code, pos, 0x7d);
  emite_byte(code, pos, desloc(a));
  emite_byte(code, pos, 0x00);

  emite_byte(code, pos, 0x75);        /* jne rel8 (pula o retorno) */
  emite_byte(code, pos, 0x00);        /* deslocamento preenchido abaixo */
  inicio_bloco = *pos;

  traduz_ret(code, pos, b);

  code[inicio_bloco - 1] = (unsigned char)(*pos - inicio_bloco);
}

/* dest = a op b */
static void traduz_oper(unsigned char code[], int *pos,
                        Operando dest, Operando a, char op, Operando b) {
  carrega_eax(code, pos, a);
  opera_eax(code, pos, op, b);
  salva_eax(code, pos, dest);
}

/*
 * dest = call num arg
 *
 * A funcao chamada e sempre a propria ou uma anterior, entao o seu
 * inicio ja esta em inicio_func[num] e o deslocamento do call pode
 * ser calculado na mesma passada. O rel32 e relativo ao endereco da
 * instrucao seguinte ao call.
 */
static void traduz_call(unsigned char code[], int *pos, Operando dest,
                        int num, Operando arg, const int inicio_func[]) {
  int rel;

  carrega_edi(code, pos, arg);

  emite_byte(code, pos, 0xe8);        /* call rel32 */
  rel = inicio_func[num] - (*pos + 4);
  emite_int(code, pos, rel);

  salva_eax(code, pos, dest);         /* valor de retorno vem em %eax */
}


/* ------------------------------------------------------------------ */
/* Funcao principal                                                    */
/* ------------------------------------------------------------------ */

void gera_codigo(FILE *f, unsigned char code[], funcp *entry) {
  char linha[MAX_LINHA];
  char tok[MAX_TOKENS][TAM_TOKEN];
  int inicio_func[MAX_FUNCS];
  int nfuncs = 0;
  int pos = 0;
  int nlinha = 0;
  int n;

  *entry = NULL;

  while (fgets(linha, sizeof(linha), f) != NULL) {
    nlinha++;
    n = tokeniza(linha, tok);
    if (n == 0)
      continue;                                       /* linha em branco */

    if (strcmp(tok[0], "function") == 0) {
      inicio_func[nfuncs++] = pos;
      emite_prologo(code, &pos);
    }
    else if (strcmp(tok[0], "end") == 0) {
      /* Garantia caso a funcao nao termine com ret (codigo inalcancavel
         quando o ultimo comando ja e um ret). */
      emite_finalizacao(code, &pos);
    }
    else if (strcmp(tok[0], "ret") == 0 && n >= 2) {
      traduz_ret(code, &pos, le_operando(tok[1]));
    }
    else if (strcmp(tok[0], "zret") == 0 && n >= 3) {
      traduz_zret(code, &pos, le_operando(tok[1]), le_operando(tok[2]));
    }
    else if (tok[0][0] == 'v' && n >= 5 && strcmp(tok[1], "=") == 0) {
      Operando dest = le_operando(tok[0]);

      if (strcmp(tok[2], "call") == 0)
        traduz_call(code, &pos, dest, atoi(tok[3]), le_operando(tok[4]), inicio_func);
      else
        traduz_oper(code, &pos, dest, le_operando(tok[2]), tok[3][0], le_operando(tok[4]));
    }
    else {
      fprintf(stderr, "gera_codigo: linha %d nao reconhecida\n", nlinha);
    }
  }

  if (nfuncs > 0)
    *entry = (funcp)(code + inicio_func[nfuncs - 1]);
}
