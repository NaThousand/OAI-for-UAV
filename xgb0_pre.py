import os
import pandas as pd
import numpy as np
import xgboost as xgb
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import accuracy_score, confusion_matrix
import seaborn as sns
import matplotlib.pyplot as plt

# 加载数据
label0_dir = '特征值/标签0'
label1_dir = '特征值/标签1'

data = []
labels = []

# 室内特征
for filename in os.listdir(label1_dir):
    if filename.endswith('.txt'):
        file_path = os.path.join(label1_dir, filename)
        with open(file_path, 'r') as file:
            content = file.read()
            features = list(map(float, content.split()))
            data.append(features)
            labels.append(1)

# 室外特征
for filename in os.listdir(label0_dir):
    if filename.endswith('.txt'):
        file_path = os.path.join(label0_dir, filename)
        with open(file_path, 'r') as file:
            content = file.read()
            features = list(map(float, content.split()))
            data.append(features)
            labels.append(0)

# 构造 DataFrame
df = pd.DataFrame(data)
df['Label'] = labels

X = df.drop(columns=['Label'])
y = df['Label']

# 数据标准化
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
    'eta': 0.05,  
    'gamma': 0.1,
    'max_depth': 4,
    'min_child_weight': 1,
    'subsample': 0.8,
    'colsample_bytree': 0.8,
    'lambda': 1,
    'alpha': 0.5,
    'scale_pos_weight': 1, 
    'objective': 'binary:logistic',
    'eval_metric': 'error'  # 错误率
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

# 计算混淆矩阵
#cm = confusion_matrix(y_val, y_pred_binary)
# plt.figure(figsize=(8, 6))
# sns.heatmap(cm, annot=True, fmt="d", cmap="Blues")
# plt.title('Confusion Matrix')
# plt.ylabel('True Label')
# plt.xlabel('Predicted Label')
# plt.show()

# 保存模型
bst.save_model('xgb_model_pre.bin')
