# gmailk-V

[English](#english) | [中文](docs/lang/README_ch.md)

---

<a name="english"></a>
## English

### Overview

**gmailk-V** is a high-performance face detection application designed for CVITEK CV181X/CV180X RISC-V embedded platforms. It features real-time face detection, RTSP streaming, and a modular architecture optimized for embedded systems.

### Features

- 🎯 **Real-time Face Detection** - Powered by CVITEK TDL SDK
- 📹 **RTSP Streaming** - H.264 video encoding with live face detection overlay
- 🔧 **Modular Architecture** - Clean separation of concerns for easy maintenance
- 🚀 **Multi-threaded Design** - Separate threads for detection and encoding
- 🔌 **OpenCV & NCNN Integration** - Support for custom image processing and neural networks
- 💻 **RISC-V Optimized** - Built for CVITEK CV181X/CV180X platforms

### System Requirements

- **Hardware**: CVITEK CV181X or CV180X SoC
- **Toolchain**: RISC-V GCC cross-compiler (musl)
- **Dependencies**: 
  - CVITEK TDL SDK
  - CVITEK Media SDK
  - OpenCV
  - NCNN

### Project Structure

```
gmailk-V/
├── CMakeLists.txt          # CMake configuration
├── build.sh                # Build script
├── include/                # Header files
│   ├── shared_data.h       # Shared data structures
│   ├── system_init.h       # System initialization
│   ├── tdl_handler.h       # TDL face detection handler
│   ├── venc_handler.h      # Video encoding handler
│   └── button_handler.h    # Button input handler
├── src/                    # Source files
│   ├── main.cpp            # Main entry point
│   ├── shared_data.cpp
│   ├── system_init.cpp
│   ├── tdl_handler.cpp
│   ├── venc_handler.cpp
│   └── button_handler.cpp
├── common/                 # Common utilities
├── lib/                    # Third-party libraries
│   ├── opencv/             # OpenCV library
│   ├── ncnn/               # NCNN library
│   ├── system/             # System libraries
│   └── tdl/                # TDL libraries
├── models/                 # Face detection models
└── tools/                  # Build tools and scripts
    ├── build_opencv.sh
    └── build_ncnn.sh
```

### Building the Project

#### Step 1: Build Third-party Libraries (First Time Only)

```bash
cd tools

# Build OpenCV
./build_opencv.sh

# Build NCNN
./build_ncnn.sh
```

#### Step 2: Build the Main Application

```bash
# Configure for CV181X (default)
./build.sh

# Or configure for CV180X
./build.sh -c CV180X

# Clean build
./build.sh -c
```

The compiled binary will be located at: `build/main`

### Running the Application

```bash
# Basic usage
./build/main /path/to/face_detection_model.cvimodel

# Example
./build/main models/scrfd_det_face_432_768_INT8_cv181x.cvimodel
```

### RTSP Streaming

After starting the application, you can view the video stream with face detection overlay:

```bash
# Using VLC
vlc rtsp://<device-ip>:554/h264

# Using ffplay
ffplay rtsp://<device-ip>:554/h264
```

### Module Overview

#### 1. **shared_data** - Shared Data Module
Manages thread-safe shared data between detection and encoding threads.

**Key Components:**
- `g_bExit` - Atomic flag for graceful shutdown
- `g_stFaceMeta` - Global face detection results
- `g_ResultMutex` - Mutex for thread synchronization

#### 2. **system_init** - System Initialization Module
Handles low-level system initialization (VI/VPSS/VENC/RTSP).

**Key Functions:**
- `SystemInit_All()` - One-call initialization
- `SystemInit_Cleanup()` - Resource cleanup

#### 3. **tdl_handler** - TDL Detection Module
Encapsulates CVITEK TDL SDK for face detection.

**Key Functions:**
- `TDLHandler_Init()` - Initialize TDL and load model
- `TDLHandler_DetectFace()` - Perform face detection
- `TDLHandler_ThreadRoutine()` - Detection thread main loop

#### 4. **venc_handler** - Video Encoding Module
Handles H.264 encoding and RTSP streaming.

**Key Functions:**
- `VENCHandler_SendFrameRTSP()` - Send frame to RTSP
- `VENCHandler_ThreadRoutine()` - Encoding thread main loop

### Threading Architecture

```
Main Thread
├── TDL Thread (Face Detection)
│   ├── Get frame from VPSS CHN1
│   ├── Run face detection
│   └── Update global face metadata (locked)
│
└── VENC Thread (Video Encoding)
    ├── Get frame from VPSS CHN0
    ├── Read face metadata (locked)
    ├── Draw face rectangles
    └── Send to RTSP stream
```

### Configuration

Edit `config.json` for custom settings:

```json
{
  "chip": "CV181X",
  "video": {
    "width": 1920,
    "height": 1080,
    "fps": 30
  },
  "detection": {
    "threshold": 0.5,
    "model": "models/scrfd_det_face_432_768_INT8_cv181x.cvimodel"
  }
}
```

### Troubleshooting

**Cannot find OpenCV/NCNN libraries:**
- Make sure to build the libraries first using the scripts in `tools/`

**RTSP stream not accessible:**
- Check firewall settings
- Verify the device IP address
- Ensure port 554 is not blocked

### Contributing

Contributions are welcome! Please feel free to submit issues or pull requests.

### License

See [LICENSE](LICENSE) file for details.

---
