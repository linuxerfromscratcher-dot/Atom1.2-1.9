CC = gcc
FC = gfortran

CFLAGS  = -Wall -Wextra -O2 -MMD -MP
FFLAGS  = -Wall -O2 -fPIC
SHARED_FFLAGS = -shared

FORTAN ?= 0

TARGET      = dist/atomc
FORTRAN_LIB = libatom_fortran.so

C_SRCS = $(wildcard src/*.c)
F_SRCS = $(wildcard src/*.f90)

C_OBJS = $(C_SRCS:src/%.c=build/%.o)
F_OBJS = $(F_SRCS:src/%.f90=build/%.o)

OBJS = $(C_OBJS)
LIBS = -ldl

FFI_CFLAGS := $(shell pkg-config --cflags libffi 2>/dev/null)
FFI_LIBS   := $(shell pkg-config --libs libffi 2>/dev/null)

ifneq ($(FFI_LIBS),)
CFLAGS += -DATOM_USE_LIBFFI $(FFI_CFLAGS)
LIBS   += $(FFI_LIBS)
endif

all: $(TARGET)
build: $(TARGET)

ifeq ($(FORTAN),1)
$(TARGET): $(OBJS) $(FORTRAN_LIB)
	@mkdir -p build dist
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LIBS)

$(FORTRAN_LIB): src/arithmetics.f90
	@mkdir -p build dist
	$(FC) $(SHARED_FFLAGS) $(FFLAGS) -J build src/arithmetics.f90 -o $@
else
$(TARGET): $(OBJS)
	@mkdir -p build dist
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LIBS)
endif

build/%.o: src/%.c
	@mkdir -p build dist
	$(CC) $(CFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)

build/%.o: src/%.f90
	@mkdir -p build dist
	$(FC) $(FFLAGS) -J build -c $< -o $@

test: $(TARGET)
	bash tests/run_tests.sh
	bash tests/run_native.sh

clean:
	rm -rf build dist $(TARGET) $(FORTRAN_LIB) *.mod tests/mh/fixtures/*.so

.PHONY: all clean test
