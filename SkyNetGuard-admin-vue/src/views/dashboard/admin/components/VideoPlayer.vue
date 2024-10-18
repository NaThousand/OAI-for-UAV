<!-- <template>
  <div class="video-player">
    <video
      v-if="videoSrc"
      :src="videoSrc"
      controls
      style="width: 100%; height: auto;"
    >
      Your browser does not support the video tag.
    </video>
    <p v-else>Loading video...</p>
  </div>
</template>

<script>
export default {
  name: 'VideoPlayer',
  data() {
    return {
      videoSrc: 'https://www.bilibili.com/video/BV1He1BYPE6W/?spm_id_from=333.1007.tianma.1-3-3.click&vd_source=14d1c0539bf5f20f3e0486b5da735905' // 视频源链接
    }
  },
  created() {
    this.fetchVideo()
  },
  methods: {
    async fetchVideo() {
      try {
        // 假设你的后端 API 为 /api/video，返回一个视频 URL
        const response = await this.$axios.get('/api/video')
        this.videoSrc = response.data.videoUrl
      } catch (error) {
        console.error('Error fetching video:', error)
      }
    }
  }
}
</script>

<style scoped>
.video-player {
  margin: 20px;
}
</style> -->
<template>
  <div class="bilibili-player">
    <!-- eslint-disable-next-line vue/html-self-closing -->
    <iframe
      :src="videoSrc"
      scrolling="no"
      border="0"
      frameborder="no"
      framespacing="0"
      allowfullscreen="true"
      class="responsive-iframe"
    />
  </div>
</template>

<script>
export default {
  name: 'BilibiliPlayer',
  data() {
    return {
      videoSrc:
        '//player.bilibili.com/player.html?isOutside=true&aid=548997825&bvid=BV1Nq4y137Sm&cid=436251126&p=1' // Bilibili视频链接
    }
  }
}
</script>

<style scoped>
.bilibili-player {
  /* 增加容器高度 */
  height: 300px; /* 你可以根据需要修改高度 */
  display: flex;
  justify-content: center; /* 水平居中 */
  align-items: center; /* 垂直居中 */
  margin-bottom: 20px; /* 只设置下边距 */
  background-color: #ffffff; /* 可选，给容器增加背景颜色 */
}

.responsive-iframe {
  width: 100%;
  max-width: 800px; /* 最大宽度，保持视频宽度不超过此值 */
  height: 280px; /* 保持16:9的高度（如800px宽度时，450px为高度） */
  border: 0;
  margin-bottom: 15px; /* 只设置下边距 */
}
</style>
