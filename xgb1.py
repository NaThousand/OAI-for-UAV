"""
最新版XGBoost代码
进行了归一化处理
"""


import os
import pandas as pd
import numpy as np
import xgboost as xgb
from sklearn.model_selection import train_test_split
from sklearn.metrics import accuracy_score, confusion_matrix
import matplotlib.pyplot as plt
from sklearn.preprocessing import StandardScaler
import joblib

# 定义数据目录
label0_dir = '特征值_3/标签0'
label1_dir = '特征值_3/标签1'

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
joblib.dump(scaler, 'scaler.pkl')

# 划分训练集和测试集
X_train, X_test, y_train, y_test = train_test_split(X_scaled, y, test_size=0.2, random_state=42)

# 转换为DMatrix格式
dtrain = xgb.DMatrix(X_train, label=y_train)
dval = xgb.DMatrix(X_test, label=y_test)

# XGBoost 参数配置
params = {
    'booster': 'gbtree',
    'eta': 0.1,
    'gamma': 0.1,
    'max_depth': 4,
    'min_child_weight': 1,
    'subsample': 0.8,
    'colsample_bytree': 0.8,
    'lambda': 1,
    'alpha': 0.5,
    'scale_pos_weight': 1,
    'objective': 'binary:logistic',
    'eval_metric': 'error'
}

# 使用xgb.cv进行交叉验证
cv_results = xgb.cv(
    params,
    dtrain,
    num_boost_round=800,           # 最大训练轮次
    nfold=5,                       # 5折交叉验证
    early_stopping_rounds=60,      # 早停
    metrics={'error'},             # 使用错误率作为评估指标
    as_pandas=True,                # 结果保存为Pandas DataFrame
    seed=42
)

# 输出交叉验证的结果
print(cv_results)

# 获取最佳训练轮次
best_num_boost_round = cv_results['test-error-mean'].idxmin()
print(f'最佳训练轮次：{best_num_boost_round}')

# 计算交叉验证的准确率
cv_accuracy = 1 - cv_results['test-error-mean']  # 计算每次交叉验证的准确率
mean_cv_accuracy = cv_accuracy.mean()  # 计算平均准确率
print(f'交叉验证后的平均准确率: {mean_cv_accuracy:.4f}')


# 使用最佳轮次重新训练整个模型
bst = xgb.train(params, dtrain, num_boost_round=best_num_boost_round)

# 在测试集上进行预测
y_pred = bst.predict(dval)
y_pred_binary = np.round(y_pred)  # 将预测值转为二分类标签

# 计算准确率（基于测试集）
accuracy = accuracy_score(y_test, y_pred_binary)  # 计算测试集的准确率
print(f'测试集的准确率: {accuracy:.4f}')

# 绘制训练和验证准确率随训练轮次变化的曲线
epochs = len(cv_results)
x_axis = range(0, epochs)

# 提取训练和测试准确率
train_accuracy = 1 - cv_results['train-error-mean']
val_accuracy = 1 - cv_results['test-error-mean']

plt.figure(figsize=(10, 6))
plt.plot(x_axis, train_accuracy, label='Train Accuracy', marker='o')
plt.plot(x_axis, val_accuracy, label='Validation Accuracy', marker='o')
plt.xlabel('Boosting Rounds')
plt.ylabel('Accuracy')
plt.title('XGBoost Training and Validation Accuracy Curve')
plt.legend()
plt.grid(True)
plt.show()

# 保存模型
bst.save_model('xgb_model_cv.bin')
