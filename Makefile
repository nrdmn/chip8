chip8: chip8.cpp
	$(CXX) -Wall -Wextra -pedantic -Werror -Wno-missing-field-initializers -lSDL2 -std=c++2a -o chip8 chip8.cpp

clean:
	rm -f chip8
