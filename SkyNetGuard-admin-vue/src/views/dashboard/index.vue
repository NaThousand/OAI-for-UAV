<!-- <template>
  <div class="page-wrapper">
    <div>
      <ThreeDView style="width: 500px; height: 500px" />
    </div>
  </div>
</template>

  <script>
import ThreeDView from "@/components/CubeView/ThreeDView.vue";

export default {
  name: "Dashboard",
  components: {
    ThreeDView,
  },
};
</script>

  <style scoped>
.page-wrapper {
  background-color: #f0f0f0; /* 浅灰色背景 */
  width: 100%;
  height: 100vh; /* 确保背景覆盖整个视口 */
}
</style>  -->

<template>
  <div class="dashboard-container">
    <component :is="currentRole" />
  </div>
</template>

<script>
import { mapGetters } from 'vuex'
import adminDashboard from './admin'
import editorDashboard from './editor'

export default {
  name: 'Dashboard',
  components: { adminDashboard, editorDashboard },
  data() {
    return {
      currentRole: 'adminDashboard'
    }
  },
  computed: {
    ...mapGetters([
      'roles'
    ])
  },
  created() {
    if (!this.roles.includes('admin')) {
      this.currentRole = 'editorDashboard'
    }
  }
}
</script>
