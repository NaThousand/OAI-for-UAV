// const Mock = require('mockjs')

// const data = Mock.mock({
//   'items|30': [{
//     id: '@id',
//     title: '@sentence(10, 20)',
//     'status|1': ['接入', '未响应', '拒绝'],
//     'author|1': ['是', '否'],
//     display_time: '@datetime("yyyy-MM-dd HH:mm:ss")',
//     pageviews: '@integer(300, 5000)'
//   }]
// })

// module.exports = [
//   {
//     url: '/SkyNetGuard-admin-vue/table/list',
//     type: 'get',
//     response: config => {
//       const items = data.items
//       return {
//         code: 20000,
//         data: {
//           total: items.length,
//           items: items
//         }
//       }
//     }
//   }
// ]

const Mock = require('mockjs')

// 获取当前时间
const now = new Date()

// 获取一年前的时间
const lastYear = new Date()
lastYear.setFullYear(now.getFullYear() - 1)

// 自定义随机日期函数，生成过去一年内的随机日期
function randomDate(start, end) {
  return new Date(start.getTime() + Math.random() * (end.getTime() - start.getTime()))
}

// 自定义随机时间范围函数，生成30分钟到6小时内的随机时间，返回单位为分钟的随机数
function randomMinutes(min, max) {
  return Math.floor(Math.random() * (max - min + 1)) + min
}

const data = Mock.mock({
  'items|30': [{
    id: '@id',
    title: '@sentence(10, 20)',
    'status|1': ['接入', '拒绝'], // 随机生成状态
    author: '@ip',
    display_time: function() {
      return randomDate(lastYear, now).toLocaleString('zh-CN', { hour12: false }) // 生成过去一年内的随机日期并格式化
    },
    pageviews: function() {
      if (this.status === '接入') {
        // 如果 status 是 "接入"，则生成 30分钟到6小时的随机时间
        const minutes = randomMinutes(30, 360) // 生成30分钟到6小时内的随机时间，单位为分钟
        const hours = Math.floor(minutes / 60) // 转换为小时
        const remainingMinutes = minutes % 60 // 剩余的分钟数
        return `${hours}小时 ${remainingMinutes}分钟` // 返回格式化时间字符串
      } else {
        // 如果 status 不是 "接入"，则返回 "——"
        return '——'
      }
    }
  }]
})

module.exports = [
  {
    url: '/SkyNetGuard-admin-vue/table/list',
    type: 'get',
    response: config => {
      const items = data.items
      return {
        code: 20000,
        data: {
          total: items.length,
          items: items
        }
      }
    }
  }
]
