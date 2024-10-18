import Vue from 'vue'
import Router from 'vue-router'

Vue.use(Router)

/* Layout */
import Layout from '@/layout'
// import ReliefMap from '@/views/dashboard/admin/components/ReliefMap.vue'
// import BarChart from '@/views/dashboard/admin/components/BarChart.vue'
// import BarCard from '@/views/dashboard/admin/components/BoxCard.vue'
// import LineChart from '@/views/dashboard/admin/components/LineChart.vue'
// import PanelGroup from '@/views/dashboard/admin/components/PanelGroup.vue'
// import TransactionTable from '@/views/dashboard/admin/components/TransactionTable.vue'
// import TodoList from '@/views/dashboard/admin/components/TodoList'
// import VideoPlayer from '@/views/dashboard/admin/components/VideoPlayer'

/**
 * Note: sub-menu only appear when route children.length >= 1
 * Detail see: https://panjiachen.github.io/vue-element-admin-site/guide/essentials/router-and-nav.html
 *
 * hidden: true                   if set true, item will not show in the sidebar(default is false)
 * alwaysShow: true               if set true, will always show the root menu
 *                                if not set alwaysShow, when item has more than one children route,
 *                                it will becomes nested mode, otherwise not show the root menu
 * redirect: noRedirect           if set noRedirect will no redirect in the breadcrumb
 * name:'router-name'             the name is used by <keep-alive> (must set!!!)
 * meta : {
    roles: ['admin','editor']    control the page roles (you can set multiple roles)
    title: 'title'               the name show in sidebar and breadcrumb (recommend set)
    icon: 'svg-name'/'el-icon-x' the icon show in the sidebar
    breadcrumb: false            if set false, the item will hidden in breadcrumb(default is true)
    activeMenu: '/example/list'  if set path, the sidebar will highlight the path you set
  }
 */

/**
 * constantRoutes
 * a base page that does not have permission requirements
 * all roles can be accessed
 */
export const constantRoutes = [
  {
    path: '/login',
    component: () => import('@/views/login/index'),
    hidden: true
  },

  {
    path: '/404',
    component: () => import('@/views/error-page/404'),
    hidden: true
  },
  {
    path: '/401',
    component: () => import('@/views/error-page/401'),
    hidden: true
  },

  {
    path: '/',
    component: Layout,
    redirect: '/dashboard',
    children: [{
      path: 'dashboard',
      name: 'Dashboard',
      component: () => import('@/views/dashboard/index'),
      meta: { title: 'Dashboard', icon: 'dashboard' }
    }]
  },

  {
    path: '/History',
    component: Layout,
    redirect: '/history',
    children: [{
      path: 'history',
      name: 'History',
      component: () => import('@/views/history/index'),
      meta: { title: 'History', icon: 'history' }
    }]
  },

  // {
  //   path: '/relief-map',
  //   component: Layout,
  //   children: [
  //     {
  //       path: 'index',
  //       name: 'ReliefMap',
  //       component: ReliefMap,
  //       meta: { title: 'Relief Map', icon: 'map' }
  //     }
  //   ]
  // },

  // {
  //   path: '/BarChart',
  //   component: Layout,
  //   children: [
  //     {
  //       path: 'index',
  //       name: 'BarChart',
  //       component: BarChart,
  //       meta: { title: 'BarChart', icon: 'BarChart' }
  //     }
  //   ]
  // },

  // {
  //   path: '/BarCard',
  //   component: Layout,
  //   children: [
  //     {
  //       path: 'index',
  //       name: 'BarCard',
  //       component: BarCard,
  //       meta: { title: 'BarCard', icon: 'BarCard' }
  //     }
  //   ]
  // },

  // {
  //   path: '/LineChart',
  //   component: Layout,
  //   children: [
  //     {
  //       path: 'index',
  //       name: 'LineChart',
  //       component: LineChart,
  //       meta: { title: 'LineChart', icon: 'LineChart' }
  //     }
  //   ]
  // },

  // {
  //   path: '/PanelGroup',
  //   component: Layout,
  //   children: [
  //     {
  //       path: 'index',
  //       name: 'PanelGroup',
  //       component: PanelGroup,
  //       meta: { title: 'PanelGroup', icon: 'PanelGroup' }
  //     }
  //   ]
  // },

  // {
  //   path: '/TransactionTable',
  //   component: Layout,
  //   children: [
  //     {
  //       path: 'index',
  //       name: 'TransactionTable',
  //       component: TransactionTable,
  //       meta: { title: 'TransactionTable', icon: 'TransactionTable' }
  //     }
  //   ]
  // },

  // {
  //   path: '/TodoList',
  //   component: Layout,
  //   children: [
  //     {
  //       path: 'index',
  //       name: 'TodoList',
  //       component: TodoList,
  //       meta: { title: 'TodoList', icon: 'TodoList' }
  //     }
  //   ]
  // },

  // {
  //   path: '/VideoPlayer',
  //   component: Layout,
  //   children: [
  //     {
  //       path: 'index',
  //       name: 'VideoPlayer',
  //       component: VideoPlayer,
  //       meta: { title: 'VideoPlayer', icon: 'VideoPlayer' }
  //     }
  //   ]
  // },

  {
    path: '/error',
    component: Layout,
    redirect: 'noRedirect',
    name: 'ErrorPages',
    meta: {
      title: 'Error Pages',
      icon: '404'
    },
    children: [
      {
        path: '401',
        component: () => import('@/views/error-page/401'),
        name: 'Page401',
        meta: { title: '401', noCache: true }
      },
      {
        path: '404',
        component: () => import('@/views/error-page/404'),
        name: 'Page404',
        meta: { title: '404', noCache: true }
      }
    ]
  },

  {
    path: '/example',
    component: Layout,
    redirect: '/example/table',
    name: 'Example',
    meta: { title: 'Example', icon: 'el-icon-s-help' },
    children: [
      {
        path: 'table',
        name: 'Table',
        component: () => import('@/views/table/index'),
        meta: { title: 'Table', icon: 'table' }
      },
      {
        path: 'tree',
        name: 'Tree',
        component: () => import('@/views/tree/index'),
        meta: { title: 'Tree', icon: 'tree' }
      }
    ]
  },

  {
    path: '/form',
    component: Layout,
    children: [
      {
        path: 'index',
        name: 'Form',
        component: () => import('@/views/form/index'),
        meta: { title: 'Form', icon: 'form' }
      }
    ]
  },

  {
    path: '/nested',
    component: Layout,
    redirect: '/nested/menu1',
    name: 'Nested',
    meta: {
      title: 'Nested',
      icon: 'nested'
    },
    children: [
      {
        path: 'menu1',
        component: () => import('@/views/nested/menu1/index'), // Parent router-view
        name: 'Menu1',
        meta: { title: 'Menu1' },
        children: [
          {
            path: 'menu1-1',
            component: () => import('@/views/nested/menu1/menu1-1'),
            name: 'Menu1-1',
            meta: { title: 'Menu1-1' }
          },
          {
            path: 'menu1-2',
            component: () => import('@/views/nested/menu1/menu1-2'),
            name: 'Menu1-2',
            meta: { title: 'Menu1-2' },
            children: [
              {
                path: 'menu1-2-1',
                component: () => import('@/views/nested/menu1/menu1-2/menu1-2-1'),
                name: 'Menu1-2-1',
                meta: { title: 'Menu1-2-1' }
              },
              {
                path: 'menu1-2-2',
                component: () => import('@/views/nested/menu1/menu1-2/menu1-2-2'),
                name: 'Menu1-2-2',
                meta: { title: 'Menu1-2-2' }
              }
            ]
          },
          {
            path: 'menu1-3',
            component: () => import('@/views/nested/menu1/menu1-3'),
            name: 'Menu1-3',
            meta: { title: 'Menu1-3' }
          }
        ]
      },
      {
        path: 'menu2',
        component: () => import('@/views/nested/menu2/index'),
        name: 'Menu2',
        meta: { title: 'menu2' }
      }
    ]
  },

  {
    path: 'external-link',
    component: Layout,
    children: [
      {
        path: 'https://panjiachen.github.io/vue-element-admin-site/#/',
        meta: { title: 'External Link', icon: 'link' }
      }
    ]
  },

  // 404 page must be placed at the end !!!
  { path: '*', redirect: '/404', hidden: true }
]

const createRouter = () => new Router({
  // mode: 'history', // require service support
  scrollBehavior: () => ({ y: 0 }),
  routes: constantRoutes
})

const router = createRouter()

// Detail see: https://github.com/vuejs/vue-router/issues/1234#issuecomment-357941465
export function resetRouter() {
  const newRouter = createRouter()
  router.matcher = newRouter.matcher // reset router
}

export default router
