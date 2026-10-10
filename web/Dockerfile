FROM node:20-slim

# ffmpeg package provides both ffmpeg and ffprobe
RUN apt-get update \
    && apt-get install -y --no-install-recommends ffmpeg \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY package*.json ./
RUN npm install --omit=dev

COPY . .

ENV PORT=3000 \
    FOOTAGE_DIR=/footage

EXPOSE 3000
USER node
CMD ["node", "server.js"]
