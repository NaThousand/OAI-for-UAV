<!-- <template>
  <div ref="threeDView" style="width: 100%; height: 100%"></div>
</template>

<script>
import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls";

export default {
  name: "ThreeDView",
  mounted() {
    this.initThreeDView();
  },
  methods: {
    initThreeDView() {
      // 创建场景
      const scene = new THREE.Scene();
      scene.background = new THREE.Color(0xffffff); // 设置背景为白色

      // 创建相机
      const camera = new THREE.PerspectiveCamera(
        75,
        this.$refs.threeDView.clientWidth / this.$refs.threeDView.clientHeight,
        0.1,
        1000
      );
      camera.position.z = 2; // 调整相机位置以增加立方体占比

      // 创建渲染器
      const renderer = new THREE.WebGLRenderer({ alpha: true });
      renderer.setSize(
        this.$refs.threeDView.clientWidth,
        this.$refs.threeDView.clientHeight
      );
      this.$refs.threeDView.appendChild(renderer.domElement);

      // 创建正方体几何体
      const geometry = new THREE.BoxGeometry(1, 1, 1);

      // 创建淡青色材质
      const material = new THREE.MeshBasicMaterial({
        color: 0xe0ffff, // 淡青色 (light cyan)
        transparent: true,
        opacity: 0.5, // 设置透明度
      });

      // 创建网格并添加到场景中
      const cube = new THREE.Mesh(geometry, material);
      scene.add(cube);

      // 创建线框材质并添加到场景中
      const edges = new THREE.EdgesGeometry(geometry);
      const lineMaterial = new THREE.LineBasicMaterial({ color: 0x0000ff }); // 蓝色线条
      const lineSegments = new THREE.LineSegments(edges, lineMaterial);
      scene.add(lineSegments);

      // 创建三维坐标轴
      this.createAxes(scene);

      // 创建 OrbitControls 控件
      const controls = new OrbitControls(camera, renderer.domElement);
      controls.enableDamping = true; // 启用阻尼效果（惯性）
      controls.dampingFactor = 0.25;
      controls.screenSpacePanning = false;
      controls.maxPolarAngle = Math.PI / 2;

      // 渲染循环
      const animate = () => {
        requestAnimationFrame(animate);

        controls.update(); // 仅更新控制器

        renderer.render(scene, camera);
      };

      animate();

      // 窗口调整时更新渲染器和相机
      window.addEventListener("resize", () => {
        const width = this.$refs.threeDView.clientWidth;
        const height = this.$refs.threeDView.clientHeight;

        camera.aspect = width / height;
        camera.updateProjectionMatrix();

        renderer.setSize(width, height);
      });
    },

    createAxes(scene) {
      const axisLength = 1.0;
      const arrowLength = 0.2;
      const arrowHeadLength = 0.05;
      const arrowHeadWidth = 0.05;

      // X轴：红色
      this.createAxis(
        scene,
        new THREE.Vector3(1, 0, 0),
        0xff0000,
        axisLength,
        arrowLength,
        arrowHeadLength,
        arrowHeadWidth
      );

      // Y轴：绿色
      this.createAxis(
        scene,
        new THREE.Vector3(0, 1, 0),
        0x00ff00,
        axisLength,
        arrowLength,
        arrowHeadLength,
        arrowHeadWidth
      );

      // Z轴：蓝色
      this.createAxis(
        scene,
        new THREE.Vector3(0, 0, 1),
        0x0000ff,
        axisLength,
        arrowLength,
        arrowHeadLength,
        arrowHeadWidth
      );
    },

    createAxis(
      scene,
      direction,
      color,
      axisLength,
      arrowLength,
      arrowHeadLength,
      arrowHeadWidth
    ) {
      // 正半轴箭头
      const arrowHelper = new THREE.ArrowHelper(
        direction,
        new THREE.Vector3(0, 0, 0),
        axisLength,
        color,
        arrowHeadLength,
        arrowHeadWidth
      );
      scene.add(arrowHelper);

      // 负半轴线段
      const lineMaterial = new THREE.LineBasicMaterial({ color: color });
      const lineGeometry = new THREE.BufferGeometry().setFromPoints([
        new THREE.Vector3(0, 0, 0),
        direction.clone().multiplyScalar(-axisLength),
      ]);
      const line = new THREE.Line(lineGeometry, lineMaterial);
      scene.add(line);
    },
  },
};
</script>

<style scoped>
/* 你可以根据需要调整样式 */
</style> -->

<!-- 2版 -->

<!-- <template>
  <div class="three-d-view-container">
    <div class="title">接入终端位置</div>
    <div class="axis-legend">
      <div class="legend-item">
        <span class="axis-color x-axis" /> X 轴: 前后方向
      </div>
      <div class="legend-item">
        <span class="axis-color y-axis" /> Y 轴: 上下方向
      </div>
      <div class="legend-item">
        <span class="axis-color z-axis" /> Z 轴: 左右方向
      </div>
    </div>
    <div ref="threeDView" class="three-d-view" />
  </div>
</template>

<script>
import * as THREE from 'three'
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls'

export default {
  name: 'ThreeDView',
  mounted() {
    this.initThreeDView()
  },
  methods: {
    initThreeDView() {
      // 创建场景
      const scene = new THREE.Scene()
      scene.background = new THREE.Color(0xffffff) // 设置背景为白色

      // 创建相机
      const camera = new THREE.PerspectiveCamera(
        75,
        this.$refs.threeDView.clientWidth / this.$refs.threeDView.clientHeight,
        0.1,
        1000
      )
      camera.position.set(0, 0.5, 2) // 设置相机位置
      camera.lookAt(new THREE.Vector3(0, 0.5, 0)) // 使相机聚焦在立方体中心
      //  camera.position.z = 2 // 调整相机位置以增加立方体占比

      // 创建渲染器
      const renderer = new THREE.WebGLRenderer({ alpha: true })
      renderer.setSize(
        this.$refs.threeDView.clientWidth,
        this.$refs.threeDView.clientHeight
      )
      this.$refs.threeDView.appendChild(renderer.domElement)

      // 创建正方体几何体
      const geometry = new THREE.BoxGeometry(1, 1, 1)

      // 创建淡青色材质
      const material = new THREE.MeshBasicMaterial({
        color: 0xe0ffff, // 淡青色 (light cyan)
        transparent: true,
        opacity: 0.5 // 设置透明度
      })

      // 创建网格并添加到场景中
      const cube = new THREE.Mesh(geometry, material)
      scene.add(cube)

      // 创建线框材质并添加到场景中
      const edges = new THREE.EdgesGeometry(geometry)
      const lineMaterial = new THREE.LineBasicMaterial({ color: 0x0000ff }) // 蓝色线条
      const lineSegments = new THREE.LineSegments(edges, lineMaterial)
      lineSegments.position.y = 0.5 // 与立方体位置同步
      scene.add(lineSegments)

      // 创建三维坐标轴
      this.createAxes(scene)

      // 创建 OrbitControls 控件
      const controls = new OrbitControls(camera, renderer.domElement)
      controls.enableDamping = true // 启用阻尼效果（惯性）
      controls.dampingFactor = 0.25
      controls.screenSpacePanning = false
      controls.maxPolarAngle = Math.PI / 2
      controls.target.set(0, 0.5, 0) // 将控制目标设置为立方体中心
      controls.update() // 更新控制器，使设置生效
      cube.position.y = 0.5 // 调整立方体的位置，使中心位于底部平面

      // 渲染循环
      const animate = () => {
        requestAnimationFrame(animate)

        controls.update() // 仅更新控制器

        renderer.render(scene, camera)
      }

      animate()

      // 窗口调整时更新渲染器和相机
      window.addEventListener('resize', () => {
        const width = this.$refs.threeDView.clientWidth
        const height = this.$refs.threeDView.clientHeight

        camera.aspect = width / height
        camera.updateProjectionMatrix()

        renderer.setSize(width, height)
      })// ... Three.js 代码与之前相同
    },
    createAxes(scene) {
      const axisLength = 1.0
      const arrowLength = 0.2
      const arrowHeadLength = 0.05
      const arrowHeadWidth = 0.05

      // X轴：红色
      this.createAxis(
        scene,
        new THREE.Vector3(1, 0, 0),
        0xff0000,
        axisLength,
        arrowLength,
        arrowHeadLength,
        arrowHeadWidth
      )

      // Y轴：绿色
      this.createAxis(
        scene,
        new THREE.Vector3(0, 1, 0),
        0x00ff00,
        axisLength + 0.3,
        arrowLength,
        arrowHeadLength,
        arrowHeadWidth
      )

      // Z轴：蓝色
      this.createAxis(
        scene,
        new THREE.Vector3(0, 0, 1),
        0x0000ff,
        axisLength,
        arrowLength,
        arrowHeadLength,
        arrowHeadWidth
      )// ... 自定义坐标轴代码与之前相同
    },
    createAxis(
      scene,
      direction,
      color,
      axisLength,
      arrowLength,
      arrowHeadLength,
      arrowHeadWidth
    ) {
      // ... 自定义坐标轴代码与之前相同
      // 正半轴箭头
      const arrowHelper = new THREE.ArrowHelper(
        direction,
        new THREE.Vector3(0, 0, 0),
        axisLength,
        color,
        arrowHeadLength,
        arrowHeadWidth
      )
      scene.add(arrowHelper)

      // 负半轴线段
      if (direction.y === 0) {
        const lineMaterial = new THREE.LineBasicMaterial({ color: color })
        const lineGeometry = new THREE.BufferGeometry().setFromPoints([
          new THREE.Vector3(0, 0, 0),
          direction.clone().multiplyScalar(-axisLength)// .add(new THREE.Vector3(0, -0.5, 0))
          // direction.clone().multiplyScalar(-axisLength)
        ])
        const line = new THREE.Line(lineGeometry, lineMaterial)
        scene.add(line)
      }
    }
  }
}
</script>

<style scoped>
.three-d-view-container {
  position: relative;
  width: 100%;
  height: 100%;
}

.title {
  position: absolute;
  top: 10px;
  left: 50%;
  transform: translateX(-50%);
  font-size: 20px;
  font-weight: bold;
  z-index: 10;
}

.axis-legend {
  position: absolute;
  top: 15px;
  right: 0px;
  display: flex;
  flex-direction: column;
  gap: 5px;
  font-size: 10px;
  z-index: 10;
}

.title, .axis-legend {
  background-color: rgba(255, 255, 255, 0.8); /* 半透明背景颜色 */
  padding: 5px;
  border-radius: 5px;
}

.legend-item {
  display: flex;
  align-items: center;
}

.axis-color {
  display: inline-block;
  width: 12px;
  height: 12px;
  margin-right: 5px;
}

.x-axis {
  background-color: #ff0000; /* X 轴的红色 */
}

.y-axis {
  background-color: #00ff00; /* Y 轴的绿色 */
}

.z-axis {
  background-color: #0000ff; /* Z 轴的蓝色 */
}

.three-d-view {
  width: 100%;
  height: 100%;
  position: absolute;
  top: 0;
  left: 0;
  z-index: 1;
}
</style> -->

<!-- 3版 -->

<template>
  <div class="three-d-view-container">
    <div class="title">接入终端位置</div>
    <div class="axis-legend">
      <div class="legend-item">
        <span class="axis-color x-axis" /> X 轴: 前后方向
      </div>
      <div class="legend-item">
        <span class="axis-color y-axis" /> Y 轴: 上下方向
      </div>
      <div class="legend-item">
        <span class="axis-color z-axis" /> Z 轴: 左右方向
      </div>
    </div>
    <div ref="threeDView" class="three-d-view" />
    <!-- 坐标和合法性展示 -->
    <div v-if="hoveredPoint" class="info-box">
      X:{{ hoveredPoint.x.toFixed(2) }}, Y:{{ hoveredPoint.y.toFixed(2) }}, Z:{{ hoveredPoint.z.toFixed(2) }} - {{ legalityStatus }}
    </div>
  </div>
</template>

<script>
import * as THREE from 'three'
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls'

export default {
  name: 'ThreeDView',
  props: {
    // eslint-disable-next-line vue/require-default-prop
    points: Array // 从父组件接收的点的坐标
  },
  data() {
    return {
      scene: null,
      camera: null,
      renderer: null,
      raycaster: new THREE.Raycaster(),
      mouse: new THREE.Vector2(),
      spheres: [], // 保存生成的点
      hoveredPoint: null, // 当前鼠标悬停的点
      legalityStatus: '', // 合法性状态
      time: 0 // 用于闪烁效果的计时器
    }
  },
  mounted() {
    this.initThreeDView()
    window.addEventListener('mousemove', this.onMouseMove, false) // 监听鼠标移动事件
  },
  beforeDestroy() {
    window.removeEventListener('mousemove', this.onMouseMove, false) // 移除鼠标事件监听器
  },
  methods: {
    initThreeDView() {
      // 创建场景
      this.scene = new THREE.Scene()
      this.scene.background = new THREE.Color(0xffffff) // 设置背景为白色

      // 创建相机
      this.camera = new THREE.PerspectiveCamera(
        75,
        this.$refs.threeDView.clientWidth / this.$refs.threeDView.clientHeight,
        0.1,
        1000
      )
      this.camera.position.set(0, 1, 2) // 设置相机位置
      this.camera.lookAt(new THREE.Vector3(0, 0.5, 0)) // 使相机聚焦在立方体底面中心

      // 创建渲染器
      this.renderer = new THREE.WebGLRenderer({ alpha: true })
      this.renderer.setSize(
        this.$refs.threeDView.clientWidth,
        this.$refs.threeDView.clientHeight
      )
      this.$refs.threeDView.appendChild(this.renderer.domElement)

      // 创建立方体：原点在底面中心
      const geometry = new THREE.BoxGeometry(1, 1, 1)
      const material = new THREE.MeshBasicMaterial({
        color: 0xe0ffff, // 淡青色 (light cyan)
        transparent: true,
        opacity: 0.5 // 设置透明度
      })
      const cube = new THREE.Mesh(geometry, material)
      cube.position.y = 0.5 // 将立方体的底面放置在 y = 0 平面上
      this.scene.add(cube)

      // 创建线框材质并添加到场景中
      const edges = new THREE.EdgesGeometry(geometry)
      const lineMaterial = new THREE.LineBasicMaterial({ color: 0x0000ff }) // 蓝色线条
      const lineSegments = new THREE.LineSegments(edges, lineMaterial)
      lineSegments.position.y = 0.5 // 与立方体位置同步
      this.scene.add(lineSegments)

      // 创建三维坐标轴
      this.createAxes(this.scene)

      // 添加发光小点
      this.points.forEach(point => {
        // 创建闪烁的发光小点
        // const sphereGeometry = new THREE.SphereGeometry(0.05, 32, 32) // 点的大小
        const sphereGeometry = new THREE.OctahedronGeometry(0.05) // 八面体，大小为0.1
        const sphereMaterial = new THREE.MeshPhongMaterial({
          color: point.legal ? 0x00ff00 : 0xff0000, // 合法的点为绿色，非法为红色
          emissive: point.legal ? 0x00ff00 : 0xff0000, // 设置发光颜色
          emissiveIntensity: 1.5, // 设置发光强度
          shininess: 50 // 增加表面亮度
        })
        const sphere = new THREE.Mesh(sphereGeometry, sphereMaterial)
        sphere.position.set(point.position.x, point.position.y, point.position.z)
        this.spheres.push(sphere)
        this.scene.add(sphere)
      })

      // 创建 OrbitControls 控件
      const controls = new OrbitControls(this.camera, this.renderer.domElement)
      controls.enableDamping = true // 启用阻尼效果（惯性）
      controls.dampingFactor = 0.25
      controls.screenSpacePanning = false
      controls.maxPolarAngle = Math.PI / 2
      controls.target.set(0, 0.5, 0) // 将控制目标设置为立方体中心
      controls.update() // 更新控制器

      // 渲染循环
      const animate = () => {
        requestAnimationFrame(animate)
        controls.update() // 更新控制器
        this.animateGlowEffect() // 调用发光动画
        this.renderer.render(this.scene, this.camera)
      }

      animate()

      // 窗口调整时更新渲染器和相机
      window.addEventListener('resize', () => {
        const width = this.$refs.threeDView.clientWidth
        const height = this.$refs.threeDView.clientHeight
        this.camera.aspect = width / height
        this.camera.updateProjectionMatrix()
        this.renderer.setSize(width, height)
      })
    },

    // 动态更新闪烁效果
    animateGlowEffect() {
      this.time += 0.02 // 增加时间，减慢速度

      // 动态调整球体发光强度，减少闪烁幅度
      this.spheres.forEach(sphere => {
        const scale = 1 + Math.sin(this.time) * 0.1 // 通过正弦波动态调整大小，减少幅度
        sphere.scale.set(scale, scale, scale)
      })
    },

    onMouseMove(event) {
      // 获取容器的尺寸
      const rect = this.$refs.threeDView.getBoundingClientRect()

      // 计算鼠标位置的归一化设备坐标（NDC）
      this.mouse.x = ((event.clientX - rect.left) / rect.width) * 2 - 1
      this.mouse.y = -((event.clientY - rect.top) / rect.height) * 2 + 1

      // 更新射线
      this.raycaster.setFromCamera(this.mouse, this.camera)

      // 计算物体和射线的交集
      const intersects = this.raycaster.intersectObjects(this.spheres)

      if (intersects.length > 0) {
        const intersectedObject = intersects[0].object
        const position = intersectedObject.position
        this.hoveredPoint = position // 记录悬停点的坐标
        this.legalityStatus = this.checkIfPointInCube(position) // 判断合法性
      } else {
        this.hoveredPoint = null // 没有悬停点时清除状态
        this.legalityStatus = ''
      }
    },

    checkIfPointInCube(position) {
      // 判断是否在立方体内
      const isInCube = (position.x >= -0.5 && position.x <= 0.5) &&
                       (position.y >= 0 && position.y <= 1) && // y 范围是 [0, 1]
                       (position.z >= -0.5 && position.z <= 0.5)
      return isInCube ? '合法' : '非法'
    },

    createAxes(scene) {
      const axisLength = 1.0
      const arrowLength = 0.2
      const arrowHeadLength = 0.05
      const arrowHeadWidth = 0.05

      // X轴：红色
      this.createAxis(
        scene,
        new THREE.Vector3(1, 0, 0),
        0xff0000,
        axisLength,
        arrowLength,
        arrowHeadLength,
        arrowHeadWidth
      )

      // Y轴：绿色
      this.createAxis(
        scene,
        new THREE.Vector3(0, 1, 0),
        0x00ff00,
        axisLength + 0.3,
        arrowLength,
        arrowHeadLength,
        arrowHeadWidth
      )

      // Z轴：蓝色
      this.createAxis(
        scene,
        new THREE.Vector3(0, 0, 1),
        0x0000ff,
        axisLength,
        arrowLength,
        arrowHeadLength,
        arrowHeadWidth
      )
    },

    createAxis(
      scene,
      direction,
      color,
      axisLength,
      arrowLength,
      arrowHeadLength,
      arrowHeadWidth
    ) {
      // 正半轴箭头
      const arrowHelper = new THREE.ArrowHelper(
        direction,
        new THREE.Vector3(0, 0, 0),
        axisLength,
        color,
        arrowHeadLength,
        arrowHeadWidth
      )
      scene.add(arrowHelper)

      // 负半轴线段
      if (direction.y === 0) {
        const lineMaterial = new THREE.LineBasicMaterial({ color: color })
        const lineGeometry = new THREE.BufferGeometry().setFromPoints([
          new THREE.Vector3(0, 0, 0),
          direction.clone().multiplyScalar(-axisLength)
        ])
        const line = new THREE.Line(lineGeometry, lineMaterial)
        scene.add(line)
      }
    }
  }
}
</script>

<style scoped>
.three-d-view-container {
  position: relative;
  width: 100%;
  height: 100%;
}

.title {
  position: absolute;
  top: 10px;
  left: 50%;
  transform: translateX(-50%);
  font-size: 20px;
  font-weight: bold;
  z-index: 10;
}

.axis-legend {
  position: absolute;
  top: 15px;
  right: 0px;
  display: flex;
  flex-direction: column;
  gap: 5px;
  font-size: 10px;
  z-index: 10;
}

.title,
.axis-legend {
  background-color: rgba(255, 255, 255, 0.8); /* 半透明背景颜色 */
  padding: 5px;
  border-radius: 5px;
}

.legend-item {
  display: flex;
  align-items: center;
}

.axis-color {
  display: inline-block;
  width: 12px;
  height: 12px;
  margin-right: 5px;
}

.x-axis {
  background-color: #ff0000; /* X 轴的红色 */
}

.y-axis {
  background-color: #00ff00; /* Y 轴的绿色 */
}

.z-axis {
  background-color: #0000ff; /* Z 轴的蓝色 */
}

.three-d-view {
  width: 100%;
  height: 100%;
  position: absolute;
  top: 0;
  left: 0;
  z-index: 1;
}

.info-box {
  position: absolute;
  bottom: 10px;
  left: 10px;
  /* transform: translateX(-50%); */
  background-color: rgba(255, 255, 255, 0.8);
  padding: 5px 10px;
  border-radius: 5px;
  font-size: 14px;
  z-index: 10;
  /* box-shadow: 0px 0px 5px rgba(0, 0, 0, 0.2); */
  font-weight: 600;
}

</style>
