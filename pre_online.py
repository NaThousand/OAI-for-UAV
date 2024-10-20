"""
对实时变化的文件进行检测并用模型进行预测，输出预测结果
"""

import time
import xgboost as xgb
import numpy as np
import pandas as pd
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler
import joblib


# 加载已保存的标准化器
scaler = joblib.load('scaler.pkl')

# 设置要监控的目录
watch_dir = '1014'

# 加载预训练好的 XGBoost 模型
model_path = 'xgb_model_cv.bin'
bst = xgb.Booster()
bst.load_model(model_path)

# 用于记录最近处理的文件和时间
last_processed_file = None
last_processed_time = 0
process_interval = 2  # 设置时间间隔（秒），避免重复处理

def parse_file_content(file_path):
    try:
        with open(file_path, 'r') as file:
            content = file.read()
            features = list(map(float, content.replace(',', ' ').split()))
        return features
    except ValueError as e:
        print(f"Error parsing file {file_path}: {e}")
        return None

class FileHandler(FileSystemEventHandler):
    def on_modified(self, event):
        global last_processed_file, last_processed_time
        if event.src_path.endswith('.txt'):
            current_time = time.time()
            if (event.src_path == last_processed_file and 
                    current_time - last_processed_time < process_interval):
                return
            
            print(f"Detected file change: {event.src_path}")

            # 更新最近处理的文件和时间
            last_processed_file = event.src_path
            last_processed_time = current_time

            features = parse_file_content(event.src_path)
            if features:
                df = pd.DataFrame([features])
                X_scaled = scaler.transform(df)

                dmatrix = xgb.DMatrix(X_scaled)

                prediction = bst.predict(dmatrix)
                predicted_label = np.round(prediction).astype(int)

                # 输出预测结果
                result_file = 'prediction_results.txt'
                with open(result_file, 'a') as f:
                    f.write(f'{predicted_label[0]}\n')
                print(f'Prediction for {event.src_path}: {predicted_label[0]}')
            else:
                print(f"Failed to parse file: {event.src_path}")

# 监控目录并处理文件变化
event_handler = FileHandler()
observer = Observer()
observer.schedule(event_handler, watch_dir, recursive=True)
observer.start()

try:
    print(f"Monitoring directory: {watch_dir}")
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    observer.stop()

observer.join()
