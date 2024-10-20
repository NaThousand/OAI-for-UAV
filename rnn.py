#RNN模型训练
import os
import pandas as pd
import numpy as np
from sklearn.preprocessing import StandardScaler
from sklearn.model_selection import train_test_split  # 导入train_test_split
import tensorflow as tf
from tensorflow.keras.models import Sequential # type: ignore
from tensorflow.keras.layers import Dense, SimpleRNN # type: ignore
import matplotlib.pyplot as plt

print("Num GPUs Available: ", len(tf.config.experimental.list_physical_devices('GPU')))
print(tf.config.experimental.list_logical_devices('GPU'))
# 定义数据目录
label0_dir = '特征值_3/标签0'
label1_dir = '特征值_3/标签1'

data = []
labels = []

def parse_file_content(content):
    try:
        features = list(map(float, content.replace(',', ' ').split()))  # 使用空格替换逗号并拆分
    except ValueError as e:
        print(f"Error parsing file content: {e}")
        features = []
    return features


# 加载标签为1的文件
for filename in os.listdir(label1_dir):
    if filename.endswith('.txt'):
        file_path = os.path.join(label1_dir, filename)
        with open(file_path, 'r') as file:
            content = file.read()
            features = parse_file_content(content)
            if features:  
                data.append(features)  
                labels.append(1) 

# 加载标签为0的文件
for filename in os.listdir(label0_dir):
    if filename.endswith('.txt'):
        file_path = os.path.join(label0_dir, filename)
        with open(file_path, 'r') as file:
            content = file.read()
            features = parse_file_content(content)
            if features:  
                data.append(features)  
                labels.append(0) 

# 转换为DataFrame
df = pd.DataFrame(data)
df['Label'] = labels

# 分离特征和目标变量
X = df.drop(columns=['Label'])
y = df['Label']

# 标准化数据
scaler = StandardScaler()
X_scaled = scaler.fit_transform(X)

# 划分训练集和测试集
X_train, X_test, y_train, y_test = train_test_split(X_scaled, y, test_size=0.2, random_state=42)

# 重塑数据以适应RNN输入 (假设每个样本有 n_features 个特征)
n_features = X_train.shape[1]
X_train = X_train.reshape((X_train.shape[0], 1, n_features))
X_test = X_test.reshape((X_test.shape[0], 1, n_features))

# 构建RNN模型
model = Sequential()
model.add(SimpleRNN(50, activation='relu', input_shape=(1, n_features)))
model.add(Dense(1, activation='sigmoid'))  # 二分类问题

# 编译模型
model.compile(optimizer='adam', loss='binary_crossentropy', metrics=['accuracy'])

# 训练模型，并存储训练过程中的数据
history = model.fit(X_train, y_train, epochs=400, batch_size=32, validation_data=(X_test, y_test))

# 绘制训练集和测试集准确率随epoch变化的曲线
plt.figure(figsize=(10, 6))
plt.plot(history.history['accuracy'], label='Train Accuracy')
plt.plot(history.history['val_accuracy'], label='Test Accuracy')
plt.title('Accuracy Over Epochs')
plt.xlabel('Epochs')
plt.ylabel('Accuracy')
plt.legend()
plt.grid(True)
plt.show()

# 预测测试集
y_pred = (model.predict(X_test) > 0.5).astype("int32")

# 评估模型在测试集上的准确率
accuracy = np.mean(y_pred.flatten() == y_test)
print(f'Final Test Accuracy: {accuracy:.4f}')
