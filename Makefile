CC = gcc
CFLAGS = -Wall -O2

all: dc_motor car

dc_motor: plants/dc_motor/main.c plants/common/solver.c
	$(CC) $(CFLAGS) -o dc_motor plants/dc_motor/main.c plants/common/solver.c

car: plants/car/main.c plants/common/solver.c
	$(CC) $(CFLAGS) -o car plants/car/main.c plants/common/solver.c
 
clean:
	rm -f dc_motor
	rm -f car