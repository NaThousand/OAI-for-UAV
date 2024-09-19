import os
import pandas as pd
import xgboost as xgb

# 定义测试数据目录
test_data_dir = 'test_data' 

# 初始化列表来存储测试数据
test_data = []

# 加载测试数据
for filename in os.listdir(test_data_dir):
    if filename.endswith('.txt'):
        file_path = os.path.join(test_data_dir, filename)
        with open(file_path, 'r') as file:
            content = file.read()
            features = list(map(float, content.split()))
            test_data.append(features)

# 转换为DataFrame
test_df = pd.DataFrame(test_data)

# 加载已保存的模型
model = xgb.Booster()
model.load_model('xgb_model.bin')

# 创建 DMatrix 用于预测
dtest = xgb.DMatrix(test_df)

# 进行预测
predictions = model.predict(dtest)

# 打印预测结果
print("Predictions:")
for prediction in predictions:
    print(prediction)

