CC = gcc
FC = gfortran

CFLAGS = -Wall -Wextra -O2
FFLAGS = -Wall -O2

TARGET = dist/atomc

C_SRCS = $(wildcard src/*.c)
F_SRCS = $(wildcard src/*.f90)

C_OBJS = $(C_SRCS:src/%.c=build/%.o)
F_OBJS = $(F_SRCS:src/%.f90=build/%.o)

OBJS = $(C_OBJS) $(F_OBJS)

all: $(TARGET)
build: $(TARGET)

$(TARGET): $(OBJS)
	$(FC) $(OBJS) -o $@

build/%.o: src/%.c
	@mkdir -p build dist
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.f90
	@mkdir -p build dist
	$(FC) $(FFLAGS) -c $< -o $@

clean:
	rm -rf build $(TARGET)

.PHONY: all clean
