<template>
  <div
    class="flat-map-container"
    @mousemove="showCoordinates"
    @mouseleave="hideCoordinates"
  >
    <!-- 显示非法区域文字 -->
    <div class="illegal-area">非法区域</div>

    <!-- 显示地图图片 -->
    <img ref="map" src="@/assets/map.png" alt="Map" class="map-image">

    <!-- 显示坐标信息 -->
    <div v-if="coordinates" class="coordinates-display">
      X: {{ coordinates.x }}, Y: {{ coordinates.y }}
    </div>
  </div>
</template>

<script>
export default {
  data() {
    return {
      coordinates: null, // 用于保存鼠标的相对坐标
      origin: { x: 0, y: 0 } // 原点（蓝色图标）的位置
    }
  },
  mounted() {
    // 定义原点位置为图片中蓝色图标的位置（手动设置）
    this.origin = { x: 279, y: 110 } // 根据您的实际图标位置调整
  },
  methods: {
    showCoordinates(event) {
      const map = this.$refs.map
      const rect = map.getBoundingClientRect()

      // 计算鼠标相对于图片的坐标
      const mouseX = event.clientX - rect.left
      const mouseY = event.clientY - rect.top

      // 相对于原点（蓝色图标）的坐标
      const relativeX = mouseX - this.origin.x
      const relativeY = mouseY - this.origin.y

      // 更新坐标
      this.coordinates = { x: Math.round(relativeX), y: Math.round(relativeY) }
    },
    hideCoordinates() {
      this.coordinates = null
    }
  }
}
</script>

  <style scoped>
.flat-map-container {
  position: relative;
  display: flex;
  justify-content: center;
  align-items: center;
  flex-direction: column; /* 垂直排列地图和文字 */
  width: 100%;
  height: 100%;
  background-color: #f0f0f0;
}

/* 地图图片 */
.map-image {
  max-width: 100%;
  max-height: 100%;
  object-fit: contain;
}

/* 显示非法区域文字 */
.illegal-area {
  position: relative;
  top: -10px; /* 控制文字显示在图片外 */
  color: red;
  font-size: 20px;
  font-weight: bold;
  margin-bottom: 2px; /* 调整文字与图片的间距 */
}

/* 显示坐标信息 */
.coordinates-display {
  position: absolute;
  bottom: 10px;
  left: 10px;
  background-color: rgba(255, 255, 255, 0.8);
  padding: 5px 10px;
  border-radius: 5px;
  font-size: 14px;
  font-weight: bold;
}
</style>
