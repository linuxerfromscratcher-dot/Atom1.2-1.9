CC = gcc
FC = gfortran

CFLAGS  = -Wall -Wextra -O2
FFLAGS  = -Wall -O2 -fPIC
SHARED_FFLAGS = -shared

# Fortran support
FORTAN ?= 1

TARGET      = dist/atomc
FORTRAN_LIB = libatom_fortran.so

C_SRCS = $(wildcard src/*.c)
F_SRCS = $(wildcard src/*.f90)

C_OBJS = $(C_SRCS:src/%.c=build/%.o)
F_OBJS = $(F_SRCS:src/%.f90=build/%.o)

OBJS = $(C_OBJS)

all: $(TARGET)
build: $(TARGET)

ifeq ($(FORTAN),1)
$(TARGET): $(OBJS) $(FORTRAN_LIB)
	@mkdir -p build dist
	$(CC) $(CFLAGS) $(OBJS) -o $@ -ldl

$(FORTRAN_LIB): src/arithmetics.f90
	@mkdir -p build dist
	$(FC) $(SHARED_FFLAGS) $(FFLAGS) src/arithmetics.f90 -o $@
else
$(TARGET): $(OBJS)
	@mkdir -p build dist
	$(CC) $(CFLAGS) $(OBJS) -o $@ -ldl
endif

build/%.o: src/%.c
	@mkdir -p build dist
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.f90
	@mkdir -p build dist
	$(FC) $(FFLAGS) -c $< -o $@

test: $(TARGET)
	bash tests/run_tests.sh

clean:
	rm -rf build $(TARGET) $(FORTRAN_LIB)

.PHONY: all clean test
