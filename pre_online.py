import time
import xgboost as xgb
import numpy as np
import pandas as pd
import joblib
import os

# 加载已保存的标准化器
scaler = joblib.load('scaler.pkl')

# 设置要监控的文件路径
watch_file = '1014/采样点1/ul_ch_estimates_1.txt'  # 修改为你的特定txt文件

# 加载预训练好的 XGBoost 模型
model_path = 'xgb_model_cv.bin'
bst = xgb.Booster()
bst.load_model(model_path)

# 用于记录上次处理的时间
last_processed_time = 0
process_interval = 2  # 设置时间间隔（秒）

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
    """处理文件并进行预测"""
    features = parse_file_content(file_path)
    if features:
        df = pd.DataFrame([features])
        X_scaled = scaler.transform(df)

        dmatrix = xgb.DMatrix(X_scaled)

        prediction = bst.predict(dmatrix)
        predicted_label = np.round(prediction).astype(int)

        # 输出预测结果
        result_file = 'prediction_results.txt'
        with open(result_file, 'w') as f:
            f.write(f'{predicted_label[0]}\n')
        print(f'Prediction for {file_path}: {predicted_label[0]}')
    else:
        print(f"Failed to parse file: {file_path}")

# 主监测循环
try:
    print(f"Monitoring file: {watch_file}")
    while True:
        # 获取文件的最后修改时间
        current_mod_time = os.path.getmtime(watch_file)
        
        # 如果文件发生了变化（根据修改时间判断）
        if current_mod_time != last_processed_time:
            print(f"Detected file change: {watch_file}")
            process_file(watch_file)
            last_processed_time = current_mod_time  # 更新最后处理时间
        
        time.sleep(process_interval)  # 每隔2秒检测一次
except KeyboardInterrupt:
    print("Monitoring stopped.")
