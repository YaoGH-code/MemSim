CC=gcc
CFLAGS=-I.
TARGET=simulator
DEPS = simulator.h gll.h fileIO.h dataStructures.h

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

debug: CFLAGS += -g -O0 -D_GLIBC_DEBUG # debug flags
debug: clean $(TARGET)

$(TARGET): simulator.o gll.o fileIO.o 
	$(CC) -o $(TARGET) simulator.o gll.o fileIO.o -lm

.PHONY: clean

clean:
	rm -f *.o *.out $(TARGET)
	

	
