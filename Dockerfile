# Linux test box: official bash image (Alpine), bash 3.2 by default.
#   make docker-build [BASH=5.0]  /  make docker-run [BASH=5.0]
ARG BASH=3.2

# Stage 1: base tools (cached; only rebuilt when this stage changes)
FROM bash:${BASH} AS base
RUN apk add --no-cache gcc musl-dev make man-pages mandoc git

# Stage 2: build and install the app
FROM base AS app
WORKDIR /src
COPY Makefile ./
COPY src src
COPY specs specs
# gcc-only heuristic: flags snprintf into PATH_MAX buffers (truncation is intended)
RUN make CFLAGS="-O2 -Wall -Wextra -Wno-format-truncation" && make install PROFILE=/root/.bashrc

ENV SHELL=/usr/local/bin/bash
CMD ["bash", "--rcfile", "/root/.bashrc", "-i"]
