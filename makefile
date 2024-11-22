CFLAGS  = -Wall -no-pie `pkg-config --cflags openssl`
LDFLAGS = -lpthread `pkg-config --libs openssl`

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


client: main.c client.h 
	gcc $(CFLAGS) main.c -o client $(LDFLAGS)

.PHONY: run
run: client
	./client $(RUN_ARGS)

clean:
	rm -rf client

