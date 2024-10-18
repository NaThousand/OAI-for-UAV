import request from '@/utils/request'

export function getList(params) {
  return request({
    url: '/SkyNetGuard-admin-vue/table/list',
    method: 'get',
    params
  })
}
