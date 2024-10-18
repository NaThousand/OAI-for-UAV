import request from '@/utils/request'

export function login(data) {
  return request({
    url: '/SkyNetGuard-admin-vue/user/login',
    method: 'post',
    data
  })
}

export function getInfo(token) {
  return request({
    url: '/SkyNetGuard-admin-vue/user/info',
    method: 'get',
    params: { token }
  })
}

export function logout() {
  return request({
    url: '/SkyNetGuard-admin-vue/user/logout',
    method: 'post'
  })
}
