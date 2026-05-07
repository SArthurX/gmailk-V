import json
import numpy as np

with open('./dataset/output.json', 'r') as f:
    data = json.load(f)

features = list(data.values())
feat1 = np.array(features[2], dtype=np.float32)
feat2 = np.array(features[3], dtype=np.float32)

feat1 = feat1 / np.linalg.norm(feat1)
feat2 = feat2 / np.linalg.norm(feat2)
similarity = np.dot(feat1, feat2)

print(f"Cosine Similarity: {similarity}")
