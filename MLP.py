import os
import pandas as pd
import numpy as np
from sklearn.model_selection import train_test_split, learning_curve
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix
from sklearn.preprocessing import StandardScaler
from sklearn.neural_network import MLPClassifier
import seaborn as sns
import matplotlib.pyplot as plt

# 定义数据目录
label0_dir = '特征值_3/标签0'
label1_dir = '特征值_3/标签1'

labels = []
data = []

def parse_file_content(content):
    try:
        features = list(map(float, content.replace(',', ' ').split()))  # 使用空格替换逗号并拆分
    except ValueError as e:
        print(f"Error parsing file content: {e}")
        features = []
    return features

# 加载文件
for filename in os.listdir(label1_dir):
    if filename.endswith('.txt'):
        file_path = os.path.join(label1_dir, filename)
        with open(file_path, 'r') as file:
            content = file.read()
            features = parse_file_content(content)
            if features:
                data.append(features)
                labels.append(1)

for filename in os.listdir(label0_dir):
    if filename.endswith('.txt'):
        file_path = os.path.join(label0_dir, filename)
        with open(file_path, 'r') as file:
            content = file.read()
            features = parse_file_content(content)
            if features:
                data.append(features)
                labels.append(0)

# 创建DataFrame
df = pd.DataFrame(data)
df['Label'] = labels

# 分离特征和目标变量
X = df.drop(columns=['Label'])
y = df['Label']

# 数据标准化处理
scaler = StandardScaler()
X_scaled = scaler.fit_transform(X)

# 分离训练集和测试集
X_train, X_test, y_train, y_test = train_test_split(X_scaled, y, test_size=0.2, random_state=42)
print(f"训练集样本数量: {X_train.shape[0]}")

# 创建MLP神经网络模型
mlp = MLPClassifier(hidden_layer_sizes=(64, 32), max_iter=500, activation='relu', solver='adam', random_state=42)

# 训练模型
mlp.fit(X_train, y_train)

# 使用测试集进行预测
y_pred = mlp.predict(X_test)

# 打印分类结果
accuracy = accuracy_score(y_test, y_pred)
print(f'Accuracy: {accuracy:.4f}')
print(classification_report(y_test, y_pred))

# 绘制混淆矩阵
cm = confusion_matrix(y_test, y_pred)
plt.figure(figsize=(8, 6))
sns.heatmap(cm, annot=True, fmt="d", cmap="Blues")
plt.title('Confusion Matrix')
plt.ylabel('True Label')
plt.xlabel('Predicted Label')
plt.show()

# 使用learning_curve绘制学习曲线
train_sizes, train_scores, test_scores = learning_curve(
    mlp, X_train, y_train, cv=5, scoring='accuracy', n_jobs=-1, train_sizes=np.linspace(0.1,1.0,10)
)

train_scores_mean = np.mean(train_scores, axis=1)
test_scores_mean = np.mean(test_scores, axis=1)

# 绘制学习曲线
plt.figure(figsize=(10, 6))
plt.plot(train_sizes, train_scores_mean, 'o-', label="Train Accuracy")
plt.plot(train_sizes, test_scores_mean, 'o-', label="Validation Accuracy")
plt.xlabel('Training examples')
plt.ylabel('Accuracy')
plt.title('MLP Learning Curve')
plt.legend(loc='best')
plt.grid(True)
plt.show()
