SHELL	= /bin/bash -O extglob -c
CC	= g++
CXX	= g++
CFLAGS	= -Wall -Wextra -pedantic -pedantic-errors -g -O2 -DLINUX -DREDAX_BUILD_COMMIT='$(BUILD_COMMIT)' -std=c++17 -pthread $(shell pkg-config --cflags libmongocxx)
CPPFLAGS := $(CFLAGS)
LDFLAGS = -lCAENVME -lCAENDigitizer -lstdc++fs -llz4 -lblosc $(shell pkg-config --libs libmongocxx) $(shell pkg-config --libs libbsoncxx)
#LDFLAGS_CC = ${LDFLAGS} -lexpect -ltcl8.6

DIR_INC=./include
DIR_SRC=./src
DIR_DEP=./dep
DIR_OBJ=./obj


#SOURCES := src/DAQController.cc src/main.cc src/MongoLog.cc src/Options.cc src/StraxFormatter.cc src/V1725.cc
SOURCES :=  src/main.cc src/MongoLog.cc src/Options.cc src/V1725.cc src/DAQController.cc
INCLUDES := $(wildcard $(DIR_INC)/*.hh)
OBJECTS := $(SOURCES:$(DIR_SRC)/%.cc=$(DIR_OBJ)/%.o)
EXEC_SLAVE = idacre


all: $(EXEC_SLAVE)

$(EXEC_SLAVE) : $(OBJECTS) 
	$(CC) $(OBJECTS) $(CFLAGS) $(LDFLAGS) -o $(EXEC_SLAVE)

$(OBJECTS) : $(DIR_OBJ)/%.o : $(DIR_SRC)/%.cc 
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean

install:
	sudo ln -sf ./idacre /usr/bin/idacre

clean:
	rm -f $(DIR_OBJ)/*.o $(DIR_DEP)/*.d
	rm -f $(EXEC_SLAVE)

include $(DEPS_SLAVE)