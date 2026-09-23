COMPILER = g++
NPROCS := $(shell grep -c ^processor /proc/cpuinfo)
ARCH := $(shell uname -m)

ifeq ($(ARCH),x86_64)
ARCH_CFLAGS = -mcmodel=medium
ARCH_LDFLAGS =
else ifeq ($(ARCH),aarch64)
ARCH_CFLAGS = -mcmodel=large -fno-pic -fno-pie -fsigned-char
ARCH_LDFLAGS = -no-pie
else
$(error Unsupported architecture: $(ARCH))
endif

CFLAGS = -g -MMD -MP -std=c++11 -O3 -fopenmp -pthread -DNPROCS=$(NPROCS) -DBOOST_BIND_GLOBAL_PLACEHOLDERS $(ARCH_CFLAGS)
WFLAGS = -pedantic -Wignored-qualifiers -Wreturn-type -Wmaybe-uninitialized -Wbool-compare -Wshadow -Wunused-but-set-variable -Wunused-variable
LIBS = -lboost_system

SRCDIR = .
SHARE_DIR = share
AI_DIR = ai_src

SOURCES = $(wildcard *.cpp)
SHARE_SOURCES = $(wildcard ./$(SHARE_DIR)/*.cpp)
AI_SOURCES = $(wildcard ./$(AI_DIR)/*.cpp)
PARAMS_SOURCES = $(wildcard ./$(AI_DIR)/params/*.cpp)

OBJDIR = ./obj
SHARE_OBJDIR = ./$(SHARE_DIR)/obj
AI_OBJDIR = ./$(AI_DIR)/obj
PARAMS_OBJDIR = ./$(AI_DIR)/obj/params

OBJECTS = $(addprefix $(OBJDIR)/, $(notdir $(SOURCES:.cpp=.o)))
SHARE_OBJECTS = $(addprefix $(SHARE_OBJDIR)/, $(notdir $(SHARE_SOURCES:.cpp=.o)))
AI_OBJECTS = $(addprefix $(AI_OBJDIR)/, $(notdir $(AI_SOURCES:.cpp=.o)))
PARAMS_OBJECTS = $(addprefix $(PARAMS_OBJDIR)/, $(notdir $(PARAMS_SOURCES:.cpp=.o)))

DEPENDS   = $(OBJECTS:.o=.d) $(SHARE_OBJECTS:.o=.d) $(AI_OBJECTS:.o=.d) $(PARAMS_OBJECTS:.o=.d)

HEADERS = $(SOURCES:.cpp=.hpp)
SHARE_HEADERS = $(SHARE_SOURCES:.cpp=.hpp)
AI_HEADERS = $(AI_SOURCES:.cpp=.hpp)

TARGET = system
TEST_SOURCES = tests/riichi_rules_test.cpp \
	tests/record_io_test.cpp \
	tests/streaming_analyzer_test.cpp \
	tests/full_analyze_protocol_test.cpp \
	tests/sparsemax_test.cpp
TEST_BINARIES = $(patsubst tests/%.cpp,$(OBJDIR)/tests/%,$(TEST_SOURCES))
TEST_DEPENDS = $(addsuffix .d,$(TEST_BINARIES))

$(TARGET):  $(OBJECTS) $(SHARE_OBJECTS) $(AI_OBJECTS) $(PARAMS_OBJECTS)
	$(COMPILER) $(OBJECTS) $(SHARE_OBJECTS) $(AI_OBJECTS) $(PARAMS_OBJECTS) $(CFLAGS) $(ARCH_LDFLAGS) $(WFLAGS) $(LIBS) -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp $(SRCDIR)/%.hpp
	@mkdir -p $(@D)
	$(COMPILER) $< -c $(CFLAGS) $(WFLAGS) -o $@

$(SHARE_OBJDIR)/%.o: $(SHARE_DIR)/%.cpp $(SHARE_DIR)/%.hpp
	@mkdir -p $(@D)
	$(COMPILER) $< -c $(CFLAGS) $(WFLAGS) -o $@

$(AI_OBJDIR)/%.o: $(AI_DIR)/%.cpp $(AI_DIR)/%.hpp
	@mkdir -p $(@D)
	$(COMPILER) $< -c $(CFLAGS) $(WFLAGS) -o $@

$(PARAMS_OBJDIR)/%.o: $(AI_DIR)/params/%.cpp
	@mkdir -p $(@D)
	$(COMPILER) $< -c $(CFLAGS) $(WFLAGS) -o $@

clean:
	-rm -f $(OBJECTS) $(SHARE_OBJECTS) $(AI_OBJECTS) $(PARAMS_OBJECTS) $(DEPENDS) $(TARGET) $(OBJDIR)/tests/*

$(OBJDIR)/tests/riichi_rules_test: tests/riichi_rules_test.cpp $(SHARE_OBJECTS)
	@mkdir -p $(@D)
	$(COMPILER) $< $(SHARE_OBJECTS) $(CFLAGS) $(ARCH_LDFLAGS) $(WFLAGS) $(LIBS) -MMD -MP -MF $@.d -o $@

RECORD_IO_TEST_OBJECTS = $(SHARE_OBJDIR)/record_io.o $(SHARE_OBJDIR)/types.o $(SHARE_OBJDIR)/event.o $(SHARE_OBJDIR)/include.o

$(OBJDIR)/tests/record_io_test: tests/record_io_test.cpp $(RECORD_IO_TEST_OBJECTS)
	@mkdir -p $(@D)
	$(COMPILER) $< $(RECORD_IO_TEST_OBJECTS) $(CFLAGS) $(ARCH_LDFLAGS) $(WFLAGS) $(LIBS) -MMD -MP -MF $@.d -o $@

STREAMING_TEST_OBJECTS = $(OBJDIR)/streaming_analyzer.o $(OBJDIR)/analysis_io.o $(RECORD_IO_TEST_OBJECTS)

$(OBJDIR)/tests/streaming_analyzer_test: tests/streaming_analyzer_test.cpp $(STREAMING_TEST_OBJECTS)
	@mkdir -p $(@D)
	$(COMPILER) $< $(STREAMING_TEST_OBJECTS) $(CFLAGS) $(ARCH_LDFLAGS) $(WFLAGS) $(LIBS) -MMD -MP -MF $@.d -o $@

$(OBJDIR)/tests/full_analyze_protocol_test: tests/full_analyze_protocol_test.cpp $(STREAMING_TEST_OBJECTS)
	@mkdir -p $(@D)
	$(COMPILER) $< $(STREAMING_TEST_OBJECTS) $(CFLAGS) $(ARCH_LDFLAGS) $(WFLAGS) $(LIBS) -MMD -MP -MF $@.d -o $@

$(OBJDIR)/tests/sparsemax_test: tests/sparsemax_test.cpp
	@mkdir -p $(@D)
	$(COMPILER) -std=c++11 -O2 -Wall -MMD -MP -MF $@.d $< -o $@

test: $(TEST_BINARIES)
	@set -e; for test_binary in $(TEST_BINARIES); do $$test_binary; done

.PHONY: clean test

-include $(DEPENDS) $(TEST_DEPENDS)
