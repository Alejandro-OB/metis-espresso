import { lazy, Suspense } from 'react'
import { createBrowserRouter, Navigate, Outlet, RouterProvider } from 'react-router-dom'
import { AppShell, LoadingState } from '@/shared/ui'
import { useAuth } from './auth-context'

const Dashboard = lazy(() => import('@/routes/dashboard'))
const Extractions = lazy(() => import('@/routes/extractions'))
const Comparison = lazy(() => import('@/routes/comparison'))
const ExtractionDetail = lazy(() => import('@/routes/extraction-detail'))
const StartExtraction = lazy(() => import('@/routes/start-extraction'))
const LiveExtraction = lazy(() => import('@/routes/live-extraction'))
const Coffees = lazy(() => import('@/routes/coffees'))
const CoffeeDetail = lazy(() => import('@/routes/coffee-detail'))
const Profiles = lazy(() => import('@/routes/profiles'))
const ProfileDetail = lazy(() => import('@/routes/profile-detail'))
const ProfileComparison = lazy(() => import('@/routes/profile-comparison'))
const Traceability = lazy(() => import('@/routes/traceability'))
const Equipment = lazy(() => import('@/routes/equipment'))
const Calibrations = lazy(() => import('@/routes/calibrations'))
const WaterReferences = lazy(() => import('@/routes/water-references'))
const CalibrationDetail = lazy(() => import('@/routes/calibration-detail'))
const Episodes = lazy(() => import('@/routes/episodes'))
const EpisodeDetail = lazy(() => import('@/routes/episode-detail'))
const Recommendations = lazy(() => import('@/routes/recommendations'))
const BaristaGuide = lazy(() => import('@/routes/barista-guide'))
const Prepare = lazy(() => import('@/routes/prepare'))
const Simulations = lazy(() => import('@/routes/simulations'))
const SimulationDetail = lazy(() => import('@/routes/simulation-detail'))
const RecommendationDetail = lazy(() => import('@/routes/recommendation-detail'))
const System = lazy(() => import('@/routes/system'))
const NotFound = lazy(() => import('@/routes/not-found'))

function RootLayout() {
  const { user, logout } = useAuth()
  return <AppShell user={user!} onLogout={logout}><Suspense fallback={<LoadingState/>}><Outlet /></Suspense></AppShell>
}

function AdminOnly() {
  const { user } = useAuth()
  return user?.role === 'admin' ? <Outlet/> : <Navigate to="/" replace/>
}

const routes = createBrowserRouter([
  {
    element: <RootLayout/>,
    children: [
      { path: '/', element: <Navigate to="/prepare" replace/> },
      { path: '/summary', element: <Dashboard/> },
      { path: '/extractions', element: <Extractions/> },
      { path: '/compare', element: <Comparison/> },
      { path: '/extractions/:id', element: <ExtractionDetail/> },
      { path: '/extract', element: <StartExtraction/> },
      { path: '/extract/:id', element: <LiveExtraction/> },
      { path: '/coffees', element: <Coffees/> },
      { path: '/coffees/:id', element: <CoffeeDetail/> },
      { path: '/profiles', element: <Profiles/> },
      { path: '/profiles/compare', element: <ProfileComparison/> },
      { path: '/profiles/:id', element: <ProfileDetail/> },
      { path: '/traceability/:kind/:id', element: <Traceability/> },
      { path: '/equipment', element: <Equipment/> },
      { path: '/calibrations', element: <Calibrations/> },
      { path: '/water-references', element: <WaterReferences/> },
      { path: '/calibrations/:id', element: <CalibrationDetail/> },
      { path: '/episodes', element: <Episodes/> },
      { path: '/episodes/:id', element: <EpisodeDetail/> },
      { path: '/recommendations', element: <Recommendations/> },
      { path: '/guide', element: <BaristaGuide/> },
      { path: '/prepare', element: <Prepare/> },
      { path: '/recommendations/:id', element: <RecommendationDetail/> },
      { path: '/simulations', element: <Simulations/> },
      { path: '/simulations/:id', element: <SimulationDetail/> },
      { element: <AdminOnly/>, children: [{ path: '/system', element: <System/> }] },
      { path: '*', element: <NotFound/> },
    ],
  },
])

export function AppRouter() {
  return <RouterProvider router={routes}/>
}
