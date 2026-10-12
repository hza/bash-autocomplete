#!/bin/sh
# docker value suggestions: sh docker.sh log-level|pull|filter|filter-label|container|image|network|volume|ps|images|stats|networks|volumes|ps-filter|compose-ps
# Prints "key<TAB>value<TAB>description<TAB>[mark: pushpin or emoji]" lines (spec: snippets: true, typed as @key;
# pin = mark: pin, most used, listed with 📌).
row() { printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4"; }
inspect() {
    row j json "whole object as JSON"
    row o '{{json .}}' "whole object as one-line JSON"
}
table() {
    row t table "default columns with headers"
    row j json "one JSON object per line"
}
case $1 in
log-level) # plain value<TAB>description lines
    printf '%s\t%s\n' debug "everything, incl. API calls" info "normal messages (default)" \
        warn "warnings and errors" error "errors only" fatal "only fatal errors" ;;
filter|filter-label) # prune --filter (volume prune: labels only)
    [ "$1" = filter ] && printf '%s\t%s\n' until=24h "created more than a day ago" \
        until=168h "created more than a week ago" until=720h "created more than 30 days ago" \
        until=2026-01-01 "created before a date" until=1h "created more than an hour ago"
    printf '%s\t%s\n' label= "only with a label: label=KEY or label=KEY=VALUE" \
        'label!=' "only without a label: label!=KEY or label!=KEY=VALUE" ;;
pull) # docker run --pull
    printf '%s\t%s\n' missing "pull only if not present locally (default)" always "always pull, even if present" \
        never "use the local image only, fail if missing" ;;
container)
    inspect
    row s '{{.State.Status}}' "running, exited, ..." pushpin
    row h '{{.State.Health.Status}}' "healthcheck status"
    row t '{{.State.StartedAt}}' "start time"
    row x '{{.State.ExitCode}}' "last exit code"
    row r '{{.RestartCount}}' "restart count"
    row i '{{.Config.Image}}' "image name"
    row d '{{.Image}}' "image ID"
    row a '{{range .NetworkSettings.Networks}}{{.IPAddress}} {{end}}' "IP addresses" pushpin
    row p '{{json .NetworkSettings.Ports}}' "port bindings" pushpin
    row m '{{json .Mounts}}' "mounts"
    row e '{{range .Config.Env}}{{println .}}{{end}}' "environment, one per line" pushpin
    row l '{{json .Config.Labels}}' "labels"
    row g '{{.LogPath}}' "log file path"
    row y '{{.HostConfig.RestartPolicy.Name}}' "restart policy" ;;
image)
    inspect
    row i '{{.Id}}' "image ID"
    row t '{{json .RepoTags}}' "tags" pushpin
    row c '{{.Created}}' "creation time"
    row s '{{.Size}}' "size in bytes"
    row a '{{.Architecture}}/{{.Os}}' "platform"
    row d '{{json .Config.Cmd}}' "default command" pushpin
    row n '{{json .Config.Entrypoint}}' "entrypoint"
    row p '{{json .Config.ExposedPorts}}' "exposed ports" pushpin
    row e '{{range .Config.Env}}{{println .}}{{end}}' "environment, one per line"
    row l '{{json .Config.Labels}}' "labels" ;;
network)
    inspect
    row d '{{.Driver}}' "driver"
    row s '{{range .IPAM.Config}}{{.Subnet}} {{end}}' "subnets" pushpin
    row c '{{range .Containers}}{{.Name}} {{.IPv4Address}}{{println}}{{end}}' "attached containers" pushpin ;;
volume)
    inspect
    row p '{{.Mountpoint}}' "host path" pushpin
    row d '{{.Driver}}' "driver"
    row c '{{.CreatedAt}}' "creation time"
    row l '{{json .Labels}}' "labels" ;;
ps)
    table
    row n '{{.Names}}' "names only" pushpin
    row i '{{.ID}}' "IDs only"
    row s 'table {{.Names}}\t{{.Status}}' "names and status" pushpin
    row p 'table {{.Names}}\t{{.Image}}\t{{.Ports}}' "names, image, ports" pushpin
    row z 'table {{.ID}}\t{{.Names}}\t{{.Status}}\t{{.Size}}' "with size (add -s)"
    row c '{{.Names}}: {{.Label "com.docker.compose.project"}}' "compose project"
    row f 'table {{.ID}}\t{{.Image}}\t{{.Names}}\t{{.Status}}\t{{.Ports}}' "compact overview"
    row b '{{.ID}} {{.Names}}' "ID and name"
    row u 'table {{.Names}}\t{{.State}}' "names and state (running, exited, ...)"
    row a 'table {{.Names}}\t{{.RunningFor}}\t{{.Status}}' "age and uptime"
    row r 'table {{.Names}}\t{{.CreatedAt}}' "creation time"
    row g 'table {{.Names}}\t{{.Image}}' "names and images"
    row d 'table {{.Names}}\t{{.Command}}' "names and commands"
    row m 'table {{.Names}}\t{{.Mounts}}' "mounts"
    row v 'table {{.Names}}\t{{.LocalVolumes}}' "local volume count"
    row w 'table {{.Names}}\t{{.Networks}}' "networks"
    row l 'table {{.Names}}\t{{.Labels}}' "all labels" ;;
images)
    table
    row r '{{.Repository}}:{{.Tag}}' "repo:tag only" pushpin
    row i '{{.ID}}' "IDs only"
    row s 'table {{.Repository}}:{{.Tag}}\t{{.Size}}' "repo:tag and size" pushpin
    row a 'table {{.Repository}}\t{{.Tag}}\t{{.ID}}\t{{.CreatedSince}}' "with age" ;;
stats)
    table
    row c 'table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}' "CPU and memory" pushpin
    row i 'table {{.Name}}\t{{.NetIO}}\t{{.BlockIO}}' "network and disk I/O"
    row m '{{.Name}} {{.MemPerc}}' "memory percent" ;;
networks)
    table
    row n '{{.Name}}' "names only" pushpin
    row d 'table {{.Name}}\t{{.Driver}}\t{{.Scope}}' "name, driver, scope" ;;
volumes)
    table
    row n '{{.Name}}' "names only" pushpin
    row d 'table {{.Name}}\t{{.Driver}}\t{{.Mountpoint}}' "name, driver, path" ;;
ps-filter)
    row r status=running "running containers" pushpin
    row e status=exited "exited containers" pushpin
    row c status=created "created, never started"
    row p status=paused "paused containers"
    row s status=restarting "restarting containers"
    row d status=dead "dead containers"
    row u health=unhealthy "failing healthcheck" pushpin
    row h health=healthy "passing healthcheck"
    row x exited=0 "exited with code 0 (use with -a)"
    row f exited=1 "exited with code 1 (use with -a)"
    row t is-task=true "swarm service tasks" ;;
compose-ps)
    table
    row s '{{.Service}}' "service names"
    row p 'table {{.Service}}\t{{.State}}\t{{.Ports}}' "service, state, ports" pushpin ;;
esac
