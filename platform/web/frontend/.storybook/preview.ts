import type { Preview } from '@storybook/react-vite'
import '../src/shared/styles/fonts.css'
import '../src/shared/styles/global.css'

const preview: Preview = { parameters: { a11y: { test: 'error' }, layout: 'padded' } }
export default preview
