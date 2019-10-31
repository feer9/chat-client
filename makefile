# If the first argument is "run"...
ifeq (run,$(firstword $(MAKECMDGOALS)))
  # use the rest as arguments for "run"
  RUN_ARGS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
  # ...and turn them into do-nothing targets
  $(eval $(RUN_ARGS):;@:)
endif

.PHONY: all
all: build

.PHONY: build
build: client


client: main.c #client.h 
	gcc -Wall -no-pie main.c -o client -lpthread

.PHONY: run
run: client
	./client $(RUN_ARGS)

clean:
	rm -rf client
