# Linux test box: bash 3.2 (official image, Alpine).
#   docker build -t autocomplete .
#   docker run --rm -it autocomplete

# Stage 1: base tools (cached; only rebuilt when this stage changes)
FROM bash:3.2 AS base
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
