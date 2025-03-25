#!/bin/bash
#
# CS695 Conductor that manages containers 
# Author: Pratik
#
echo -e "\e[1;32mCS695 Conductor that manages containers\e[0m"

# If not root then exit
[ "$EUID" -ne "0" ] && echo "This script needs root permissions" && exit 1

set -o errexit
set -o nounset
umask 077

source "$(dirname "$0")/setup.sh"

############## utility function start

die() {
    echo -e "\e[1;31mERROR: $1\e[0m" >&2
    exit 1
}

check_prereq() {
    [ "$EUID" -ne "0" ] && die "This script needs root permissions"
    for t in $NEEDED_TOOLS; do which "$t" >/dev/null || die "Missing tool: $t"; done
    mkdir -p "$IMAGEDIR" "$CONTAINERDIR" "$CACHEDIR" || die "Failed to create directories"
}

get_next_num() {
    local NUM=1
    if [ -f "$CACHEDIR/.HIGHEST_NUM" ]; then
        NUM=$(( 1 + $(< "$CACHEDIR/.HIGHEST_NUM" )))
    fi

    echo $NUM > "$CACHEDIR/.HIGHEST_NUM"
    printf "%x" $NUM
}

wait_for_dev() {
    local iface="$1"
    local in_ns="${2:-}"
    local retries=5 # max retries
    local nscmd=

    [ -n "$in_ns" ] && nscmd="ip netns exec $in_ns"
    while [ "$retries" -gt "0" ]; do
        if ! $nscmd ip addr show dev $iface | grep -q tentative; then return 0; fi
        sleep 0.5
        retries=$((retries -1))
    done
}

process_instruction() {
    local instruction="$1"
    local parent_layers="$2"
    
    # Extract command type (first word)
    local cmd_type="${instruction%% *}"
    local cmd_args="${instruction#* }"

    case "$cmd_type" in
        RUN)
            handle_run "$cmd_args" "$parent_layers"
            ;;
        COPY)
            handle_copy "$cmd_args" "$parent_layers"
            ;;
        *)
            die "Unsupported instruction: $cmd_type"
            ;;
    esac
}

get_layer_hash() {
    echo "$1" | awk -F: '{print $NF}' | awk -F/ '{print $NF}'
}

update_layer_stack() {
    local current_layer="$1"
    local layer_stack="$2"
    
    if [ -z "$layer_stack" ]; then
        echo "$current_layer"
    else
        echo "$layer_stack:$current_layer"
    fi
}

############## utility function end

# Subtask 3.f
# Create a new layer for RUN instruction
# Mount the overlay filesystem and execute the command
# Unmount the overlay filesystem after executing the command
# Record the metadata and parent layer for the new layer
handle_run() {
    local command="$1"
    local parent_layers="$2"
    local parent_hash=$(get_layer_hash "$parent_layers")
    
    # Generate unique hash for this command
    local cmd_hash=$(echo "$command" | sha256sum | cut -d' ' -f1)
    local layer_hash=$(echo "RUN-${parent_hash}-${cmd_hash}" | sha256sum | cut -d' ' -f1)
    
    # Check cache
    if [ -d "$CACHEDIR/layers/$layer_hash" ]; then
        echo "Using cached RUN layer: $layer_hash"
        current_layer="$CACHEDIR/layers/$layer_hash/diff"
        return
    fi
    
    # Subtask 3.f.1
    # Create new layer
    mkdir -p "$CACHEDIR/layers/$layer_hash/diff"
    mkdir -p "$CACHEDIR/layers/$layer_hash/work" 

    # # Subtask 3.f.2
    # # Temporarily mount the overlay filesystem
    local temp_mount="$CACHEDIR/layers/$layer_hash/temp_mount"
    mkdir -p "$temp_mount"

    local ABS_PARENT=""
    IFS=':' read -r -a layer_array <<< "$parent_layers"
    for layer in "${layer_array[@]}"; do
        local abs_layer
        abs_layer=$(realpath "$layer")
        abs_layer=$(echo "$abs_layer" | sed 's|/diff/diff|/diff|g')
        if [ -z "$ABS_PARENT" ]; then
            ABS_PARENT="$abs_layer"
        else
            ABS_PARENT="$ABS_PARENT:$abs_layer"
        fi
    done
    [ -z "$ABS_PARENT" ] && die "ABS_PARENT is empty; parent_layers ($parent_layers) did not convert properly"
    
    mount -t overlay overlay -o lowerdir="$ABS_PARENT",upperdir="$CACHEDIR/layers/$layer_hash/diff",workdir="$CACHEDIR/layers/$layer_hash/work" "$temp_mount"

    # # Subtask 3.f.3
    # # Execute the command in the new mount
    chroot "$temp_mount" /bin/bash -c "$command"

    # # Subtask 3.f.4
    # # Cleanup and record metadata
    umount -l "$temp_mount"
    rmdir "$temp_mount"
    mkdir -p "$CACHEDIR/layers/$layer_hash"
    echo "RUN $command" > "$CACHEDIR/layers/$layer_hash/metadata"
    echo "$parent_hash" > "$CACHEDIR/layers/$layer_hash/parent"
    current_layer="$CACHEDIR/layers/$layer_hash"
    echo "$current_layer" > "$CACHEDIR/layers/.last_layer"

    
}


# Subtask 3.e
# Create a new layer for COPY instruction
# Mount the overlay filesystem and copy the files from source to destination
# Unmount the overlay filesystem after copying the files
# Record the metadata and parent layer for the new layer
handle_copy() {
    local args="$1"
    local parent_layers="$2"
    local parent_hash=$(get_layer_hash "$parent_layers")
    
    # Parse COPY arguments
    IFS=' ' read -r src dest <<< "$args"
    [ -z "$src" ] && die "COPY requires source path"
    [ -z "$dest" ] && die "COPY requires destination path"
    
    # Generate content hash
    local content_hash=$(find "$src" -type f -exec sha256sum {} + | sha256sum | cut -d' ' -f1)
    local layer_hash=$(echo "COPY-${parent_hash}-${content_hash}" | sha256sum | cut -d' ' -f1)

    # Lesson: Check in the cache if the layer exists
    if [ -d "$CACHEDIR/layers/$layer_hash" ]; then
        echo "Using cached COPY layer: $layer_hash"
        current_layer="$CACHEDIR/layers/$layer_hash/diff"
        return
    fi
    
    # Subtask 3.e.1
    # Create a new layer
    mkdir -p "$CACHEDIR/layers/$layer_hash/diff"
    mkdir -p "$CACHEDIR/layers/$layer_hash/work"

    # Subtask 3.e.2
    # Temporarily mount the overlay filesystem
    local temp_mount="$CACHEDIR/layers/$layer_hash/temp_mount"
    mkdir -p "$temp_mount"
    mount -t overlay overlay -o lowerdir="$parent_layers",upperdir="$CACHEDIR/layers/$layer_hash/diff",workdir="$CACHEDIR/layers/$layer_hash/work" "$temp_mount"
    
    
    # Subtask 3.e.3
    # Copy the files from source to destination
    cp -a "$src" "$temp_mount/$dest"
    
    # Subtask 3.e.4
    # Unmount the overlay filesystem
    umount -l "$temp_mount"
    rmdir "$temp_mount"

    # Record metadata and parent layer
    echo "COPY $src $dest" > "$CACHEDIR/layers/$layer_hash/metadata"
    echo "$parent_hash" > "$CACHEDIR/layers/$layer_hash/parent"
    current_layer="$CACHEDIR/layers/$layer_hash"
    echo "$current_layer" > "$CACHEDIR/layers/.last_layer"
}

# Subtask 3.d
# This function will download debian container image using debootstrap 
# Downloaded images are stored within "$IMAGEDIR" directory. 
# Multiple images with different names can be created using this function
build() {
    local NAME=${1:-}
    [ -z "$NAME" ] && die "Image name required"
    [ -d "$IMAGEDIR/$NAME" ] && die "Image $NAME exists"

    # Lesson: Get the base image from the given file. If the file does not exist, use Conductorfile
    local CONDUCTORFILE="${2:-Conductorfile}"

    # check if conductorfile exists
    [ -f "$CONDUCTORFILE" ] || die "Conductorfile not found"
    
    # Parse base image
    local FROM_LINE=$(grep -m1 "^FROM " "$CONDUCTORFILE")
    [[ $FROM_LINE =~ FROM[[:space:]]([^:]+):([^[:space:]]+) ]] || die "Invalid FROM format"
    local DISTRO="${BASH_REMATCH[1]}" VERSION="${BASH_REMATCH[2]}"

    local BASE_KEY="${DISTRO}:${VERSION}" BASE_NAME="${DISTRO}-${VERSION}"
    
    # Debootstrap if missing
    if [ ! -d "$CACHEDIR/base/$BASE_NAME" ]; then
        mkdir -p "$CACHEDIR/base/$BASE_NAME"

        # Lesson: This is how debootstrap can be invoked to build filesystem for the container
        echo "=== DEBOOTSTRAP START ==="
        debootstrap "${BASE_SUITES[$BASE_KEY]}" "$CACHEDIR/base/$BASE_NAME" "${BASE_MIRRORS[$DISTRO]}" || die "Failed to create image $NAME"
        echo "=== DEBOOTSTRAP COMPLETE ==="
    fi

    # Remove on implementation of 3.d.1 <---
    # cp -a "$CACHEDIR/base/$BASE_NAME/" "$IMAGEDIR/$NAME"
    # echo -e "\e[1;32mImage $NAME built without any layers\e[0m"
    # Remove on implementation of 3.d.1 <---

    # # Subtask 3.d.1 - start 
    # # Uncomment the below code to implement layering
    # # Store the base layer and the layer stack in the image directory to be used later
    local BASE_LAYER=$(realpath "$CACHEDIR/base/$BASE_NAME")
    local LAYER_STACK="$BASE_LAYER"
    
    # # For subtask 3.e and 3.f
    while IFS= read -r instruction; do
        process_instruction "$instruction" "$LAYER_STACK"
        LAYER_STACK=$(update_layer_stack "$current_layer/diff" "$LAYER_STACK")
    done < <(grep -E '^(RUN|COPY)' "$CONDUCTORFILE")
    
    mkdir -p "$IMAGEDIR/$NAME"
    echo "$LAYER_STACK" > "$IMAGEDIR/$NAME/layers"
    echo -e "\e[1;32mImage ${NAME:-} built with $(( $(echo "${LAYER_STACK}" | tr -dc ':' | wc -c) + 1 )) layers\e[0m"
    # # Subtask 3.d.1 - end
}

# This function shows all downloaded container images that are kept in .images directory
images() {
    local IMAGES=$(ls -1 "$IMAGEDIR" 2>/dev/null || true)
    if [ -z "$IMAGES" ]; then
        echo -e "\e[1;31mNo images found\e[0m"
    else
        printf "%-20s %-10s %s\n" "Name" "Size" "Date"
        for i in $IMAGES; do
            local SIZE=$(du -sh "$IMAGEDIR/$i" | awk '{print $1}')
            local DATE=$(stat -c %y "$IMAGEDIR/$i" | awk '{print $1}')
            printf "%-20s %-10s %s\n" "$i" "$SIZE" "$DATE"
        done
    fi
}

# This function deletes a container image
remove_image() {
    local NAME=${1:-}
    [ -z "$NAME" ] && die "Image name is required"
    [ -d "$IMAGEDIR/$NAME" ] || die "Image $NAME does not exist"

    rm -rf "$IMAGEDIR/$NAME"
    echo -e "\e[1;32mImage $NAME removed\e[0m"
}

# This function deletes all caches
rmcache() {
    # Check active containers first
    local ACTIVE_CONTAINERS=$(ls "$CONTAINERDIR" 2>/dev/null)
    [ -n "$ACTIVE_CONTAINERS" ] && die "Active containers exist:\n$ACTIVE_CONTAINERS"

    # Remove unused layers
    if [ ! -d "$CACHEDIR/layers" ]; then
        echo -e "\e[1;31mNo cached layers found\e[0m"
    else
        # Remove unused layers
        find "$CACHEDIR/layers" -mindepth 1 -maxdepth 1 -type d | while read -r layer; do
            local layer_hash=$(basename "$layer")
            if ! grep -qr "$layer_hash" "$IMAGEDIR"; then
                rm -rf "$layer"
                echo "Removed unused layer: $layer_hash"
            fi
        done
    fi

    # Process each base cache
    find "$CACHEDIR/base" -mindepth 1 -maxdepth 1 -type d | while read -r base; do
        local BASE_NAME=$(basename "$base")
        # Unmount if needed
        if mount | grep -q "$base"; then
            umount "$base/merged" 2>/dev/null || true
        fi
        rm -rf "$base"
        echo -e "\e[1;32mRemoved base cache: $BASE_NAME\e[0m"
    done
}

# Subtask 3.a / 3.d
# This function should use unshare and chroot to run a container from given image.
# You also need to mount appropriate filesystems to the rootfs within the container
# to enable tools tools that utilize those filesystems e.g. ps, top, ifconfig etc. to
# be confined within the container isolation
run() {
    local IMAGE=${1:-}
    local NAME=${2:-}

    [ -z "$NAME" ] && die "Container name is required"
    [ -z "$IMAGE" ] && die "Image name is required"

    [ -d "$IMAGEDIR/$IMAGE" ] || die "Image $IMAGE does not exist"
    [ -d "$CONTAINERDIR/$NAME" ] && die "Container $NAME already exists"

    # Remove on implementation of 3.d.2 <---
    # mkdir -p "$CONTAINERDIR/$NAME/rootfs"
    # cp -a "$IMAGEDIR/$IMAGE"/* "$CONTAINERDIR/$NAME/rootfs" //Commented for task 3d2
    # Remove on implementation of 3.d.2 <---

    # Subtask 3.d.2 - start
    # Create a new directory for the container rootfs
    # Read the layer stack from the image directory and mount the overlay filesystem
    mkdir -p "$CONTAINERDIR/$NAME"/{upper,work,merged}
    # local LOWER=$(cat "$IMAGEDIR/$IMAGE/layers")

    # mount -t overlay overlay -o lowerdir="$LOWER",upperdir="$CONTAINERDIR/$NAME/upper",workdir="$CONTAINERDIR/$NAME/work" "$CONTAINERDIR/$NAME/merged"

    local LOWER
    LOWER=$(cat "$IMAGEDIR/$IMAGE/layers")
    local ABS_LOWER=""
    IFS=':' read -r -a layer_array <<< "$LOWER"
    for layer in "${layer_array[@]}"; do
        local abs_layer
        abs_layer=$(realpath "$layer")
        abs_layer=$(echo "$abs_layer" | sed 's|/diff/diff|/diff|g')
        if [ -z "$ABS_LOWER" ]; then
            ABS_LOWER="$abs_layer"
        else
         ABS_LOWER="$ABS_LOWER:$abs_layer"
        fi
    done

    mount -t overlay overlay -o lowerdir="$ABS_LOWER",upperdir="$(realpath "$CONTAINERDIR/$NAME/upper")",workdir="$(realpath "$CONTAINERDIR/$NAME/work")" "$(realpath "$CONTAINERDIR/$NAME/merged")"
 
    
    # Subtask 3.d.2 - end

    shift 2
    # this is the init command that should be run within the container
    local INIT_CMD_ARGS=${@:-/bin/bash} # if no command is given, then substitute by /bin/bash

    # Subtask 3.a.1
    # You should bind mount /dev within the container root fs
    # CONTAINER_ROOTFS="$CONTAINERDIR/$NAME/rootfs"
    # mount --bind /dev "$CONTAINER_ROOTFS/dev"

    # Subtask 3.d.3
    # Modify subtask 3.a.1 to bind mount /dev
    CONTAINER_ROOTFS="$CONTAINERDIR/$NAME/merged"
    mount --bind /dev "$CONTAINER_ROOTFS/dev"

    # Subtask 3.a.2
    # - Use unshare to run the container in a new [uts, pid, net, mount, ipc] namespaces
    # - You should change the root to the rootfs that has been created for the container
    # - procfs and sysfs should be mounted within the container for proper isolated execution
    #   of processes within the container
    # - When unshare process exits all of its children also exit (--kill-child option)
    # - permission of root dir within container should be set to 755 for apt to work correctly
    # - $INIT_CMD_ARGS should be the entry program for the container
    #This is added for 3.a.2
    # unshare --uts --pid --net --mount --ipc --fork --kill-child bash -c "chmod 755 $CONTAINER_ROOTFS && chroot $CONTAINER_ROOTFS /bin/bash -i -c \"mount -t proc proc /proc && mount -t sysfs sys /sys && exec $INIT_CMD_ARGS\""
    # Subtask 3.d.3
    # Modify subtask 3.a.2 to use the overlay filesystem
    unshare --uts --pid --net --mount --ipc --fork --kill-child bash -c "chroot $CONTAINER_ROOTFS /bin/bash -c \"mount -t proc proc /proc && mount -t sysfs sys /sys && exec $INIT_CMD_ARGS\""

}

# This will show containers that are currently running
show_containers() {
    local CONTAINERS=$(ls -1 "$CONTAINERDIR" 2>/dev/null || true)

    if [ -z "$CONTAINERS" ]; then
        echo "No containers found"
    else
        printf "%-20s %-10s\n" "Name" "Date"
        for i in $CONTAINERS; do
            local DATE=$(stat -c %y "$CONTAINERDIR/$i" | awk '{print $1}')
            printf "%-20s %-10s\n" "$i" "$DATE"
        done
    fi
}


# This function will stop a running container
# To stop a container you need to kill the entry process within the container
# You should also unmount any mount points you created while running the container
stop() {
    local NAME=${1:-}

    [ -z "$NAME" ] && die "Container name is required"

    [ -d "$CONTAINERDIR/$NAME" ] || die "Container $NAME does not exist"

    # Subtask 3.d.3
    # Modify the below code to use the overlay filesystem
    # Lesson: Getting the pid of the entry process within the container
    # local PID=$(ps -ef | grep "$CONTAINERDIR/$NAME/rootfs" | grep -v grep | awk '{print $2}')
    local PID=$(ps -ef | grep "$CONTAINERDIR/$NAME/merged" | grep -v grep | awk '{print $2}')     

    # Lesson: Delete the ip link created in host for the container
    if [ -e "/sys/class/net/${NAME}-outside" ]; then
        ip link delete "${NAME}-outside"
    fi

    # Lesson: Kill the process and unmount unused points
    [ -z $PID ] || kill -9 $PID

    # Subtask 3.d.3
    # Modify the below code to use the overlay filesystem
    # This is a comprehensive list of unmounts
    # You can remove any if not required depending on how you mounted them
    # umount "$CONTAINERDIR/$NAME/rootfs/proc" > /dev/null 2>&1 || :
    # umount "$CONTAINERDIR/$NAME/rootfs/sys" > /dev/null 2>&1 || :
    # umount "$CONTAINERDIR/$NAME/rootfs/dev" > /dev/null 2>&1 || :

    unmount "$CONTAINERDIR/$NAME/merged/proc" > /dev/null 2>&1 || :
    unmount "$CONTAINERDIR/$NAME/merged/sys" > /dev/null 2>&1 || :
    unmount -l "$CONTAINERDIR/$NAME/merged/dev" > /dev/null 2>&1 || :


    # Subtask 3.d.4
    # Unmount the overlay filesystem
    umount -l "$CONTAINERDIR/$NAME/merged" > /dev/null 2>&1 || :

    # Deletes the container file
    rm -rf "$CONTAINERDIR/$NAME"
    [ -z "$(ls -1 "$CONTAINERDIR" 2>/dev/null || true)" ] && rm -f "$EXTRADIR/.HIGHEST_NUM" &&  iptables -P FORWARD DROP && iptables -F FORWARD && iptables -t nat -F
    echo -e "\e[1;32m$NAME successfully removed\e[0m"
}

# Subtask 3.b
# This function will execute a program within a running container
exec() {
    # Hint: nsenter can be used to execute a process in existing namespace
    local NAME=${1:-}
    local CMD=${2:-}

    [ -z "$NAME" ] && die "Container name is required"
    
    shift # shift arguments so that remaining arguments represent the program and its arguments to execute

    # if no command is given then substitute with /bin/bash
    local EXEC_CMD_ARGS=${@:-/bin/bash}

    [ -d "$CONTAINERDIR/$NAME" ] || die "Container $NAME does not exist"
    echo -e "\e[1;32mExecuting $CMD in $NAME container!\e[0m"

    # Subtask 3.d.3
    # Modify the below code to use the overlay filesystem
    # This is the PID of the unshare process for the given container
    # local UNSHARE_PID=$(ps -ef | grep "$CONTAINERDIR/$NAME/rootfs" | grep -v grep | awk '{print $2}')
    # local UNSHARE_PID=$(ps -ef | grep "$CONTAINERDIR/$NAME/merged" | grep -v grep | awk '{print $2}')
    local UNSHARE_PID=$(ps -ef | grep "unshare" | grep "$CONTAINERDIR/$NAME/merged" | grep -v grep | awk '{print $2}' | head -n1)


    [ -z "$UNSHARE_PID" ] && die "Cannot find container process"

    # This is the PID of the process that unshare executed within the container
    local CONTAINER_INIT_PID=$(pgrep -P $UNSHARE_PID | head -1)
    [ -z "$CONTAINER_INIT_PID" ] && die "Cannot find container process"

    # Subtask 3.b.1
    # Write command to join the existing namespace {all namespaces: uts, pid, net, mount, ipc} 
    # of the running container and execute the given command and args. 
    # You should use $EXEC_CMD_ARGS to pass the command and arguments 
    # The executed process should be within correct namespace and root
    # directory as of the container and tools like ps, top should show only processes
    # running within the container

    # local CONTAINER_ROOTFS=$(realpath "$CONTAINERDIR/$NAME/rootfs")
    # nsenter --uts --pid --net --mount --ipc --target "$CONTAINER_INIT_PID" chroot "$CONTAINER_ROOTFS" $EXEC_CMD_ARGS

    #this is for Subtask 4
   local CONTAINER_ROOTFS=$(realpath "$CONTAINERDIR/$NAME/merged")
    nsenter --uts --pid --net --mount --ipc --target "$CONTAINER_INIT_PID" chroot "$CONTAINER_ROOTFS" $EXEC_CMD_ARGS 

}

# Subtask 3.c
# This function is used to setup networking capabilities of containers
# using tools like iproute2 (ip command), iptables etc.
# addnetwork() {
#     local NAME=${1:-}
#     [ -z "$NAME" ] && die "Container name is required"
    
#     # Lesson: This folder within host is used by iproute2 to store 
#     # the network namespace inode which can be used to operate on that
#     # network namespace
#     # 
#     # If you create a network namespace using iproute2, this folder stores
#     # link to inode of the created netns. inode of the network namespace is 
#     # required to be present within this directory to use iproute2 to manipulate 
#     # network namespace configuration.
    
#     # But since we are using unshare to create netns, it will be empty. To 
#     # use iproutes2, we are manually linking the netns inode unshare created
#     # within this directory

#     local NETNSDIR="/var/run/netns"

#     if [ ! -e $NETNSDIR ]; then
#         mkdir -p $NETNSDIR
#     fi

#     # Subtask 3.d.3
#     # Modify the below code to use the overlay filesystem (Use only one pid)
#     # local PID=$(ps -ef | grep "$CONTAINERDIR/$NAME/rootfs" | grep -v grep | awk '{print $2}')
#     local PID=$(ps -ef | grep "unshare"| grep "$CONTAINERDIR/$NAME/merged" | grep -v grep | awk '{print $2}')

#     # local CONDUCTORNS="/proc/$PID/ns/net"
#     # local NSDIR=$NETNSDIR/$NAME

#     # if [ -e CONDUCTORNS ]; then
# 	#     rm $NSDIR
#     # fi
#     # ln -sf $CONDUCTORNS $NSDIR

#     if [ -e "$NSDIR" ]; then
#         rm -f "$NSDIR"  # Change: Remove existing symlink if it exists.
#     fi
#     ln -sf "$CONDUCTORNS" "$NSDIR"

#     # Finally we can use iproute2 for configuring network within our network namespace
#     local NUM=$(get_next_num "$NAME") # Getting unique interface identifier

#     # Building the ip address for the link
#     [ -z "$IP4_PREFIX" ] && IP4_PREFIX="${IP4_SUBNET}.$((0x$NUM))." 

#     INSIDE_IP4="${IP4_PREFIX}2"
#     OUTSIDE_IP4="${IP4_PREFIX}1"
#     INSIDE_PEER="${NAME}-inside"
#     OUTSIDE_PEER="${NAME}-outside"

#     # Subtask 3.c.1
#     # Add a veth links (It is a peer link connecting two points) connecting the container's network 
#     # namespace to the root(host) namespace. The veth link will have two interfaces.
#     # Inside the container, it should use INSIDE_PEER interface and within the host it should use
#     # OUTSIDE_PEER interface
#     # You should use iproute2 tool (ip command)

#     if ip link show "$INSIDE_PEER" &>/dev/null; then
#         ip link delete "$INSIDE_PEER"   
#     fi
#     if ip link show "$OUTSIDE_PEER" &>/dev/null; then
#         ip link delete "$OUTSIDE_PEER"  
#     fi

#     ip link add "$INSIDE_PEER" type veth peer name "$OUTSIDE_PEER"
#     ip link set "$INSIDE_PEER" netns "$NAME"
#     ip addr add "$OUTSIDE_IP4"/"$IP4_PREFIX_SIZE" dev "$OUTSIDE_PEER"
#     ip link set "$OUTSIDE_PEER" up


#     # Lesson: By default linux does not forward packets, it only acts as an end host
#     # We need to enable packet forwarding capability to forward packets to our containers
#     echo 1 > /proc/sys/net/ipv4/ip_forward

#     # Subtask 3.c.2
#     # Enable the interfaces that you have created within the host and the container
#     # You should also enable lo interface within the container (which is disabled by default)
#     # In total here 3 interfaces should be enabled
#     ip netns exec "$NAME" ip addr add "$INSIDE_IP4"/"$IP4_PREFIX_SIZE" dev "$INSIDE_PEER"
#     ip netns exec "$NAME" ip link set "$INSIDE_PEER" up
#     ip netns exec "$NAME" ip link set lo up


#     # Lesson: Configuring addresses and adding routes for the container in the routing table
#     # according to the addressing conventions selected above
#     # ip addr add dev "$OUTSIDE_PEER" "${OUTSIDE_IP4}/${IP4_PREFIX_SIZE}"
#     # ip -n "$NAME" addr add dev "$INSIDE_PEER" "${INSIDE_IP4}/${IP4_PREFIX_SIZE}"
#     # ip -n "$NAME" route add "${IP4_SUBNET}/${IP4_FULL_PREFIX_SIZE}" via "$OUTSIDE_IP4" dev "$INSIDE_PEER"
#     ip -n "$NAME" addr add dev "$INSIDE_PEER" "${INSIDE_IP4}/${IP4_PREFIX_SIZE}"
#     ip -n "$NAME" route add "${IP4_SUBNET}/${IP4_FULL_PREFIX_SIZE}" via "$OUTSIDE_IP4" dev "$INSIDE_PEER"



#     echo -n "Setting up network '$NAME' with peer ip ${INSIDE_IP4}." || echo "."
#     echo " Waiting for interface configuration to settle..."
#     echo ""
#     # Following command will wait for the link to be ready
#     wait_for_dev "$OUTSIDE_PEER" && wait_for_dev "$INSIDE_PEER" "$NAME"

#     # In the above configuration we only addressed the communication channel between
#     # the container veth interface to the host veth interface. In order to access external
#     # network from the host, packets need to be routed to the external network through host.
#     # The Host will act like a NAT router for the container traffic.
#     if [ "$INTERNET" -eq "1" ]; then
#         # Lesson: Making host the default gateway for all packets sent to veth interface with the container
#         ip -n "$NAME" route add default via "$OUTSIDE_IP4" dev "$INSIDE_PEER"
        
#         # Lesson: This iptable rule will do NAT translation for all packets having source IP same as the 
#         # ip of the container
#         iptables -t nat -A POSTROUTING -s "${INSIDE_IP4}/${IP4_PREFIX_SIZE}" -o ${DEFAULT_IFC} -j MASQUERADE

#         # Lesson: All packets to be forwarded to and fro between default public interface and outside veth interface 
#         iptables -A FORWARD -i ${DEFAULT_IFC} -o ${OUTSIDE_PEER} -j ACCEPT
#         iptables -A FORWARD -i ${OUTSIDE_PEER} -o ${DEFAULT_IFC} -j ACCEPT

#         # Lesson: Setting DNS server statically as per IITB norms
#         cp /etc/resolv.conf /etc/resolv.conf.old
#         echo "nameserver 8.8.8.8" > /etc/resolv.conf
#         echo "Internet Configured..."
#     fi
    
#     # We are exposing a port within the container to be accessible from the public interface
#     # Any TCP packet received on the OUTER_PORT of host will be forwarded to the INNER_PORT within
#     # the container. This is done to expose a service running within the container to the public
#     if [ "$EXPOSE" -eq "1" ]; then
#         # Lesson: This iptable rule will replace the ip address and port of any TCP packet received on default interface 
#         # destined to OUTER_PORT
#         iptables -t nat -A PREROUTING -p tcp -i ${DEFAULT_IFC} --dport ${OUTER_PORT} -j DNAT --to-destination ${INSIDE_IP4}:${INNER_PORT}
#         # Lesson: The above rule will only route packets received on the external interface. As a result
#         # curl <host-self-ip>:port will not work within the host itself.
#         # This iptable rule will redirect packets generated 
#         # within the host with destination set as hostip:OUTER_PORT to the containers:INNER_PORT. 
#         # This will still not work for localhost/127.0.0.1. You will have to send using the host-ip
#         iptables -t nat -A OUTPUT -o lo -m addrtype --src-type LOCAL --dst-type LOCAL -p tcp --dport ${OUTER_PORT} -j DNAT --to-destination ${INSIDE_IP4}:${INNER_PORT}
#         # Lesson: Allows forwarding of TCP session initiator packets from the public to the container
#         iptables -A FORWARD -p tcp -d ${INSIDE_IP4} --dport ${INNER_PORT} -m state --state NEW,ESTABLISHED,RELATED -j ACCEPT
#         echo "Port ${INNER_PORT} exposed to ${OUTER_PORT}..."
#     fi

#     rm -rf $NETNSDIR
#     echo "Network setup complete..."
# }
# addnetwork() {
#     local NAME=${1:-}
#     [ -z "$NAME" ] && die "Container name is required"
    
#     echo "DEBUG: Starting addnetwork for container: $NAME"
    
#     local NETNSDIR="/var/run/netns"
#     if [ ! -e "$NETNSDIR" ]; then
#         echo "DEBUG: Creating network namespace directory $NETNSDIR"
#         mkdir -p "$NETNSDIR"
#     fi

#     # Get the PID of the unshare process associated with the container
#     local PID
#     PID=$(ps -ef | grep "unshare" | grep "$CONTAINERDIR/$NAME/merged" | grep -v grep | awk '{print $2}')
#     echo "DEBUG: Found unshare PID: $PID"

#     local CONDUCTORNS="/proc/$PID/ns/net"
#     local NSDIR="$NETNSDIR/$NAME"

#     if [ -e "$NSDIR" ]; then
#         echo "DEBUG: Removing existing netns symlink $NSDIR"
#         rm -f "$NSDIR"
#     fi
#     echo "DEBUG: Creating symlink $NSDIR -> $CONDUCTORNS"
#     ln -sf "$CONDUCTORNS" "$NSDIR"

#     # Get a unique number for IP addressing
#     local NUM
#     NUM=$(get_next_num "$NAME")
#     echo "DEBUG: Unique number from get_next_num: $NUM"

#     [ -z "$IP4_PREFIX" ] && IP4_PREFIX="${IP4_SUBNET}.$((0x$NUM))."
#     echo "DEBUG: IP4_PREFIX set to: $IP4_PREFIX"

#     INSIDE_IP4="${IP4_PREFIX}2"
#     OUTSIDE_IP4="${IP4_PREFIX}1"
#     INSIDE_PEER="${NAME}-inside"
#     OUTSIDE_PEER="${NAME}-outside"
#     echo "DEBUG: INSIDE_IP4: $INSIDE_IP4, OUTSIDE_IP4: $OUTSIDE_IP4"
#     echo "DEBUG: INSIDE_PEER: $INSIDE_PEER, OUTSIDE_PEER: $OUTSIDE_PEER"

#     echo "DEBUG: Adding veth pair: $INSIDE_PEER <--> $OUTSIDE_PEER"

#     # Check if either interface already exists, and if so, delete them
#     if ip link show "$INSIDE_PEER" &>/dev/null; then
#         echo "DEBUG: Interface $INSIDE_PEER exists. Deleting it..."
#         ip link delete "$INSIDE_PEER"
#     fi
#     if ip link show "$OUTSIDE_PEER" &>/dev/null; then
#         echo "DEBUG: Interface $OUTSIDE_PEER exists. Deleting it..."
#         ip link delete "$OUTSIDE_PEER"
#     fi

#     # Now add the veth pair
#     ip link add "$INSIDE_PEER" type veth peer name "$OUTSIDE_PEER" || { echo "ERROR: Failed to add veth pair"; exit 1; }
    
#     echo "DEBUG: Setting interface $INSIDE_PEER to container namespace: $NAME"
#     ip link set "$INSIDE_PEER" netns "$NAME"
    
#     echo "DEBUG: Assigning IP $OUTSIDE_IP4/$IP4_PREFIX_SIZE to $OUTSIDE_PEER in host namespace"
#     ip addr add "$OUTSIDE_IP4"/"$IP4_PREFIX_SIZE" dev "$OUTSIDE_PEER"
    
#     echo "DEBUG: Bringing interface $OUTSIDE_PEER up"
#     ip link set "$OUTSIDE_PEER" up

#     echo "DEBUG: Enabling IP forwarding on host"
#     echo 1 > /proc/sys/net/ipv4/ip_forward

#     echo "DEBUG: Configuring container namespace: Assigning IP $INSIDE_IP4/$IP4_PREFIX_SIZE to $INSIDE_PEER"
#     ip netns exec "$NAME" ip addr add "$INSIDE_IP4"/"$IP4_PREFIX_SIZE" dev "$INSIDE_PEER"
    
#     echo "DEBUG: Bringing up interface $INSIDE_PEER in container namespace"
#     ip netns exec "$NAME" ip link set "$INSIDE_PEER" up
    
#     echo "DEBUG: Bringing up loopback interface in container namespace"
#     ip netns exec "$NAME" ip link set lo up

#     # Remove duplicate assignment: ip -n "$NAME" addr add dev "$INSIDE_PEER" "${INSIDE_IP4}/${IP4_PREFIX_SIZE}"
    
#     echo "DEBUG: Adding route in container namespace for ${IP4_SUBNET}/${IP4_FULL_PREFIX_SIZE} via $OUTSIDE_IP4"
#     ip -n "$NAME" route add "${IP4_SUBNET}/${IP4_FULL_PREFIX_SIZE}" via "$OUTSIDE_IP4" dev "$INSIDE_PEER"

#     echo "DEBUG: Waiting for interfaces to settle..."
#     wait_for_dev "$OUTSIDE_PEER" && wait_for_dev "$INSIDE_PEER" "$NAME"

#     if [ "$INTERNET" -eq "1" ]; then
#         echo "DEBUG: Setting default route in container namespace for Internet access via $OUTSIDE_IP4"
#         ip -n "$NAME" route add default via "$OUTSIDE_IP4" dev "$INSIDE_PEER"
        
#         echo "DEBUG: Adding iptables MASQUERADE rule for ${INSIDE_IP4}/${IP4_PREFIX_SIZE} on interface ${DEFAULT_IFC}"
#         iptables -t nat -A POSTROUTING -s "${INSIDE_IP4}/${IP4_PREFIX_SIZE}" -o "${DEFAULT_IFC}" -j MASQUERADE
        
#         echo "DEBUG: Adding iptables FORWARD rule: ${DEFAULT_IFC} -> ${OUTSIDE_PEER}"
#         iptables -A FORWARD -i "${DEFAULT_IFC}" -o "${OUTSIDE_PEER}" -j ACCEPT
        
#         echo "DEBUG: Adding iptables FORWARD rule: ${OUTSIDE_PEER} -> ${DEFAULT_IFC}"
#         iptables -A FORWARD -i "${OUTSIDE_PEER}" -o "${DEFAULT_IFC}" -j ACCEPT

#         echo "DEBUG: Backing up current resolv.conf and setting DNS to 8.8.8.8"
#         cp /etc/resolv.conf /etc/resolv.conf.old
#         echo "nameserver 8.8.8.8" > /etc/resolv.conf
#         echo "DEBUG: Internet Configured..."
#     fi

#     if [ "$EXPOSE" -eq "1" ]; then
#         echo "DEBUG: Adding iptables rules for port exposure"
#         iptables -t nat -A PREROUTING -p tcp -i "${DEFAULT_IFC}" --dport "${OUTER_PORT}" -j DNAT --to-destination "${INSIDE_IP4}:${INNER_PORT}"
#         iptables -t nat -A OUTPUT -o lo -m addrtype --src-type LOCAL --dst-type LOCAL -p tcp --dport "${OUTER_PORT}" -j DNAT --to-destination "${INSIDE_IP4}:${INNER_PORT}"
#         iptables -A FORWARD -p tcp -d "${INSIDE_IP4}" --dport "${INNER_PORT}" -m state --state NEW,ESTABLISHED,RELATED -j ACCEPT
#         echo "DEBUG: Port ${INNER_PORT} exposed to ${OUTER_PORT}"
#     fi

#     echo "DEBUG: Removing netns symlink directory $NETNSDIR"
#     rm -rf "$NETNSDIR"
#     echo "DEBUG: Network setup complete for container '$NAME'."
# }

addnetwork() {
    local NAME=${1:-}
    [ -z "$NAME" ] && die "Container name is required"

    local NETNSDIR="/var/run/netns"
    [ ! -e "$NETNSDIR" ] && mkdir -p "$NETNSDIR"

    # Get the PID of the unshare process associated with the container
    local PID
    PID=$(ps -ef | grep "unshare" | grep "$CONTAINERDIR/$NAME/merged" | grep -v grep | awk '{print $2}')

    local CONDUCTORNS="/proc/$PID/ns/net"
    local NSDIR="$NETNSDIR/$NAME"
    [ -e "$NSDIR" ] && rm -f "$NSDIR"
    ln -sf "$CONDUCTORNS" "$NSDIR"

    # Get a unique number for IP addressing
    local NUM
    NUM=$(get_next_num "$NAME")
    [ -z "$IP4_PREFIX" ] && IP4_PREFIX="${IP4_SUBNET}.$((0x$NUM))."

    INSIDE_IP4="${IP4_PREFIX}2"
    OUTSIDE_IP4="${IP4_PREFIX}1"
    INSIDE_PEER="${NAME}-inside"
    OUTSIDE_PEER="${NAME}-outside"

    # Delete pre-existing veth interfaces if they exist
    if ip link show "$INSIDE_PEER" &>/dev/null; then
        ip link delete "$INSIDE_PEER"
    fi
    if ip link show "$OUTSIDE_PEER" &>/dev/null; then
        ip link delete "$OUTSIDE_PEER"
    fi

    # Add the veth pair and move one end to container's namespace
    ip link add "$INSIDE_PEER" type veth peer name "$OUTSIDE_PEER" || { die "Failed to add veth pair"; }
    ip link set "$INSIDE_PEER" netns "$NAME"
    ip addr add "$OUTSIDE_IP4"/"$IP4_PREFIX_SIZE" dev "$OUTSIDE_PEER"
    ip link set "$OUTSIDE_PEER" up

    echo 1 > /proc/sys/net/ipv4/ip_forward

    ip netns exec "$NAME" ip addr add "$INSIDE_IP4"/"$IP4_PREFIX_SIZE" dev "$INSIDE_PEER"
    ip netns exec "$NAME" ip link set "$INSIDE_PEER" up
    ip netns exec "$NAME" ip link set lo up

    ip -n "$NAME" route add "${IP4_SUBNET}/${IP4_FULL_PREFIX_SIZE}" via "$OUTSIDE_IP4" dev "$INSIDE_PEER"
    wait_for_dev "$OUTSIDE_PEER" && wait_for_dev "$INSIDE_PEER" "$NAME"

    if [ "$INTERNET" -eq "1" ]; then
        ip -n "$NAME" route add default via "$OUTSIDE_IP4" dev "$INSIDE_PEER"
        iptables -t nat -A POSTROUTING -s "${INSIDE_IP4}/${IP4_PREFIX_SIZE}" -o "${DEFAULT_IFC}" -j MASQUERADE
        iptables -A FORWARD -i "${DEFAULT_IFC}" -o "${OUTSIDE_PEER}" -j ACCEPT
        iptables -A FORWARD -i "${OUTSIDE_PEER}" -o "${DEFAULT_IFC}" -j ACCEPT
        cp /etc/resolv.conf /etc/resolv.conf.old
        echo "nameserver 8.8.8.8" > /etc/resolv.conf
    fi

    if [ "$EXPOSE" -eq "1" ]; then
        iptables -t nat -A PREROUTING -p tcp -i "${DEFAULT_IFC}" --dport "${OUTER_PORT}" -j DNAT --to-destination "${INSIDE_IP4}:${INNER_PORT}"
        iptables -t nat -A OUTPUT -o lo -m addrtype --src-type LOCAL --dst-type LOCAL -p tcp --dport "${OUTER_PORT}" -j DNAT --to-destination "${INSIDE_IP4}:${INNER_PORT}"
        iptables -A FORWARD -p tcp -d "${INSIDE_IP4}" --dport "${INNER_PORT}" -m state --state NEW,ESTABLISHED,RELATED -j ACCEPT
    fi

    rm -rf "$NETNSDIR"
}







# This function is used to enable peer to peer packet traffic between two containers
peer() {
    local NAMEA=${1:-}
    local NAMEB=${2:-}
    [ -z "$NAMEA" ] && die "First Container name is required"
    [ -z "$NAMEB" ] && die "Second Container name is required"

    # Lesson: Setting iptable rules to allow two and from traffic between host container interfaces
    iptables -A FORWARD -i "${NAMEA}-outside" -o "${NAMEB}-outside" -j ACCEPT
    iptables -A FORWARD -i "${NAMEB}-outside" -o "${NAMEA}-outside" -j ACCEPT
    echo "Added peer to peer traffic between $NAMEA and $NAMEB..."
}


usage() {
    local FULL=${1:-}

    echo "Usage: $0 <command> [params] [options] [params]"
    echo ""
    echo "Commands:"
    echo "build <img-name> <conductorfile>      Build image for containers"
    echo "images                                List available images"
    echo "rmi <img>                             Delete image"
    echo "rmcache                               Delete all cache"
    echo "run <img> <cntr> -- [command <args>]  Runs [command] within a new container named <cntr> fro, the image named <img>"
    echo "ps                                    Show all running containers"
    echo "stop <cntr>                           Stop and delete container"
    echo "exec <cntr> -- [command <args>]       Execute command (default /bin/bash) in a container"
    echo "addnetwork <cntr>                     Adds layer 3 networking to the container"
    echo "peer <cntr> <cntr>                    Allow to container to communicate with each other"
    echo ""

    if [ -z "$FULL" ] ; then
        echo "Use --help to see the list of options."
        exit 1
    fi

    echo "Options:"
    echo "-h, --help                Show this usage text"
    echo ""
    echo ""
    echo "-i, --internet            Allow internet access from the container."
    echo "                          Should be used allongwith addnetwork"
    echo "                          Otherwise makes no sense."
    echo ""
    echo "-e, --expose <inner-port>-<outer-port>"
    echo "                          Expose some port of container (inner)"
    echo "                          as the host's port (outer)"
    echo ""
    echo ""
    exit 1
}

OPTS="hie:"
LONGOPTS="help,internet,expose:"

OPTIONS=$(getopt -o "$OPTS" --long "$LONGOPTS" -- "$@")
[ "$?" -ne "0" ] && usage >&2 || true

eval set -- "$OPTIONS"


while true; do
    arg="$1"
    shift

    case "$arg" in
        -h | --help)
            usage full >&2
            ;;
        -i | --internet)
            INTERNET=1
            ;;
        -e | --expose)
            PORT="$1"
            INNER_PORT=${PORT%-*}
            OUTER_PORT=${PORT#*-}
            EXPOSE=1
            shift
            ;;
        -- )
            break
            ;;
    esac
done

[ "$#" -eq 0 ] && usage >&2

case "$1" in
    build)
        CMD=build
        ;;
    images)
        CMD=images
        ;;
    rmi)
        CMD=remove_image
        ;;
    rmcache)
        CMD=rmcache
        ;;
    run)
        CMD=run
        ;;
    ps)
        CMD=show_containers
        ;;
    stop)
        CMD=stop
        ;;
    exec)
        CMD=exec
        ;;
    addnetwork)
        CMD=addnetwork
        ;;
    peer)
        CMD=peer
        ;;
    "help")
        usage full >&2
        ;;
    *)
        usage >&2
        ;;
esac

shift
check_prereq
$CMD "$@"
