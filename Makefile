
CC=gcc

.PHONY: simeth simnic

all: simeth simnic

simeth:
	${MAKE} -C simeth all

simnic:
	${MAKE} -C simeth_nic all

clean: clean_simeth clean_simnic

clean_simeth:
	${MAKE} -C simeth clean

clean_simnic:
	${MAKE} -C simeth_nic clean

