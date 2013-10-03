CC=g++
CFLAGS=-c -Wall -O3
LIBS=sgutils2

all: seagate-leds

seagate-leds: seagate-leds.o
	$(CC) seagate-leds.o -l $(LIBS) -o seagate-leds

seagate-leds.o: seagate-leds.cpp
	$(CC) $(CFLAGS) seagate-leds.cpp

clean:
	rm -rf *.o seagate-leds

