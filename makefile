CC := g++
CFLAGS := -std=c++17 -g
SRC := $(wildcard *.cpp)
OBJ := $(SRC:%.cpp=%.o)
HEADER := $(wildcard %.h)

app: $(OBJ)
	$(CC) $(CFLAGS) -pthread $(OBJ) -o $@ $(addprefix -I, $(HEADER))

%.o: %.cpp
	$(CC) $(CFLAGS) $< -c

.PHONY: clean
clean:
	rm app *.o
