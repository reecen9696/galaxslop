# The whole site in one container: the relay plus the web build it serves
# (STATIC_DIR).  Build the browser variant and bundle it first:
#   ninja -C build/web smg && node tools/web/bundle.mjs
#   docker build -f server/Dockerfile -t smg-coop .
#   docker run -p 8787:8787 smg-coop        # http://localhost:8787/
# Put it behind HTTPS (the page needs a secure context off localhost).
# CI publishes it on every push to main (.github/workflows/docker.yml):
#   docker pull ghcr.io/reecen9696/galaxslop:latest
FROM node:22-slim
WORKDIR /app/server
COPY server/package.json server/package-lock.json ./
RUN npm ci --omit=dev
COPY server/src ./src
COPY dist/web /app/web
ENV PORT=8787 STATIC_DIR=/app/web NODE_ENV=production
EXPOSE 8787
HEALTHCHECK --interval=30s --timeout=3s CMD node -e "fetch('http://localhost:'+process.env.PORT+'/healthz').then(r=>process.exit(r.ok?0:1),()=>process.exit(1))"
USER node
CMD ["node", "--experimental-strip-types", "src/main.ts"]
