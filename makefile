all:
	gcc -Wall -Werror -pthread -o s-talk s-talk.c instructorList.o

local1:
	./s-talk 1337 127.0.0.1 7331
	
local2:
	./s-talk 7331 127.0.0.1 1337
	
sfu1:
	./s-talk 6060 asb9700u-a01 6001
	
sfu2:
	./s-talk 6001 asb9700u-a03 6060
	
valgrind1: all
	valgrind --leak-check=full --show-leak-kinds=all ./s-talk 1337 127.0.0.1 7331
	
valgrind2: all
	valgrind --leak-check=full --show-leak-kinds=all ./s-talk 7331 127.0.0.1 1337

clean:
	rm s-talk

