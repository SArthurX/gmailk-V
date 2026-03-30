# gmailk-V

[English](#english) | [中文](#chinese) | [專案全景狀態](docs/PROJECT_STATUS.md)

---

<a name="english"></a>
## English

### Overview

**gmailk-V** is a privacy-preserving face detection & recognition system for **CVITEK CV181X/CV180X RISC-V** embedded platforms. It combines real-time AI inference with revocable biometric template protection (BioHash + BCH ECC), ensuring that **no raw face features are ever stored**.

### Features

- 🎯 **Real-time Face Detection** — SCRFD via CVITEK TDL SDK (TPU)
- 🧠 **Face Recognition** — ArcFace 512-dim features (CVI TPU inference)
- 🔐 **Privacy-Preserving Storage** — BioHash + BCH error-correcting codes (no raw features stored)
- 🕐 **Auto-Expiring Templates** — Stateless time-based seeds with automatic revocation (~1 month)
- 📹 **RTSP Streaming** — H.264 1280×720 with live face overlay (colored bounding boxes + IDs)
- 🏃 **DeepSORT Tracking** — Stable face tracking with persistent IDs
- 🔘 **Hardware Interaction** — GPIO button (short press: identify / long press: register), LED, SSD1306 OLED
- 🚀 **Multi-threaded** — Separate TDL, VENC, and Button threads
- ⚡ **Batch Verification** — O(Seed) + O(Person) optimized matching

### System Requirements

- **Hardware**: CVITEK CV181X or CV180X SoC (e.g., Milk-V Duo 256M)
- **Toolchain**: RISC-V GCC cross-compiler (musl)
- **Dependencies**: 
  - CVITEK TDL SDK & Media SDK
  - OpenCV (cross-compiled)
  - CVI NN Runtime

### Project Structure

```
gmailk-V/
├── CMakeLists.txt              # Top-level CMake
├── build.sh                    # Cross-compile script (RISC-V musl)
├── config.json                 # Runtime configuration
│
├── src/                        # Source files (10 .cpp)
│   ├── main.cpp                # Entry point, init & thread creation
│   ├── shared_data.cpp         # Global shared data + mutexes
│   ├── system_init.cpp         # VI/VPSS/VENC/RTSP initialization
│   ├── tdl_handler.cpp         # Face detect + track + feature extract + verify
│   ├── venc_handler.cpp        # H.264 encoding + RTSP + OSD drawing
│   ├── button_handler.cpp      # GPIO button polling (short/long press)
│   ├── biohash_processor.cpp   # BioHash + BCH core algorithm
│   ├── face_database.cpp       # JSON face database (BioHash templates)
│   ├── face_feature_extractor.cpp  # ArcFace TPU inference
│   ├── oled_ctrl.cpp           # SSD1306 OLED display
│   ├── helpers/                # Inline helper modules
│   ├── 3rdparty/               # BCH codec, nlohmann/json
│   └── drivers/                # SSD1306 I2C driver
│
├── include/                    # Header files
├── models/                     # SCRFD + ArcFace .cvimodel files
├── bioh-bch/                   # BioHash+BCH reference implementation & docs
│   ├── process_explanation.md  # Algorithm walkthrough
│   └── time_based_biohash_concept.md  # Architecture design & extensions
├── data/                       # face_database.json (runtime)
├── docs/                       # Documentation
│   └── PROJECT_STATUS.md       # Full project status for AI collaboration
├── common/                     # Middleware utilities
├── lib/                        # Pre-built libraries (opencv, tdl, system)
└── tools/                      # Toolchain & build scripts
```

### Building

```bash
# Build (default: CV181X, Release)
./build.sh

# Options
./build.sh -d              # Debug build
./build.sh -re             # Clean rebuild
./build.sh --chip CV180X   # Target CV180X
./build.sh -c              # Clean only
./build.sh -t              # Build test directory
```

### Running

```bash
# Detection only
./main models/scrfd_det_face_432_768_INT8_cv181x.cvimodel

# Detection + Recognition (ArcFace TPU)
./main models/scrfd_det_face_432_768_INT8_cv181x.cvimodel models/arcface_cv181x_int8_sym.cvimodel

# With OLED display
./main models/scrfd.cvimodel models/arcface.cvimodel --oled
```

### RTSP Streaming

```bash
vlc rtsp://<device-ip>:554/h264
ffplay rtsp://<device-ip>:554/h264
```

### Hardware Pinout

| Pin | Function | Notes |
|-----|----------|-------|
| GPIO 21 | Button input | Pull-up, active LOW |
| GPIO 25 | LED output | Toggles on press |
| I2C-3 | SSD1306 OLED | 128×64, optional (`--oled`) |

### Threading Architecture

```
Main Thread
├── TDL Thread (VPSS CHN1)
│   ├── SCRFD face detection
│   ├── DeepSORT tracking
│   ├── Auto-lock (3s center dwell)
│   ├── Button event handling
│   ├── ArcFace feature extraction (TPU)
│   ├── BioHash + BCH verification
│   └── Update global face metadata
│
├── VENC Thread (VPSS CHN0)
│   ├── Read face metadata
│   ├── Draw bounding boxes (🔴selected 🟡center 🟢stable 🔵unstable)
│   ├── Draw crosshair + FPS
│   └── H.264 → RTSP stream
│
└── Button Thread
    └── GPIO polling → short press (identify) / long press (register)
```

### BioHash + BCH Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| `BIOHASH_DIM` | 512 | Projection dim = ArcFace feature dim |
| `BIOHASH_K` | 128 | Reliable bits selected |
| `BCH_M` | 9 | GF(2^9), codeword length n=511 |
| `BCH_T` | 25 | Error correction: up to 25 bits (~19.5%) |

> Templates auto-expire when they exceed the candidate seed scan range (~1 month).  
> See [bioh-bch/time_based_biohash_concept.md](bioh-bch/time_based_biohash_concept.md) for design details.

### Documentation

- [QUICKSTART.md](QUICKSTART.md) — Quick start guide
- [docs/PROJECT_STATUS.md](docs/PROJECT_STATUS.md) — Full project status (for AI collaboration)
- [bioh-bch/process_explanation.md](bioh-bch/process_explanation.md) — BioHash+BCH algorithm walkthrough
- [bioh-bch/time_based_biohash_concept.md](bioh-bch/time_based_biohash_concept.md) — Architecture & extension roadmap

### License

See [LICENSE](LICENSE) file for details.

---

<a name="chinese"></a>
## 中文

請參閱 [docs/PROJECT_STATUS.md](docs/PROJECT_STATUS.md) 取得完整中文專案狀態文件，  
或 [QUICKSTART.md](QUICKSTART.md) 快速上手。
