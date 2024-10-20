import tensorflow as tf
# 检查是否有可用的 GPU
print("Num GPUs Available: ", len(tf.config.list_physical_devices('GPU')))

# 创建简单的张量计算
a = tf.constant([[1.0, 2.0], [3.0, 4.0]])
b = tf.constant([[1.0, 1.0], [0.0, 1.0]])
c = tf.matmul(a, b)

print("Result of matrix multiplication:")
print(c)

#import torch
 
# 检查CUDA是否可用
#cuda_available = torch.cuda.is_available()
 
#if cuda_available:
#     print("CUDA is available!")
# else:
#     print("CUDA is not available.")
 
# # # 检查是否有可用的GPU
# device_count = torch.cuda.device_count()
# if device_count > 0:
#     print(f"There are {device_count} GPU(s) available.")
#     device = torch.device("cuda:0" if torch.cuda.is_available() else "cpu")
# else:
#     print("No GPU available.")
#     device = torch.device("cpu")