CC      = gcc
CFLAGS  = -Wall -Wextra -g -I include
LDFLAGS = -lpthread

SRCS_DIR = src
BIN_DIR  = .

PROGRAMS = inicializador productor espia finalizador

all: $(PROGRAMS)

inicializador: $(SRCS_DIR)/inicializador.c include/shared.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

productor: $(SRCS_DIR)/productor.c include/shared.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

espia: $(SRCS_DIR)/espia.c include/shared.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

finalizador: $(SRCS_DIR)/finalizador.c include/shared.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(PROGRAMS) bitacora.log

.PHONY: all clean
