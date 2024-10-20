import os
import pandas as pd
import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split, cross_val_score, learning_curve
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix
import seaborn as sns
import matplotlib.pyplot as plt
from sklearn.preprocessing import StandardScaler

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

# 加载文件并处理数据
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

# 创建 DataFrame
df = pd.DataFrame(data)
df['Label'] = labels

# 分离特征和目标变量
X = df.drop(columns=['Label'])
y = df['Label']

# 数据归一化处理
scaler = StandardScaler()
X_scaled = scaler.fit_transform(X)

# 分离训练集和测试集
X_train, X_test, y_train, y_test = train_test_split(X_scaled, y, test_size=0.2, random_state=42)

# 创建随机森林模型
rf_classifier = RandomForestClassifier(
    n_estimators=500,
    max_depth=None,
    min_samples_split=5,
    min_samples_leaf=5,
    random_state=42
)

# 交叉验证评估模型
cv_scores = cross_val_score(rf_classifier, X_scaled, y, cv=5, scoring='accuracy')
print(f'交叉验证平均准确率: {cv_scores.mean():.4f}')

# 训练模型
rf_classifier.fit(X_train, y_train)

# 使用训练好的模型进行预测
y_pred = rf_classifier.predict(X_test)

# 打印测试集的准确率和分类报告
accuracy = accuracy_score(y_test, y_pred)
print(f'测试集的准确率: {accuracy:.4f}')
print(classification_report(y_test, y_pred))

# 可视化混淆矩阵
cm = confusion_matrix(y_test, y_pred)
plt.figure(figsize=(8, 6))
sns.heatmap(cm, annot=True, fmt="d", cmap="Blues")
plt.title('Confusion Matrix - Random Forest')
plt.ylabel('True Label')
plt.xlabel('Predicted Label')
plt.show()

# 绘制学习曲线
train_sizes, train_scores, test_scores = learning_curve(
    rf_classifier, X_scaled, y, cv=5, scoring='accuracy', n_jobs=-1, train_sizes=np.linspace(0.1, 1.0, 10)
)

train_scores_mean = np.mean(train_scores, axis=1)
test_scores_mean = np.mean(test_scores, axis=1)

plt.figure(figsize=(10, 6))
plt.plot(train_sizes, train_scores_mean, 'o-', label="Train Accuracy")
plt.plot(train_sizes, test_scores_mean, 'o-', label="Validation Accuracy")
plt.xlabel('Training examples')
plt.ylabel('Accuracy')
plt.title('Learning Curve')
plt.legend(loc='best')
plt.grid(True)
plt.show()

# 保存训练好的模型
# joblib.dump(rf_classifier, 'random_forest_model.pkl')
