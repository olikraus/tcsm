CFLAGS = -Ofast -Wall

SRC = $(shell ls *.c) 

OBJ = $(SRC:.c=.o)

find_card: $(OBJ) 
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJ)  -o $@
	cp find_card ../python/.

clean:
	-rm $(OBJ) find_card

