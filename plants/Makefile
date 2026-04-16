CC = gcc
CFLAGS = -Wall -Wextra -Icommon/include -O2
LDFLAGS = -lcurl -lm

COMMON_SRC = common/src/network.c common/src/solver.c
COMMON_OBJ = $(COMMON_SRC:.c=.o)

PLANTS = car dc_motor tank

TARGETS = $(addprefix bin/, $(PLANTS))

all: $(TARGETS)

bin/%: %/main.c $(COMMON_OBJ)
	@mkdir -p bin
	$(CC) $(CFLAGS) $< $(COMMON_OBJ) -o $@ $(LDFLAGS)

common/src/%.o: common/src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(COMMON_OBJ)
	rm -rf bin