/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  darkMode: 'class',
  theme: {
    extend: {
      colors: {
        'groove': {
          bg: '#0d1117',
          panel: '#161b22',
          border: '#30363d',
          accent: '#58a6ff',
          green: '#3fb950',
          red: '#f85149',
          yellow: '#d29922',
          text: '#c9d1d9',
          muted: '#8b949e',
        }
      }
    },
  },
  plugins: [],
}
