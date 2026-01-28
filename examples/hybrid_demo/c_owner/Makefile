# Makefile for Hybrid Demo C Owner
#
# Prerequisites:
#   - libpaho-mqtt installed (brew install libpaho-mqtt on macOS)
#   - Run ../generate.sh first to create demo_types.h

CC = gcc
CFLAGS = -Wall -Wextra -O2

# Include paths
INCLUDES = -I../lib/include

# Library paths (adjust for your system)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS with Homebrew
    PAHO_PREFIX := $(shell brew --prefix libpaho-mqtt 2>/dev/null || echo "/opt/homebrew/opt/libpaho-mqtt")
    INCLUDES += -I$(PAHO_PREFIX)/include
    LDFLAGS = -L$(PAHO_PREFIX)/lib
else
    # Linux
    LDFLAGS =
endif

LIBS = -lpaho-mqtt3c

# Source files
SRC = main.c \
      ../lib/src/sds_core.c \
      ../lib/src/sds_json.c \
      ../lib/platform/posix/sds_platform_posix.c

# Output
TARGET = owner

.PHONY: all clean

all: check_types $(TARGET)

# Check that demo_types.h exists
check_types:
	@if [ ! -f ../lib/include/demo_types.h ]; then \
		echo "Error: demo_types.h not found."; \
		echo "Run '../generate.sh' first to generate types."; \
		exit 1; \
	fi

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRC) -o $(TARGET) $(LDFLAGS) $(LIBS)
	@echo ""
	@echo "Build successful!"
	@echo "Run: ./$(TARGET) [broker_host] [node_id]"
	@echo "Example: ./$(TARGET) localhost c_owner"

clean:
	rm -f $(TARGET)
