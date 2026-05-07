#include <iostream>
#include <fstream>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>
#include <string>
#include <cstring>

#include "cvi_tdl.h"
#include "cvi_tdl_media.h"
#include "face_feature_extractor.h"

extern "C" {
#include <cvi_sys.h>
#include <cvi_vb.h>
}

// Check if a file ends with a specific suffix
bool endsWith(const std::string& str, const std::string& suffix) {
    if (str.length() >= suffix.length()) {
        return (0 == str.compare(str.length() - suffix.length(), suffix.length(), suffix));
    }
    return false;
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0] << " <scrfd_model> <arcface_model> <image_dir> <output.json>" << std::endl;
        return -1;
    }

    std::string scrfd_path = argv[1];
    std::string arcface_path = argv[2];
    std::string image_dir = argv[3];
    std::string output_json = argv[4];

    // Minimal SYS and VB initialization
    CVI_SYS_Exit();
    CVI_VB_Exit();
    
    VB_CONFIG_S stVbConf;
    memset(&stVbConf, 0, sizeof(VB_CONFIG_S));
    stVbConf.u32MaxPoolCnt = 2;
    // Pool 0: for image data (support up to 1080p)
    stVbConf.astCommPool[0].u32BlkSize = 1920 * 1080 * 3;
    stVbConf.astCommPool[0].u32BlkCnt = 3;
    stVbConf.astCommPool[0].enRemapMode = VB_REMAP_MODE_CACHED;
    // Pool 1: for TDL/VPSS preprocessing (BGR_888_PLANAR, matching main program's Pool 2)
    stVbConf.astCommPool[1].u32BlkSize = 1280 * 720 * 3;
    stVbConf.astCommPool[1].u32BlkCnt = 3;
    stVbConf.astCommPool[1].enRemapMode = VB_REMAP_MODE_CACHED;
    
    CVI_S32 ret = CVI_VB_SetConfig(&stVbConf);
    if (ret != CVI_SUCCESS) {
        std::cerr << "CVI_VB_SetConfig failed! ret=0x" << std::hex << ret << std::endl;
        return -1;
    }
    
    ret = CVI_VB_Init();
    if (ret != CVI_SUCCESS) {
        std::cerr << "CVI_VB_Init failed! ret=0x" << std::hex << ret << std::endl;
        return -1;
    }
    
    ret = CVI_SYS_Init();
    if (ret != CVI_SUCCESS) {
        std::cerr << "CVI_SYS_Init failed! ret=0x" << std::hex << ret << std::endl;
        CVI_VB_Exit();
        return -1;
    }

    // Initialize TDL
    cvitdl_handle_t tdl_handle;
    ret = CVI_TDL_CreateHandle(&tdl_handle);
    if (ret != CVI_SUCCESS) {
        std::cerr << "CVI_TDL_CreateHandle failed!" << std::endl;
        return -1;
    }

    // Use Pool 1 for TDL VPSS preprocessing (Pool 0 is for image data)
    ret = CVI_TDL_SetVBPool(tdl_handle, 0, 1);
    if (ret != CVI_SUCCESS) {
        std::cerr << "CVI_TDL_SetVBPool failed!" << std::endl;
        return -1;
    }

    CVI_TDL_SetVpssTimeout(tdl_handle, 1000);

    // Load Models
    ret = CVI_TDL_OpenModel(tdl_handle, CVI_TDL_SUPPORTED_MODEL_SCRFDFACE, scrfd_path.c_str());
    if (ret != CVI_SUCCESS) {
        std::cerr << "Failed to open SCRFDFACE model!" << std::endl;
        return -1;
    }

    FaceFeatureExtractor feature_extractor(arcface_path, tdl_handle);
    if (!feature_extractor.isLoaded()) {
        std::cerr << "Failed to load ArcFace model!" << std::endl;
        return -1;
    }

    imgprocess_t img_handle;
    CVI_TDL_Create_ImageProcessor(&img_handle);

    // Open output file
    std::ofstream ofs(output_json);
    if (!ofs.is_open()) {
        std::cerr << "Failed to open output file: " << output_json << std::endl;
        return -1;
    }
    ofs << "{\n";

    // Read directory
    DIR* dir = opendir(image_dir.c_str());
    if (!dir) {
        std::cerr << "Failed to open directory: " << image_dir << std::endl;
        return -1;
    }

    struct dirent* entry;
    bool first_entry = true;
    int success_count = 0;

    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        if (filename == "." || filename == "..") continue;
        
        if (!endsWith(filename, ".jpg") && !endsWith(filename, ".jpeg") && !endsWith(filename, ".png")) {
            continue;
        }

        std::string filepath = image_dir + "/" + filename;
        std::cout << "Processing: " << filepath << std::endl;

        VIDEO_FRAME_INFO_S frame;
        memset(&frame, 0, sizeof(VIDEO_FRAME_INFO_S));

        // Read Image
        ret = CVI_TDL_ReadImage(img_handle, filepath.c_str(), &frame, PIXEL_FORMAT_RGB_888_PLANAR);
        if (ret != CVI_SUCCESS) {
            std::cerr << "  ⚠️ Failed to read image!" << std::endl;
            continue;
        }

        // Detect Face
        cvtdl_face_t face_meta;
        memset(&face_meta, 0, sizeof(cvtdl_face_t));
        ret = CVI_TDL_FaceDetection(tdl_handle, &frame, CVI_TDL_SUPPORTED_MODEL_SCRFDFACE, &face_meta);

        if (ret == CVI_TDL_SUCCESS && face_meta.size > 0) {
            // Pick largest face
            int best_idx = 0;
            float max_area = 0;
            for (uint32_t i = 0; i < face_meta.size; i++) {
                float area = (face_meta.info[i].bbox.x2 - face_meta.info[i].bbox.x1) * 
                             (face_meta.info[i].bbox.y2 - face_meta.info[i].bbox.y1);
                if (area > max_area) {
                    max_area = area;
                    best_idx = i;
                }
            }

            std::vector<float> feature;
            ret = feature_extractor.extractFeature(&frame, &face_meta.info[best_idx], feature);
            
            if (ret == CVI_SUCCESS && feature.size() == 512) {
                if (!first_entry) {
                    ofs << ",\n";
                }
                ofs << "  \"" << filename << "\": [";
                for (size_t i = 0; i < feature.size(); i++) {
                    ofs << feature[i] << (i < feature.size() - 1 ? ", " : "");
                }
                ofs << "]";
                first_entry = false;
                success_count++;
                std::cout << "  ✅ Extracted 512-D feature" << std::endl;
            } else {
                std::cerr << "  ❌ Failed to extract feature" << std::endl;
            }
        } else {
            std::cerr << "  ❌ No face detected" << std::endl;
        }

        CVI_TDL_Free(&face_meta);
        CVI_TDL_ReleaseImage(img_handle, &frame);
    }
    
    ofs << "\n}\n";
    ofs.close();
    closedir(dir);

    CVI_TDL_Destroy_ImageProcessor(img_handle);
    CVI_TDL_DestroyHandle(tdl_handle);
    CVI_SYS_Exit();
    CVI_VB_Exit();

    std::cout << "-----------------------------------" << std::endl;
    std::cout << "Done! Successfully processed " << success_count << " images." << std::endl;
    std::cout << "Results saved to " << output_json << std::endl;

    return 0;
}
