---
name: docker
description: Write, optimise and debug Dockerfiles and docker compose setups - small secure images, fast cached builds, healthy multi-service stacks. Use for containerising apps or fixing container problems.
---
# Docker

## Dockerfiles
- Pin base images to a specific version (and digest for production); prefer slim/distroless variants.
- Multi-stage builds: compile in a builder stage, copy only artifacts into the runtime stage.
- Order for caching: copy dependency manifests and install first, then copy source.
- One process per container; `CMD` in exec form (`["node", "server.js"]`); handle SIGTERM.
- Run as a non-root user; no secrets in layers (use build secrets or runtime env); `.dockerignore` for `.git`, `node_modules`, build output, `.env`.
- Add a `HEALTHCHECK` or rely on the orchestrator's probes; set `EXPOSE` and document env vars.

## Compose
Named volumes for data, explicit networks, `depends_on` with `condition: service_healthy`, env files for local config, no `latest` tags.

## Debugging
`docker build --progress=plain`, `docker run -it --entrypoint sh image`, `docker logs -f`, `docker inspect`, `docker compose config` to see the resolved file. Check the image size with `docker image ls` and layers with `docker history`.
