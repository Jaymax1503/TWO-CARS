CC = gcc
LIBS = -lncurses -lm

two_cars: two_cars.c
	$(CC) -o game two_cars.c $(LIBS)

clean:
	rm -f two_cars
