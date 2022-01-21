BINARY := uploader

CMDLINE_FILE := cmdline.c
SRC := $(wildcard src/*.cpp)

all: $(BINARY)

clean:
	rm -f $(BINARY)

$(BINARY): $(SRC) $(CMDLINE_FILE) Makefile
	g++ -O3 -std=c++11 -o $(BINARY) $(SRC) $(CMDLINE_FILE) -luv

$(CMDLINE_FILE): $(BINARY).cmdline
	gengetopt --input=$(BINARY).cmdline --include-getopt
