<!-- <template>
  <div class="page-wrapper">
    <div class="three-d-container">
      <ThreeDView />
    </div>
  </div>
</template>

<script>
import ThreeDView from '@/components/CubeView/ThreeDView.vue'

export default {
  name: 'Dashboard',
  components: {
    ThreeDView
  }
}
</script>

<style scoped>
.page-wrapper {
  background-color: #ffffff; /* 背景颜色 */
  width: 100%;
  height: 300px; /* 保持父容器高度不变 */
  display: flex;
  justify-content: center; /* 水平居中 */
  align-items: center; /* 垂直居中 */
}

.three-d-container {
  display: flex;
  justify-content: center;
  align-items: center;
  width: 100%;
  height: 100%;
  /* 去除任何内外边距，以确保没有额外的空隙 */
  margin: 0;
  padding: 0;
}

.three-d-container > * {
  width: 100%; /* 让子组件的宽度自适应父容器 */
  height: 100%; /* 让子组件的高度自适应父容器 */
  flex-grow: 1; /* 确保子组件填满整个容器 */
  box-sizing: border-box; /* 确保宽度计算正确 */
}
</style> -->

<!-- 二版 -->

<!-- <template>
  <div class="page-wrapper">
    <div class="three-d-container">
      <ThreeDView v-if="activeView === '3d'" />
      <FlatView v-if="activeView === 'flat'" />
    </div>

    <div class="button-container">
      <button :class="{ active: activeView === '3d' }" @click="switchView('3d')">立体</button>
      <button :class="{ active: activeView === 'flat' }" @click="switchView('flat')">平面</button>
    </div>
  </div>
</template>

<script>
import ThreeDView from '@/components/CubeView/ThreeDView.vue'
import FlatView from '@/components/CubeView/FlatView.vue' // 导入新的平面视图组件

export default {
  name: 'Dashboard',
  components: {
    ThreeDView,
    FlatView // 注册平面视图组件
  },
  data() {
    return {
      activeView: '3d' // 初始显示3D视图
    }
  },
  methods: {
    // 切换视图的方法
    switchView(view) {
      this.activeView = view
    }
  }
}
</script>
<style scoped>
.page-wrapper {
  background-color: #ffffff; /* 背景颜色 */
  width: 100%;
  height: 300px; /* 保持父容器高度不变 */
  display: flex;
  flex-direction: column; /* 让按钮和3D容器垂直排列 */
  justify-content: center; /* 水平居中 */
  align-items: center; /* 垂直居中 */
}

.button-container {
  margin-top: 10px; /* 按钮与地图之间的间距 */
  display: flex;
  justify-content: center; /* 水平居中按钮 */
}

button {
  margin: 0 40px;
  padding: 2px 15px;
  border: none;
  background-color: #e0e0e0;
  cursor: pointer;
  transition: background-color 0.3s ease;
}

button:hover {
  background-color: #ccc;
}

button.active {
  background-color: #007bff;
  color: white;
}

.three-d-container {
  display: flex;
  justify-content: center;
  align-items: center;
  width: 100%;
  height: 100%;
  /* 去除任何内外边距，以确保没有额外的空隙 */
  margin: 0;
  padding: 0;
}

.three-d-container > * {
  width: 100%; /* 让子组件的宽度自适应父容器 */
  height: 100%; /* 让子组件的高度自适应父容器 */
  flex-grow: 1; /* 确保子组件填满整个容器 */
  box-sizing: border-box; /* 确保宽度计算正确 */
}
</style> -->
<!-- 3版 -->
<template>
  <div class="page-wrapper">
    <div class="three-d-container">
      <ThreeDView v-if="activeView === '3d'" :show-coordinates="showCoordinates" :points="points" />
      <FlatView v-if="activeView === 'flat'" />
    </div>

    <!-- 按钮放置在地图的正下方 -->
    <div class="button-container">
      <button :class="{ active: activeView === '3d' }" @click="switchView('3d')">立体</button>
      <button :class="{ active: activeView === 'flat' }" @click="switchView('flat')">平面</button>
    </div>
  </div>
</template>

<script>
import ThreeDView from '@/components/CubeView/ThreeDView.vue'
import FlatView from '@/components/CubeView/FlatView.vue' // 导入新的平面视图组件

export default {
  name: 'Dashboard',
  components: {
    ThreeDView,
    FlatView // 注册平面视图组件
  },
  data() {
    return {
      activeView: '3d', // 初始显示3D视图
      showCoordinates: '', // 用于显示鼠标悬停时的坐标和合法性
      points: [ // 定义两个固定点的坐标
        { position: { x: 0.2, y: 0.3, z: -0.3 }, legal: true }, // 点1在正方体内
        { position: { x: 0.6, y: 0.6, z: 0.6 }, legal: false } // 点2在正方体外
      ]
    }
  },
  methods: {
    // 切换视图的方法
    switchView(view) {
      this.activeView = view
    }
  }
}
</script>

<style scoped>
.page-wrapper {
  background-color: #ffffff; /* 背景颜色 */
  width: 100%;
  height: 300px; /* 保持父容器高度不变 */
  display: flex;
  flex-direction: column; /* 让按钮和3D容器垂直排列 */
  justify-content: center; /* 水平居中 */
  align-items: center; /* 垂直居中 */
}

.button-container {
  margin-top: 10px; /* 按钮与地图之间的间距 */
  display: flex;
  justify-content: center; /* 水平居中按钮 */
}

button {
  margin: 0 40px;
  padding: 2px 15px;
  border: none;
  background-color: #e0e0e0;
  cursor: pointer;
  transition: background-color 0.3s ease;
}

button:hover {
  background-color: #ccc;
}

button.active {
  background-color: #007bff;
  color: white;
}

.three-d-container {
  display: flex;
  justify-content: center;
  align-items: center;
  width: 100%;
  height: 100%;
  /* 去除任何内外边距，以确保没有额外的空隙 */
  margin: 0;
  padding: 0;
}

.three-d-container > * {
  width: 100%; /* 让子组件的宽度自适应父容器 */
  height: 100%; /* 让子组件的高度自适应父容器 */
  flex-grow: 1; /* 确保子组件填满整个容器 */
  box-sizing: border-box; /* 确保宽度计算正确 */
}
</style>
