CC = gcc
CFLAGS = -Wall -Wextra -g -O2 -I./include
LDFLAGS = 

SRCDIR = src
INCDIR = include

SOURCES_MPMC = $(SRCDIR)/mpmc.c $(SRCDIR)/common.c $(SRCDIR)/shm_utils.c $(SRCDIR)/msg_utils.c
SOURCES_PRODUZ = $(SRCDIR)/produz.c $(SRCDIR)/common.c $(SRCDIR)/shm_utils.c $(SRCDIR)/msg_utils.c
SOURCES_CONSOME = $(SRCDIR)/consome.c $(SRCDIR)/common.c $(SRCDIR)/shm_utils.c $(SRCDIR)/msg_utils.c

OBJECTS_MPMC = $(SOURCES_MPMC:.c=.o)
OBJECTS_PRODUZ = $(SOURCES_PRODUZ:.c=.o)
OBJECTS_CONSOME = $(SOURCES_CONSOME:.c=.o)

TARGETS = mpmc produz consome

all: $(TARGETS)

mpmc: $(OBJECTS_MPMC)
	$(CC) $(OBJECTS_MPMC) -o mpmc $(LDFLAGS)

produz: $(OBJECTS_PRODUZ)
	$(CC) $(OBJECTS_PRODUZ) -o produz $(LDFLAGS)

consome: $(OBJECTS_CONSOME)
	$(CC) $(OBJECTS_CONSOME) -o consome $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/*.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SRCDIR)/*.o $(TARGETS) *~

distclean: clean
	rm -f /tmp/mpmc_*_key

.PHONY: all clean distclean
