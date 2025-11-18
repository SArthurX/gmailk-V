TARGET=main


ifeq (,$(TOOLCHAIN_PREFIX))
$(error TOOLCHAIN_PREFIX is not set)
endif

ifeq ($(CHIP), CV181X)
CFLAGS += -DCV181X -D__CV181X__
LDFLAGS += -lcvi_ive
else ifeq ($(CHIP), CV180X)
CFLAGS += -DCV180X -D__CV180X__
CFLAGS += -DUSE_TPU_IVE
LDFLAGS += -lcvi_ive_tpu
else
$(error CHIP is not supported)
endif

CXX=$(TOOLCHAIN_PREFIX)g++
CC=$(TOOLCHAIN_PREFIX)gcc
CFLAGS +=  -fsigned-char -Wno-format-truncation -fdiagnostics-color=always -s -lpthread -latomic
CXXFLAGS = $(CFLAGS) -std=c++11 -I./include -I$(COMMON_DIR)/../include/tdl

LDFLAGS += -lini -lsns_full -lsample -lisp -lvdec -lvenc -lawb \
		   -lae -laf -lcvi_bin -lcvi_bin_isp -lmisc -lisp_algo \
		   -lsys  -lvi -lvo -lvpss -lrgn -lgdc
LDFLAGS += -lcvi_tdl
LDFLAGS += -lopencv_core -lopencv_imgproc -lopencv_imgcodecs
LDFLAGS += -lcvikernel -lcvimath -lcviruntime
LDFLAGS += -lcvi_rtsp
LDFLAGS += -lwiringx

OBJ_DIR = obj

COMMON_SRC = $(COMMON_DIR)/middleware_utils.c
COMMON_OBJ = $(COMMON_SRC:.c=.o)

CPP_SOURCES = $(wildcard src/*.cpp)
CPP_OBJS = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(CPP_SOURCES))

C_SOURCES = $(wildcard src/*.c)
C_OBJS = $(patsubst %.c, $(OBJ_DIR)/%.o, $(C_SOURCES))

SOURCE = main.cpp
OBJS = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(SOURCE))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) $(CPP_OBJS) $(C_OBJS) $(COMMON_OBJ)
	$(CXX) -o $@ $(COMMON_OBJ) $(CPP_OBJS) $(C_OBJS) $(OBJS) $(CXXFLAGS) $(LDFLAGS)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ -c $<

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	@rm -rf $(OBJ_DIR)
	@rm -rf $(COMMON_OBJ)
	@rm -rf $(TARGET)
