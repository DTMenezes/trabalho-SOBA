# Moldes de assembly usados pelo gerador, um bloco por comando LBS.
# Serve so para estudo: e daqui que sairam os bytes do gera_codigo.c.
#
#   gcc -c traducoes.s -o traducoes.o
#   objdump -d traducoes.o
#
# Posicoes na pilha:
#   v0 -4(%rbp)  v1 -8(%rbp)  v2 -12(%rbp)  v3 -16(%rbp)  v4 -20(%rbp)  p0 -24(%rbp)

  .text

prologo:                        # function
  pushq %rbp
  movq  %rsp, %rbp
  subq  $32, %rsp
  movl  %edi, -24(%rbp)

carrega_eax:                    # primeiro operando -> %eax
  movl  -4(%rbp), %eax          # v0
  movl  -20(%rbp), %eax         # v4
  movl  -24(%rbp), %eax         # p0
  movl  $100, %eax              # $100
  movl  $-10, %eax              # $-10

opera_memoria:                  # segundo operando e variavel ou p0
  addl  -8(%rbp), %eax
  subl  -8(%rbp), %eax
  imull -8(%rbp), %eax

opera_constante:                # segundo operando e constante
  addl  $1000, %eax
  subl  $1000, %eax
  imull $1000, %eax, %eax

salva_eax:                      # resultado -> vX
  movl  %eax, -4(%rbp)
  movl  %eax, -12(%rbp)

zret:                           # zret v1 v0
  cmpl  $0, -8(%rbp)
  jne   pula
  movl  -4(%rbp), %eax
  leave
  ret
pula:

call:                           # v1 = call 0 v2
  movl  -12(%rbp), %edi
  movl  $5, %edi                # (argumento constante)
  call  prologo
  movl  %eax, -8(%rbp)

ret_e_end:                      # ret / end
  leave
  ret
