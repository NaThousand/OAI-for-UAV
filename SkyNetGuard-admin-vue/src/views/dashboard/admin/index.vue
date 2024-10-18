<template>
  <div class="dashboard-editor-container">
    <github-corner class="github-corner" />

    <panel-group @handleSetLineChartData="handleSetLineChartData" />

    <el-row style="background:#fff;padding:16px 16px 0;margin-bottom:32px;">
      <line-chart :chart-data="lineChartData" />
    </el-row>

    <el-row :gutter="32">
      <el-col :xs="24" :sm="24" :lg="8">
        <div class="chart-wrapper">
          <relief-map />
        </div>
      </el-col>
      <el-col :xs="24" :sm="24" :lg="8">
        <div class="chart-wrapper">
          <video-player />
        </div>
      </el-col>
      <el-col :xs="24" :sm="24" :lg="8">
        <div class="chart-wrapper">
          <bar-chart />
        </div>
      </el-col>
    </el-row>

    <el-row :gutter="8">
      <el-col :xs="{span: 24}" :sm="{span: 24}" :md="{span: 24}" :lg="{span: 12}" :xl="{span: 12}" style="padding-right:8px;margin-bottom:30px;">
        <transaction-table />
      </el-col>
      <el-col :xs="{span: 24}" :sm="{span: 12}" :md="{span: 12}" :lg="{span: 6}" :xl="{span: 6}" style="margin-bottom:30px;">
        <todo-list />
      </el-col>
      <el-col :xs="{span: 24}" :sm="{span: 12}" :md="{span: 12}" :lg="{span: 6}" :xl="{span: 6}" style="margin-bottom:30px;">
        <box-card />
      </el-col>
    </el-row>
  </div>
</template>

<script>
import GithubCorner from '@/components/GithubCorner'
import PanelGroup from './components/PanelGroup'
import LineChart from './components/LineChart'
// import PanelGroup from './components/PanelGroup'
import VideoPlayer from './components/VideoPlayer'
import BarChart from './components/BarChart'
import TransactionTable from './components/TransactionTable'
import TodoList from './components/TodoList'
import BoxCard from './components/BoxCard'
import ReliefMap from './components/ReliefMap.vue'

const lineChartData = {
  newVisitis: {
    xAxisData: ['0h', '30min', '1h', '1h30min', '2h', '2h30min', 'h'], // 新增横坐标数据
    expectedData: [0, 2, 1, 1, 2, 3, 1],
    actualData: [1, 2, 0, 4, 1, 0, 2]
  },
  messages: {
    xAxisData: ['0h', '4h', '8h', '12h', '16h', '20h', '24h'], // 新增横坐标数据
    expectedData: [4, 3, 2, 4, 6, 13, 10],
    actualData: [7, 3, 4, 14, 7, 4, 10]
  },
  purchases: {
    xAxisData: ['周一', '周二', '周三', '周四', '周五', '周六', '周日'], // 新增横坐标数据
    expectedData: [8, 10, 21, 12, 11, 30, 42],
    actualData: [12, 9, 10, 38, 22, 13, 49]
  },
  shoppings: {
    xAxisData: ['1~5日', '6~10日', '11~15日', '16~20日', '21~25日', '25~30日'], // 新增横坐标数据
    expectedData: [130, 140, 141, 142, 145, 116],
    actualData: [120, 82, 91, 154, 162, 132]
  }
}

export default {
  name: 'DashboardAdmin',
  components: {
    GithubCorner,
    PanelGroup,
    LineChart,
    ReliefMap,
    VideoPlayer,
    BarChart,
    TransactionTable,
    TodoList,
    // PanelGroup,
    BoxCard
  },
  data() {
    return {
      lineChartData: lineChartData.newVisitis
    }
  },
  methods: {
    handleSetLineChartData(type) {
      this.lineChartData = lineChartData[type]
    }
  }
}
</script>

<style lang="scss" scoped>
.dashboard-editor-container {
  padding: 32px;
  background-color: rgb(240, 242, 245);
  position: relative;

  .github-corner {
    position: absolute;
    top: 0px;
    border: 0;
    right: 0;
  }

  .chart-wrapper {
    background: #fff;
    padding: 16px 16px 0;
    margin-bottom: 32px;

  }
}

@media (max-width:1024px) {
  .chart-wrapper {
    padding: 8px;
  }
}
</style>

