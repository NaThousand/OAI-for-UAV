import time
import xgboost as xgb
import numpy as np
import pandas as pd
import joblib
import os
import hashlib
from tqdm import tqdm  

# 加载标准化器
scaler = joblib.load('scaler.pkl')

# 设置要监控的文件夹路径
watch_directory = '1014/采样点0'  
output_file='output.txt'  

# 加载预训练好的 XGBoost 模型
model_path = 'xgb_model_cv.bin'
bst = xgb.Booster()
bst.load_model(model_path)

# 记录文件内容的哈希值，确保只有内容变更时才处理
file_hashes = {}
predictions = []

def calculate_file_hash(file_path):
    hasher = hashlib.md5()
    with open(file_path, 'rb') as f:
        buf = f.read()
        hasher.update(buf)
    return hasher.hexdigest()

def parse_file_content(file_path):
    try:
        with open(file_path, 'r') as file:
            content = file.read()
            features = list(map(float, content.replace(',', ' ').split()))
        return features
    except ValueError as e:
        print(f"Error parsing file {file_path}: {e}")
        return None

def process_file(file_path):
    features = parse_file_content(file_path)
    if features:
        df = pd.DataFrame([features])
        X_scaled = scaler.transform(df)
        dmatrix = xgb.DMatrix(X_scaled)
        prediction = bst.predict(dmatrix)
        predicted_label = int(np.round(prediction)[0])
        return predicted_label
    else:
        print(f"Failed to parse file: {file_path}")
        return None



for file_name in os.listdir(watch_directory):
    if file_name.endswith('.txt'):
        file_path = os.path.join(watch_directory, file_name)
        file_hashes[file_name] = calculate_file_hash(file_path)


progress_bar = tqdm(total=100, desc="Processing Predictions", unit="file")

# 实时监测文件夹变化
try:
    print(f"Monitoring directory: {watch_directory}")
    while True:
        current_files = [f for f in os.listdir(watch_directory) if f.endswith('.txt')]
        
        for file_name in current_files:
            file_path = os.path.join(watch_directory, file_name)
            current_hash = calculate_file_hash(file_path)

            # 检查文件内容是否更新（通过哈希值判断）
            if file_name not in file_hashes or file_hashes[file_name] != current_hash:
                file_hashes[file_name] = current_hash 

                predicted_label = process_file(file_path)
                if predicted_label is not None:
                    predictions.append(predicted_label)
                    progress_bar.update(1)  

                # 检查是否已经有 100 个预测
                if len(predictions) >= 100:
                    count_1 = predictions.count(1)
                    count_0 = len(predictions) - count_1
                    final_output = 1 if count_1 > count_0 else 0
                    print(f"\nFinal Output: {final_output}")

                    with open(output_file, 'a') as f:
                        f.write(f'{final_output}\n')
                    print(f"\nFinal Output: {final_output} written to {output_file}")
                    
                    # 清空预测记录，重置进度条
                    predictions.clear()
                    progress_bar.reset()

        time.sleep(0.05)  
except KeyboardInterrupt:
    print("\nMonitoring stopped.")
    progress_bar.close()
