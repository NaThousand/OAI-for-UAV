"""
针对919里的数据进行训练, 并保存模型
(919)里的数据经过处理后, 保存在特征值_2中
"""
import os
import pandas as pd
import numpy as np
import xgboost as xgb
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import accuracy_score, confusion_matrix
import seaborn as sns
import matplotlib.pyplot as plt

label0_dir = '特征值_2/标签0'
label1_dir = '特征值_2/标签1'

data = []
labels = []

# 处理函数：解析整个文件的内容，将其作为一个特征向量
def parse_file_content(content):
    try:
        features = list(map(float, content.replace(',', ' ').split()))  # 使用空格替换逗号并拆分
    except ValueError as e:
        print(f"Error parsing file content: {e}")
        features = []
    return features

# 室内特征
for filename in os.listdir(label1_dir):
    if filename.endswith('.txt'):
        file_path = os.path.join(label1_dir, filename)
        with open(file_path, 'r') as file:
            content = file.read()
            features = parse_file_content(content)
            if features:  
                data.append(features)  
                labels.append(1) 

# 室外特征
for filename in os.listdir(label0_dir):
    if filename.endswith('.txt'):
        file_path = os.path.join(label0_dir, filename)
        with open(file_path, 'r') as file:
            content = file.read()
            features = parse_file_content(content)  
            if features: 
                data.append(features)  
                labels.append(0)  
                #测试features是否正确
                #print(f"Feature from file {filename}: {features[:10]}")  
                #print(f"Feature length from file {filename}: {len(features)}")

            
df = pd.DataFrame(data)

# 检查数据和标签长度是否匹配
if len(data) != len(labels):
    print(f"Data length ({len(data)}) and labels length ({len(labels)}) do not match!")
else:
    df['Label'] = labels

X = df.drop(columns=['Label'])
y = df['Label']

# 数据归一化处理
scaler = StandardScaler()
X_scaled = scaler.fit_transform(X)

# 划分训练集和验证集
X_train, X_val, y_train, y_val = train_test_split(X_scaled, y, test_size=0.2, random_state=42)

# 转换为DMatrix格式
dtrain = xgb.DMatrix(X_train, label=y_train)
dval = xgb.DMatrix(X_val, label=y_val)

# XGBoost 参数配置
params = {
    'booster': 'gbtree',
    'eta': 0.05, #可调
    'gamma': 0.1,
    'max_depth': 4,
    'min_child_weight': 1,
    'subsample': 0.8,
    'colsample_bytree': 0.8,
    'lambda': 1,
    'alpha': 0.5,
    'scale_pos_weight': 1,  #可选
    'objective': 'binary:logistic',
    'eval_metric': 'error'  # 评估标准
}

# 用于存储评估结果
evals_result = {}

# 训练模型，记录评估结果
evals = [(dval, 'eval'), (dtrain, 'train')]
bst = xgb.train(params, dtrain, num_boost_round=200, evals=evals, early_stopping_rounds=20, evals_result=evals_result)

# 绘制随训练轮次增加，训练集和验证集的准确率变化
epochs = len(evals_result['train']['error'])
x_axis = range(0, epochs)

# 转换错误率为准确率（1 - error）
train_accuracy = [1 - error for error in evals_result['train']['error']]
val_accuracy = [1 - error for error in evals_result['eval']['error']]

plt.figure(figsize=(10, 6))
plt.plot(x_axis, train_accuracy, label='Train Accuracy', marker='o')
plt.plot(x_axis, val_accuracy, label='Validation Accuracy', marker='o')
plt.xlabel('Epochs')
plt.ylabel('Accuracy')
plt.title('XGBoost Accuracy Curve')
plt.legend()
plt.grid(True)
plt.show()

# 模型预测和保存
y_pred = bst.predict(dval)
y_pred_binary = np.round(y_pred)

accuracy = accuracy_score(y_val, y_pred_binary)
print(f'验证集准确性：{accuracy:.4f}')

# 绘制混淆矩阵
# cm = confusion_matrix(y_val, y_pred_binary)
# plt.figure(figsize=(8, 6))
# sns.heatmap(cm, annot=True, fmt="d", cmap="Blues")
# plt.title('Confusion Matrix')
# plt.ylabel('True Label')
# plt.xlabel('Predicted Label')
# plt.show()

# 保存模型
bst.save_model('xgb_model_pre.bin')
