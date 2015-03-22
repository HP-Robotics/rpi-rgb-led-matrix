CFLAGS=-Wall -O3 -g
CXXFLAGS=-Wall -O3 -g
OBJECTS=main.o gpio.o led-matrix.o ledsim.o thread.o
BINARIES=led-matrix
LDFLAGS=-lrt -lm -lpthread -lxcb -lxcb-keysyms

all : $(BINARIES)

led-matrix.o: led-matrix.cc led-matrix.h ledsim.h
ledsim.o: ledsim.c ledsim.h
main.o: led-matrix.h ledsim.h

led-matrix : $(OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(OBJECTS) $(BINARIES)
