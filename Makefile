# ==============================================================================
# Makefile — Simulador de SO Multitarefa (Projeto A)
#
# Estrutura do projeto:
#   src/        Arquivos-fonte (.c)
#   include/    Cabeçalhos (.h)
#   build/      Objetos intermediários (.o)  — criado automaticamente
#   simulador   Executável final
#
# Uso:
#   make            Compila o projeto completo
#   make clean      Remove objetos e executável
#   make rebuild    Limpa e recompila do zero
#   make run        Compila e executa
#
# Compatibilidade: GCC no Linux, macOS e MinGW/MSYS2 no Windows.
# ==============================================================================

# --------------------------------------------------------------------------
# Compilador e flags
# --------------------------------------------------------------------------
CC      = gcc

# -Wall -Wextra       Ativa todos os avisos importantes
# -pedantic           Segue estritamente o padrão C (detecta extensões não-padrão)
# -std=c11            Padrão C11 (suporta _Bool, _Static_assert, etc.)
# -Iinclude           Permite #include "tipos.h" em vez de #include "../include/tipos.h"
CFLAGS  = -Wall -Wextra -pedantic -std=c11 -Iinclude

# Flags de debug (ativadas com: make DEBUG=1)
# -g        Símbolos de debug para GDB/LLDB
# -DDEBUG   Define a macro DEBUG no pré-processador (use #ifdef DEBUG no código)
ifeq ($(DEBUG), 1)
    CFLAGS += -g -DDEBUG
else
    # Otimização padrão (sem debug)
    CFLAGS += -O2
endif

# --------------------------------------------------------------------------
# Arquivos
# --------------------------------------------------------------------------
SRC_DIR   = src
INC_DIR   = include
BUILD_DIR = build

# Lista todos os .c em src/ automaticamente (sem precisar atualizar o Makefile
# ao adicionar novos módulos — basta criar o .c em src/ e o .h em include/).
SRCS = $(wildcard $(SRC_DIR)/*.c)

# Transforma src/foo.c em build/foo.o
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# Nome do executável final
TARGET = simulador

# --------------------------------------------------------------------------
# Regras
# --------------------------------------------------------------------------

# Regra padrão: compila tudo
all: $(BUILD_DIR) $(TARGET)

# Cria o diretório build/ se não existir
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Linka todos os objetos no executável final
# -lm  inclui libm (math.h) — necessário se futuramente usar sqrt/pow etc.
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ -lm
	@echo ""
	@echo "==> Compilacao concluida: $(TARGET)"

# Compila cada .c em seu respectivo .o
# $<  = primeiro pré-requisito (o .c)
# $@  = alvo (o .o)
# Dependências de headers são capturadas automaticamente pelo flag -MMD abaixo.
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Inclui os arquivos de dependência gerados por -MMD.
# Isso faz o Make recompilar automaticamente um .o quando um .h incluído muda,
# sem precisar declarar cada dependência manualmente.
-include $(OBJS:.o=.d)

# --------------------------------------------------------------------------
# Alvos auxiliares
# --------------------------------------------------------------------------

# Remove objetos, dependências e executável
clean:
	rm -rf $(BUILD_DIR) $(TARGET)
	@echo "==> Arquivos intermediarios removidos."

# Recompila do zero
rebuild: clean all

# Compila e executa
run: all
	./$(TARGET)

# Exibe as variáveis resolvidas (útil para depurar o próprio Makefile)
info:
	@echo "CC      = $(CC)"
	@echo "CFLAGS  = $(CFLAGS)"
	@echo "SRCS    = $(SRCS)"
	@echo "OBJS    = $(OBJS)"
	@echo "TARGET  = $(TARGET)"

# Marca estes alvos como "não são arquivos" para que o Make nunca confunda
# um alvo com um arquivo de mesmo nome no diretório.
.PHONY: all clean rebuild run info
