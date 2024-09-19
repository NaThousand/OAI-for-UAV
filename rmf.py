#random forest classification
import os
import pandas as pd
import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split
from sklearn.metrics import accuracy_score,classification_report
import joblib



# 定义数据目录
label0_dir = '特征值_2/标签0'
label1_dir = '特征值_2/标签1'

# label0_dir='特征值_1/室外特征_1'
# label1_dir='特征值_1/室内特征_1'

labels=[]
data=[]

def parse_file_content(content):
    try:
        features = list(map(float, content.replace(',', ' ').split()))  # 使用空格替换逗号并拆分
    except ValueError as e:
        print(f"Error parsing file content: {e}")
        features = []
    return features

#加载文件
for filename in os.listdir(label1_dir):
    if filename.endswith('.txt'):
        file_path=os.path.join(label1_dir,filename)
        with open(file_path,'r') as file:
            content=file.read()
            features = parse_file_content(content)
            if features:  
                data.append(features)  
                labels.append(1) 

for filename in os.listdir(label0_dir):
    if filename.endswith('.txt'):
        file_path=os.path.join(label0_dir,filename)
        with open(file_path,'r') as file:
            content=file.read()
            features = parse_file_content(content)
            if features:  
                data.append(features)  
                labels.append(0) 


df=pd.DataFrame(data)
df['Label']=labels

#分离特征和目标变量
X=df.drop(columns=['Label'])
y=df['Label']

#分离训练集和测试集
X_train,X_test,y_train,y_test=train_test_split(X,y,test_size=0.2,random_state=42)

#创建随机森林分类器
rf_classifier=RandomForestClassifier(n_estimators=100,random_state=42)

#训练
rf_classifier.fit(X_train,y_train)

#预测
y_pred=rf_classifier.predict(X_test)

accuracy=accuracy_score(y_test,y_pred)
print(f'Aaccuracy:{accuracy:.4f}')
print(classification_report(y_test,y_pred))

joblib.dump(rf_classifier,'random_forest_model.pkl')