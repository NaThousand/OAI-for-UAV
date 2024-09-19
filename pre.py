"""
将919中的数据整合到特征值_2文件夹中,
并将标签1和标签0分别存放在特征值_2/标签1和特征值_2/标签0文件夹中
"""

import os
import shutil

data_dir = '919' 
output_dir_label1 = '特征值_2/标签1'
output_dir_label0 = '特征值_2/标签0'

os.makedirs(output_dir_label1, exist_ok=True)
os.makedirs(output_dir_label0, exist_ok=True)

for i in range(1, 6):
    sample_dir = os.path.join(data_dir, f'采样点{i}')
    
    for idx, filename in enumerate(os.listdir(sample_dir), start=1):
        if filename.startswith('ul') and filename.endswith('.txt'):
            new_filename = f'sample{i}_ul_{idx}.txt'
            file_path = os.path.join(sample_dir, filename)
            new_file_path = os.path.join(output_dir_label1, new_filename)
            shutil.copy(file_path, new_file_path)

for i in range(6, 11):
    sample_dir = os.path.join(data_dir, f'采样点{i}')
    
    for idx, filename in enumerate(os.listdir(sample_dir), start=1):
        if filename.startswith('ul') and filename.endswith('.txt'):
            new_filename = f'sample{i}_ul_{idx}.txt'
            file_path = os.path.join(sample_dir, filename)
            new_file_path = os.path.join(output_dir_label0, new_filename)
            shutil.copy(file_path, new_file_path)

print("文件整合完成")
