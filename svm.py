import os
import pandas as pd
import numpy as np
from sklearn.svm import SVC  # 导入SVM分类器
from sklearn.model_selection import train_test_split, cross_val_score, learning_curve, GridSearchCV, StratifiedKFold
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix
import seaborn as sns
import matplotlib.pyplot as plt
from sklearn.preprocessing import StandardScaler
from imblearn.over_sampling import SMOTE
from tqdm import tqdm  # 导入SMOTE技术

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

# 检查类别分布
print(y_train.value_counts())  # 输出类别分布

# 使用SMOTE进行数据增强
# smote = SMOTE(random_state=42)
# X_train_resampled, y_train_resampled = smote.fit_resample(X_train, y_train)

# 1. 调整超参数 C 和 gamma
# param_grid = {
#     'C': [0.1, 1, 10, 100],  # 正则化参数
#     'gamma': [1, 0.1, 0.01, 0.001],  # RBF核的gamma参数
#     'kernel': ['rbf']  # 核函数为RBF
# }

# # 使用StratifiedKFold进行交叉验证
# cv = StratifiedKFold(n_splits=5)

# # 使用GridSearchCV进行参数调优
# grid = GridSearchCV(SVC(class_weight='balanced', random_state=42), param_grid, refit=True, verbose=2, cv=cv, n_jobs=-1)
# grid.fit(X_train, y_train)

# # 输出最佳参数
# print(f"最佳参数: {grid.best_params_}")

# 使用最佳参数训练后的模型进行预测
best_svm = SVC(C=1000, gamma=0.01, kernel='rbf', class_weight='balanced', random_state=42)
for i in tqdm(range(100),desc="Training progress"):
    best_svm.fit(X_train, y_train)
# 使用最佳参数训练模型
# best_svm.fit(X_train, y_train)

# 使用测试集进行预测
y_pred = best_svm.predict(X_test)
# 打印分类结果
accuracy = accuracy_score(y_test, y_pred)
print(f'Accuracy: {accuracy:.4f}')
print(classification_report(y_test, y_pred))

# 绘制混淆矩阵
# cm = confusion_matrix(y_test, y_pred)
# plt.figure(figsize=(8, 6))
# sns.heatmap(cm, annot=True, fmt="d", cmap="Blues")
# plt.title('Confusion Matrix')
# plt.ylabel('True Label')
# plt.xlabel('Predicted Label')
# plt.show()


# 确保数据的一致性
#print(f"X_train_resampled shape: {X_train_resampled.shape}")
#print(f"y_train_resampled shape: {y_train_resampled.shape}")

# 使用learning_curve绘制学习曲线
train_sizes, train_scores, test_scores = learning_curve(
    best_svm, X_train, y_train, cv=10, scoring='accuracy', n_jobs=-1, train_sizes=np.linspace(0.1, 1.0, 10)
)

train_scores_mean = np.mean(train_scores, axis=1)
test_scores_mean = np.mean(test_scores, axis=1)

# 绘制学习曲线
plt.figure(figsize=(10, 6))
plt.plot(train_sizes, train_scores_mean, 'o-', label="Train Accuracy")
plt.plot(train_sizes, test_scores_mean, 'o-', label="Validation Accuracy")
plt.xlabel('Training examples')
plt.ylabel('Accuracy')
plt.title('Learning Curve')
plt.legend(loc='best')
plt.grid(True)
plt.show()
