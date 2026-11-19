FROM node:24.7.0-bookworm-slim AS dependencies
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci

FROM dependencies AS build
COPY . .
RUN npm run build

FROM nginxinc/nginx-unprivileged:1.29.1-alpine AS runtime
COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /app/dist /usr/share/nginx/html
EXPOSE 8080

FROM mcr.microsoft.com/playwright:v1.63.0-noble AS e2e
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci
COPY e2e ./e2e
COPY playwright.config.ts ./
CMD ["npm", "run", "e2e:compose"]
