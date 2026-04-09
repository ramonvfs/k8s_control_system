CC = gcc
CFLAGS = -Wall -O2
LIBS = -lcurl

all: dc_motor car

dc_motor: plants/dc_motor/main.c plants/common/solver.c plants/common/network.c
	$(CC) $(CFLAGS) -o dc_motor plants/dc_motor/main.c plants/common/solver.c plants/common/network.c $(LIBS)

car: plants/car/main.c plants/common/solver.c plants/common/network.c
	$(CC) $(CFLAGS) -o car plants/car/main.c plants/common/solver.c plants/common/network.c $(LIBS)
 
clean:
	rm -f dc_motor car