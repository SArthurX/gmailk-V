import numpy as np
import matplotlib.pyplot as plt
import os
import argparse
import json

def generate_synthetic_data(num_genuine=200, num_imposter=1000, dim=512):
    """
    Generates synthetic 512-D ArcFace features for initial testing.
    """
    # Simulate a single person's base feature
    base_face = np.random.randn(dim)
    base_face = base_face / np.linalg.norm(base_face)
    
    # Genuine data: base face + small noise
    genuine_data = []
    for _ in range(num_genuine):
        noise = np.random.randn(dim) * 0.05 # Simulate variation
        feat = base_face + noise
        feat = feat / np.linalg.norm(feat)
        genuine_data.append(feat)
        
    # Imposter data: completely random features
    imposter_data = []
    for _ in range(num_imposter):
        feat = np.random.randn(dim)
        feat = feat / np.linalg.norm(feat)
        imposter_data.append(feat)
        
    return np.array(genuine_data), np.array(imposter_data)

def generate_projection_matrix(out_dim=2048, in_dim=512, seed=42):
    """
    Generates the random projection matrix.
    """
    np.random.seed(seed)
    matrix = np.random.randn(out_dim, in_dim)
    return matrix

def extract_biohash_b(feature_512, matrix_2048x512, indices=None):
    """
    1. Project 512D to 2048D.
    2. Binarize using median.
    3. If indices=None (Enrollment): Select top 511 most stable bits and return them + indices.
       If indices is provided (Verification): Extract bits strictly using the provided indices.
    """
    # 1. Project
    projected = np.dot(matrix_2048x512, feature_512)
    
    # 2. Binarize using median
    median = np.median(projected)
    bits = (projected > median).astype(int)
    
    # 3. Bit Selection
    if indices is None:
        # 註冊階段 (Enrollment)：挑選離中位數最遠的 511 個位元
        distances = np.abs(projected - median)
        top_511_indices = np.argsort(distances)[::-1][:511]
        b_511 = bits[top_511_indices]
        return b_511, top_511_indices
    else:
        # 驗證階段 (Verification)：使用註冊時存下來的 Helper Data (indices) 取值
        b_511 = bits[indices]
        return b_511

def compute_hamming_distance(b1, b2):
    return np.sum(b1 != b2)

def load_real_data(genuine_path, imposter_path):
    print(f"Loading real genuine data from {genuine_path}...")
    print(f"Loading real imposter data from {imposter_path}...")
    
    def load_data(path):
        if path.endswith('.npy'):
            return np.load(path).astype(np.float32)
        elif path.endswith('.json'):
            with open(path, 'r') as f:
                data_dict = json.load(f)
            # 轉換 dict 的 values 成為 2D array
            return np.array(list(data_dict.values()), dtype=np.float32)
        else:
            raise ValueError(f"Unsupported file format: {path}")
    
    # 載入檔案
    genuine_data = load_data(genuine_path)
    imposter_data = load_data(imposter_path)
    
    # 如果是一維陣列，轉為二維 (N, 512)
    if genuine_data.ndim == 1:
        genuine_data = genuine_data.reshape(1, -1)
    if imposter_data.ndim == 1:
        imposter_data = imposter_data.reshape(1, -1)
        
    # 正規化 (與 ArcFace 保持一致)
    genuine_data = genuine_data / np.linalg.norm(genuine_data, axis=1, keepdims=True)
    imposter_data = imposter_data / np.linalg.norm(imposter_data, axis=1, keepdims=True)
    
    return genuine_data, imposter_data

def run_simulation(args):
    if args.genuine and args.imposter:
        genuine_features, imposter_features = load_real_data(args.genuine, args.imposter)
    else:
        print("Generating synthetic data (Warning: This is for testing only. Use --genuine and --imposter for actual analysis)...")
        genuine_features, imposter_features = generate_synthetic_data(num_genuine=200, num_imposter=1000)
    
    print("Generating projection matrix...")
    matrix = generate_projection_matrix(2048, 512)
    
    print("Extracting BioHashes and applying Reliable Bit Selection...")
    
    # 1. Enrollment (註冊)：使用 Genuine 第一張照片產生 Template 與 Helper Data (indices)
    base_hash, helper_data_indices = extract_biohash_b(genuine_features[0], matrix)
    
    # 2. Verification (本人驗證)：使用剩下的 Genuine 照片與「同一組 indices」提取特徵
    genuine_hashes = [base_hash]
    for i in range(1, len(genuine_features)):
        h = extract_biohash_b(genuine_features[i], matrix, indices=helper_data_indices)
        genuine_hashes.append(h)
        
    # 3. Imposter Verification (路人驗證)：使用 Imposter 照片與「同一組 indices」提取特徵
    imposter_hashes = [extract_biohash_b(f, matrix, indices=helper_data_indices) for f in imposter_features]
    
    print("Computing distances...")
    # Genuine distances (Base Hash vs All other Genuine Hashes)
    genuine_distances = []
    for j in range(1, len(genuine_hashes)):
        dist = compute_hamming_distance(base_hash, genuine_hashes[j])
        genuine_distances.append(dist)
            
    # Imposter distances (Base Hash vs All Imposter Hashes)
    imposter_distances = [compute_hamming_distance(base_hash, ih) for ih in imposter_hashes]
    
    # 1. Calculate stats strings
    gen_mean, gen_min, gen_max = np.mean(genuine_distances), np.min(genuine_distances), np.max(genuine_distances)
    imp_mean, imp_min, imp_max = np.mean(imposter_distances), np.min(imposter_distances), np.max(imposter_distances)

    stats_text = (
        f"--- Distance Statistics ---\n"
        f"Genuine: Mean={gen_mean:.2f}, Min={gen_min}, Max={gen_max}\n"
        f"Imposter: Mean={imp_mean:.2f}, Min={imp_min}, Max={imp_max}\n"
    )

    # 2. Calculate FAR/FRR table
    table_lines = [
        f"\n--- Error Rates for BCH t ---",
        f"{'BCH t':<6} | {'FAR (%)':<8} | {'FRR (%)':<8}",
        "-" * 30
    ]
    
    max_scan_t = min(200, int(np.max(genuine_distances)) + 15)
    t_values = list(range(0, max_scan_t, 5)) 
    
    optimal_t = None
    min_diff = float('inf')
    
    for t in t_values:
        far = sum(1 for d in imposter_distances if d <= t) / len(imposter_distances) * 100
        frr = sum(1 for d in genuine_distances if d > t) / len(genuine_distances) * 100
        table_lines.append(f"{t:<6} | {far:<8.2f} | {frr:<8.2f}")
        
        if abs(far - frr) < min_diff:
            min_diff = abs(far - frr)
            optimal_t = t
            
    table_text = "\n".join(table_lines)
    full_info_text = stats_text + table_text + f"\n\nOptimal t (EER): ~{optimal_t}"

    print(f"\n{stats_text}")
    print(table_text)
    print(f"\nEstimated Optimal t (EER point): ~{optimal_t}")

    # 3. Plotting
    plt.figure(figsize=(12, 7))
    ax = plt.gca()
    plt.hist(genuine_distances, bins=30, alpha=0.7, label='Genuine (Same Person)', color='blue', density=True)
    plt.hist(imposter_distances, bins=30, alpha=0.7, label='Imposter (Different People)', color='red', density=True)
    plt.axvline(x=255.5, color='gray', linestyle='--', label='Expected Random (255.5)')
    
    plt.title('Hamming Distance Distribution (BioHash 511-bit)')
    plt.xlabel('Hamming Distance (Errors)')
    plt.ylabel('Density')
    plt.legend(loc='upper left')
    plt.grid(True, alpha=0.3)

    # Add text box
    plt.text(0.98, 0.5, full_info_text, transform=ax.transAxes, fontsize=9,
             verticalalignment='center', horizontalalignment='right',
             family='monospace', bbox=dict(boxstyle='round,pad=0.5', facecolor='wheat', alpha=0.5))
    
    plt.tight_layout()
    plot_path = os.path.join(os.path.dirname(__file__), 'distance_histogram.png')
    plt.savefig(plot_path)
    print(f"Histogram saved to {plot_path}")

    if not (args.genuine and args.imposter):
        print("Note: This is based on synthetic data. Real ArcFace data will shift the distributions.")
        print("Please collect actual ArcFace features to determine the final system parameters.")
    else:
        print("Simulation complete using REAL ArcFace data.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="BioHash BCH Quantitative Analysis Simulator")
    parser.add_argument("--genuine", type=str, help="真實本人 ArcFace 特徵的路徑 (.npy 或 .json 檔案)")
    parser.add_argument("--imposter", type=str, help="真實路人 ArcFace 特徵的路徑 (.npy 或 .json 檔案)")
    args = parser.parse_args()
    
    run_simulation(args)
