#!/bin/sh
# pmset value suggestions: sh pmset.sh get|bool|minutes|seconds|percent|ups|hibernatemode|type|weekdays|time|datetime
# Prints "value<TAB>description" lines.
row() { printf '%s\t%s\n' "$1" "$2"; }
case $1 in
get)
    row live "settings in use"
    row custom "custom settings for every power source"
    row cap "power management features this Mac supports"
    row sched "scheduled wake/sleep/power events"
    row ups "UPS emergency thresholds"
    row batt "battery and UPS state"
    row ps "battery and UPS state"
    row pslog "running log of power source state"
    row rawlog "running log of raw battery state"
    row therm "thermal conditions limiting CPU speed"
    row thermlog "log of thermal notifications"
    row assertions "power assertions (who keeps the Mac awake)"
    row assertionslog "log of assertion creation and release"
    row sysload "system load advisory"
    row sysloadlog "log of system load advisory changes"
    row ac "attached power adapter details"
    row adapter "attached power adapter details"
    row log "history of sleeps, wakes and power events"
    row uuid "current sleep/wake UUID"
    row uuidlog "sleep/wake UUIDs as they are set"
    row history "timeline of sleep/wake UUIDs (needs boot-arg)"
    row historydetailed "driver timings for one sleep/wake UUID"
    row powerstate "power states of I/O Kit drivers"
    row powerstatelog "periodic power state residency of drivers"
    row stats "sleep and wake counts since boot"
    row systemstate "current system power state and capabilities"
    row everything "output of every -g option" ;;
bool)
    row 1 "on"
    row 0 "off" ;;
minutes)
    row 0 "never"
    row 1 "1 minute"
    row 5 "5 minutes"
    row 10 "10 minutes"
    row 15 "15 minutes"
    row 30 "30 minutes"
    row 60 "1 hour"
    row 180 "3 hours" ;;
seconds)
    row 900 "15 minutes"
    row 3600 "1 hour"
    row 10800 "3 hours"
    row 86400 "1 day" ;;
percent)
    row 20 "20 %"
    row 50 "50 % (default)"
    row 80 "80 %" ;;
ups)
    row -1 "off"
    row 25 "25 (percent or minutes)"
    row 40 "40 (percent or minutes)" ;;
hibernatemode)
    row 0 "plain sleep, memory only (desktop default)"
    row 3 "memory powered, image on disk (laptop default)"
    row 25 "hibernate: image on disk, memory off" ;;
type)
    row sleep "put the Mac to sleep"
    row wake "wake from sleep"
    row poweron "power on from off"
    row shutdown "shut down"
    row wakeorpoweron "wake or power on" ;;
weekdays)
    row MTWRFSU "every day"
    row MTWRF "weekdays"
    row SU "weekend" ;;
time)
    row 08:00:00 "8 AM"
    row 12:00:00 "noon"
    row 20:00:00 "8 PM"
    row 23:00:00 "11 PM" ;;
datetime)
    row "$(date -v+1d +%m/%d/%y) 08:00:00" "tomorrow 8 AM (MM/dd/yy HH:mm:ss)" ;;
esac
