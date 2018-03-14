
CC=gcc

all: bsimeth bsimnic

bsimeth:
	${MAKE} -C simeth all

bsimnic:
	${MAKE} -C simnic all

clean: clean_simeth clean_simnic

clean_simeth:
	${MAKE} -C simeth clean

clean_simnic:
	${MAKE} -C simnic clean

