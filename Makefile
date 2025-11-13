TARGET=main
CXX=$(TOOLCHAIN_PREFIX)g++

ifeq (,$(COMMON_DIR))
$(error COMMON_DIR is not set)
endif

ifeq (,$(TOOLCHAIN_PREFIX))
$(error TOOLCHAIN_PREFIX is not set)
endif

ifeq (,$(CFLAGS))
$(error CFLAGS is not set)
endif

ifeq (,$(LDFLAGS))
$(error LDFLAGS is not set)
endif

ifeq ($(CHIP), CV181X)
CFLAGS += -DCV181X -D__CV181X__
LDFLAGS += -lcvi_ive
else ifeq ($(CHIP), CV180X)
CFLAGS += -DCV180X -D__CV180X__
CFLAGS += -DUSE_TPU_IVE
LDFLAGS += -lcvi_ive_tpu
else
$(error CHIP is not set)
endif

CFLAGS += -std=gnu11 -Wno-pointer-to-int-cast -fsigned-char -Wno-format-truncation -fdiagnostics-color=always -s -lpthread -latomic
CXXFLAGS = $(CFLAGS) -std=c++11

LDFLAGS += -lini -lsns_full -lsample -lisp -lvdec -lvenc -lawb -lae -laf -lcvi_bin -lcvi_bin_isp -lmisc -lisp_algo -lsys  -lvi -lvo -lvpss -lrgn -lgdc
LDFLAGS += -lcvi_tdl
LDFLAGS += -lopencv_core -lopencv_imgproc -lopencv_imgcodecs
LDFLAGS += -lcvikernel -lcvimath -lcviruntime
LDFLAGS += -lcvi_rtsp

COMMON_SRC = $(COMMON_DIR)/middleware_utils.c
COMMON_OBJ = $(COMMON_SRC:.c=.o)

# Source files
C_SOURCES = $(wildcard src/*.c)
CPP_SOURCES = $(wildcard src/*.cpp)
C_OBJS = $(C_SOURCES:.c=.o)
CPP_OBJS = $(CPP_SOURCES:.cpp=.o)

# Old main
SOURCE = main.cpp
OBJS = $(SOURCE:.cpp=.o)

# New refactored main
SOURCE_REFACTORED = main.cpp
OBJS_REFACTORED = $(SOURCE_REFACTORED:.cpp=.o)

# Old refactored main
SOURCE_REFACTORED = main.cpp
OBJS_REFACTORED = $(SOURCE_REFACTORED:.cpp=.o)

# Targets
.PHONY: all clean old refactored

all: refactored

# Build old version
old: $(TARGET)

# Build refactored version (default)
refactored: $(TARGET)

$(TARGET): $(OBJS_REFACTORED) $(C_OBJS) $(CPP_OBJS) $(COMMON_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(COMMON_OBJ) $(C_OBJS) $(CPP_OBJS) $(OBJS_REFACTORED) $(LDFLAGS)

# Build old main (for comparison)
$(TARGET)_old: $(OBJS) $(COMMON_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(COMMON_OBJ) $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	@rm -rf *.o src/*.o
	@rm -rf $(COMMON_OBJ)
	@rm -rf $(C_OBJS) $(CPP_OBJS) $(OBJS) $(OBJS_REFACTORED)
	@rm -rf $(TARGET) $(TARGET)_old

