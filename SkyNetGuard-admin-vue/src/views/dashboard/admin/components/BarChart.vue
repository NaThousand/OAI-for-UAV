<template>
  <div :class="className" :style="{height:height,width:width}" />
</template>

<script>
import * as echarts from 'echarts'
require('echarts/theme/macarons') // echarts theme
import resize from './mixins/resize'

const animationDuration = 6000

export default {
  mixins: [resize],
  props: {
    className: {
      type: String,
      default: 'chart'
    },
    width: {
      type: String,
      default: '100%'
    },
    height: {
      type: String,
      default: '300px'
    }
  },
  data() {
    return {
      chart: null
    }
  },
  mounted() {
    this.$nextTick(() => {
      this.initChart()
    })
  },
  beforeDestroy() {
    if (!this.chart) {
      return
    }
    this.chart.dispose()
    this.chart = null
  },
  methods: {
    initChart() {
      this.chart = echarts.init(this.$el, 'macarons')

      this.chart.setOption({
        tooltip: {
          trigger: 'axis',
          axisPointer: { // 坐标轴指示器，坐标轴触发有效
            type: 'shadow' // 默认为直线，可选为：'line' | 'shadow'
          }
        },
        grid: {
          top: 10,
          left: '6%',
          right: '2%',
          bottom: '8%',
          containLabel: true
        },
        xAxis: [{
          type: 'category',
          data: ['节点1', '节点2', '节点3', '节点4', '节点5', '节点6'],
          axisTick: {
            alignWithLabel: true
          },
          name: '检测节点',
          nameLocation: 'middle',
          nameGap: 25,
          nameTextStyle: {
            fontSize: 11 // 设置标题的字体大小
          }
        }],
        yAxis: [{
          type: 'value',
          axisTick: {
            show: false
          },
          name: '传输速率 (MB/S)', // 添加纵坐标标题和单位
          nameLocation: 'middle', // 标题位置，放在纵坐标顶部
          nameGap: 35,
          nameTextStyle: {
            fontSize: 11 // 设置标题的字体大小
            // color: '#add8e6'
          }
        }],
        series: [{
          name: '最高速率',
          type: 'bar',
          barWidth: '50%',
          barGap: '-40%',
          data: [79, 89, 200, 334, 390, 330],
          animationDuration,
          itemStyle: {
            opacity: 0.6 // 底层稍微透明
          }
        },
        // {
        //   name: 'pageB',
        //   type: 'bar',
        //   barWidth: '60%',
        //   data: [80, 52, 200, 334, 390, 330],
        //   animationDuration,
        //   itemStyle: {
        //     opacity: 0.8 // 中间层稍微不透明
        //   }
        // },
        {
          name: '平均速率',
          type: 'bar',
          barWidth: '50%',
          barGap: '-40%',
          data: [30, 52, 134, 154, 190, 230],
          animationDuration,
          itemStyle: {
            opacity: 1 // 最外层不透明
          }
        }]
      })
    }
  }
}
</script>
