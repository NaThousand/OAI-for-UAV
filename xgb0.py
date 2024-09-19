"""
针对数据集：特征值
未进行数据归一化处理
"""

import os
import pandas as pd
import numpy as np
import xgboost as xgb
from sklearn.model_selection import train_test_split
from sklearn.metrics import accuracy_score, confusion_matrix
import seaborn as sns
import matplotlib.pyplot as plt

label0_dir='特征值/标签0'
label1_dir='特征值/标签1'

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


df = pd.DataFrame(data)
df['Label'] = labels
X = df.drop(columns=['Label'])
y = df['Label']

# 划分训练集和验证集
X_train, X_val, y_train, y_val = train_test_split(X, y, test_size=0.2, random_state=42)

dtrain = xgb.DMatrix(X_train, label=y_train)
dval = xgb.DMatrix(X_val, label=y_val)

# XGBoost 参数配置
params = {
    'booster': 'gbtree',
    'eta': 0.1,
    'gamma': 0,
    'max_depth': 3,
    'min_child_weight': 5,
    'max_delta_step': 0,
    'subsample': 1,
    'sampling_method': 'uniform',
    'lambda': 10,
    'alpha': 5,
    'tree_method': 'auto',
    'objective': 'binary:logistic',
    'eval_metric': 'error'  # 错误率
}

# 用于存储评估结果
evals_result = {}

# 训练模型，记录评估结果
evals = [(dval, 'eval'), (dtrain, 'train')]
bst = xgb.train(params, dtrain, num_boost_round=300, evals=evals, early_stopping_rounds=10, evals_result=evals_result)

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
cm = confusion_matrix(y_val, y_pred_binary)

# 绘制混淆矩阵
plt.figure(figsize=(8, 6))
sns.heatmap(cm, annot=True, fmt="d", cmap="Blues")
plt.title('Confusion Matrix')
plt.ylabel('True Label')
plt.xlabel('Predicted Label')
plt.show()

# 保存模型
bst.save_model('xgb_model.bin')
