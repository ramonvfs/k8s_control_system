CC = gcc
CFLAGS = -Wall -Wextra -Icommon/include -O2
LDFLAGS = -lcurl -lm -pthread

COMMON_SRC = common/src/network.c common/src/solver.c common/src/plant_runtime.c
COMMON_OBJ = $(COMMON_SRC:.c=.o)
COMMON_HDR = $(wildcard common/include/*.h)

PLANTS = car dc_motor tank

TARGETS = $(addprefix bin/, $(PLANTS))

all: $(TARGETS)

bin/%: %/main.c $(COMMON_OBJ) $(COMMON_HDR)
	@mkdir -p bin
	$(CC) $(CFLAGS) $< $(COMMON_OBJ) -o $@ $(LDFLAGS)

common/src/%.o: common/src/%.c $(COMMON_HDR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(COMMON_OBJ)
	rm -rf bin
