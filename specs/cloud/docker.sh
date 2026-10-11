#!/bin/sh
# docker --format value suggestions: sh docker.sh container|image|network|volume|ps|images|stats|networks|volumes|compose-ps
# Prints "value<TAB>description" lines.
row() { printf '%s\t%s\n' "$1" "$2"; }
inspect() {
    row json "whole object as JSON"
    row '{{json .}}' "whole object as one-line JSON"
}
table() {
    row table "default columns with headers"
    row json "one JSON object per line"
}
case $1 in
container)
    inspect
    row '{{.State.Status}}' "running, exited, ..."
    row '{{.State.Health.Status}}' "healthcheck status"
    row '{{.State.StartedAt}}' "start time"
    row '{{.State.ExitCode}}' "last exit code"
    row '{{.RestartCount}}' "restart count"
    row '{{.Config.Image}}' "image name"
    row '{{.Image}}' "image ID"
    row '{{range .NetworkSettings.Networks}}{{.IPAddress}} {{end}}' "IP addresses"
    row '{{json .NetworkSettings.Ports}}' "port bindings"
    row '{{json .Mounts}}' "mounts"
    row '{{range .Config.Env}}{{println .}}{{end}}' "environment, one per line"
    row '{{json .Config.Labels}}' "labels"
    row '{{.LogPath}}' "log file path"
    row '{{.HostConfig.RestartPolicy.Name}}' "restart policy" ;;
image)
    inspect
    row '{{.Id}}' "image ID"
    row '{{json .RepoTags}}' "tags"
    row '{{.Created}}' "creation time"
    row '{{.Size}}' "size in bytes"
    row '{{.Architecture}}/{{.Os}}' "platform"
    row '{{json .Config.Cmd}}' "default command"
    row '{{json .Config.Entrypoint}}' "entrypoint"
    row '{{json .Config.ExposedPorts}}' "exposed ports"
    row '{{range .Config.Env}}{{println .}}{{end}}' "environment, one per line"
    row '{{json .Config.Labels}}' "labels" ;;
network)
    inspect
    row '{{.Driver}}' "driver"
    row '{{range .IPAM.Config}}{{.Subnet}} {{end}}' "subnets"
    row '{{range .Containers}}{{.Name}} {{.IPv4Address}}{{println}}{{end}}' "attached containers" ;;
volume)
    inspect
    row '{{.Mountpoint}}' "host path"
    row '{{.Driver}}' "driver"
    row '{{.CreatedAt}}' "creation time"
    row '{{json .Labels}}' "labels" ;;
ps)
    table
    row '{{.Names}}' "names only"
    row '{{.ID}}' "IDs only"
    row 'table {{.Names}}\t{{.Status}}' "names and status"
    row 'table {{.Names}}\t{{.Image}}\t{{.Ports}}' "names, image, ports"
    row 'table {{.ID}}\t{{.Names}}\t{{.Status}}\t{{.Size}}' "with size (add -s)"
    row '{{.Names}}: {{.Label "com.docker.compose.project"}}' "compose project" ;;
images)
    table
    row '{{.Repository}}:{{.Tag}}' "repo:tag only"
    row '{{.ID}}' "IDs only"
    row 'table {{.Repository}}:{{.Tag}}\t{{.Size}}' "repo:tag and size"
    row 'table {{.Repository}}\t{{.Tag}}\t{{.ID}}\t{{.CreatedSince}}' "with age" ;;
stats)
    table
    row 'table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}' "CPU and memory"
    row 'table {{.Name}}\t{{.NetIO}}\t{{.BlockIO}}' "network and disk I/O"
    row '{{.Name}} {{.MemPerc}}' "memory percent" ;;
networks)
    table
    row '{{.Name}}' "names only"
    row 'table {{.Name}}\t{{.Driver}}\t{{.Scope}}' "name, driver, scope" ;;
volumes)
    table
    row '{{.Name}}' "names only"
    row 'table {{.Name}}\t{{.Driver}}\t{{.Mountpoint}}' "name, driver, path" ;;
compose-ps)
    table
    row '{{.Service}}' "service names"
    row 'table {{.Service}}\t{{.State}}\t{{.Ports}}' "service, state, ports" ;;
esac
