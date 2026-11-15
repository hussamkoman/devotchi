# Devotchi server image (docs/deploy.md). The server speaks plain HTTP and
# WebSocket on port 8080; TLS is terminated by a reverse proxy such as the
# Caddy service in docker-compose.yml.

FROM node:24-slim AS manifests
WORKDIR /src
RUN corepack enable
# The client manifest is needed for the frozen lockfile check even though the
# client is not part of the image.
COPY package.json pnpm-lock.yaml pnpm-workspace.yaml tsconfig.base.json ./
COPY packages/shared/package.json packages/shared/
COPY packages/server/package.json packages/server/
COPY packages/client/package.json packages/client/

FROM manifests AS build
RUN pnpm install --frozen-lockfile --filter "@devotchi/server..."
COPY packages/shared packages/shared
COPY packages/server packages/server
# The server build encodes the pet catalog from these sources (dist/pets.json).
COPY pets pets
RUN pnpm --filter @devotchi/shared run build && pnpm --filter @devotchi/server run build

# A separate install, so that no development package reaches the virtual store.
FROM manifests AS deps
RUN pnpm install --frozen-lockfile --prod --ignore-scripts --filter "@devotchi/server..."

FROM node:24-slim
ENV NODE_ENV=production \
    DEVOTCHI_HOST=0.0.0.0 \
    DEVOTCHI_PORT=8080 \
    DEVOTCHI_DB_PATH=/data/devotchi.db
WORKDIR /app
# pnpm links packages through node_modules/.pnpm; copying the directories
# keeps those relative symlinks intact.
COPY --from=deps /src/package.json ./
COPY --from=deps /src/node_modules ./node_modules
COPY --from=deps /src/packages/server/node_modules packages/server/node_modules
COPY --from=build /src/packages/shared/package.json packages/shared/
COPY --from=build /src/packages/shared/dist packages/shared/dist
COPY --from=build /src/packages/server/package.json packages/server/
COPY --from=build /src/packages/server/dist packages/server/dist
COPY --from=build /src/packages/server/public packages/server/public
RUN mkdir -p /data && chown node:node /data
USER node
VOLUME /data
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD ["node", "-e", "fetch('http://127.0.0.1:' + (process.env.DEVOTCHI_PORT || 8080) + '/health').then((r) => process.exit(r.ok ? 0 : 1), () => process.exit(1))"]
CMD ["node", "packages/server/dist/index.js"]
