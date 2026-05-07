TARGET = extract_features

ifeq (,$(TOOLCHAIN_PREFIX))
$(error TOOLCHAIN_PREFIX is not set. Please run 'source ../../envsetup.sh' first)
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

CXX = $(TOOLCHAIN_PREFIX)g++
CC = $(TOOLCHAIN_PREFIX)gcc

ROOT_DIR = ../..
COMMON_DIR = $(ROOT_DIR)/common

CFLAGS += -fsigned-char -Wno-format-truncation -fdiagnostics-color=always -s -lpthread -latomic
CXXFLAGS = $(CFLAGS) -std=c++11 -I$(ROOT_DIR)/include -I$(ROOT_DIR)/include/utils -I$(COMMON_DIR)/../include/tdl -I$(ROOT_DIR)/src/3rdparty/bch

LDFLAGS += -lini -lsns_full -lsample -lisp -lvdec -lvenc -lawb \
           -lae -laf -lcvi_bin -lcvi_bin_isp -lmisc -lisp_algo \
           -lsys  -lvi -lvo -lvpss -lrgn -lgdc
LDFLAGS += -lcvi_tdl
LDFLAGS += -lopencv_core -lopencv_imgproc -lopencv_imgcodecs
LDFLAGS += -lcvikernel -lcvimath -lcviruntime

# We need the FaceFeatureExtractor source
SRCS = extract_features.cpp \
       $(ROOT_DIR)/src/face_feature_extractor.cpp

OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(CXXFLAGS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

clean:
	rm -f $(OBJS) $(TARGET)
