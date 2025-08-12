#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <filesystem>
#include <sys/mount.h>
#include <sys/stat.h>
#include <string>
#include <fcntl.h>
#include <cstring>
#include <vector>
#include <sys/utsname.h>
#include <arpa/inet.h>

#include "stack_memory.h"
#include "process_limitation_cleanup.h"

void setup_vaiables()
{
    clearenv();
    setenv("TERM", "xterm-256color", 0);
    setenv("PATH", "/bin/:/sbin/:usr/bin:/usr/sbin", 0);
}

void setup_root(const char *folder)
{
    if (chroot(folder) == -1)
    {
        fprintf(stderr, "chroot failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (chdir("/") == -1)
    {
        fprintf(stderr, "chdir failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void run_cmd(const std::string &cmd, bool ignore_errors = false)
{
    int ret = system(cmd.c_str());
    if (ret != 0 && !ignore_errors)
    {
        std::cerr << "[FATAL] Command failed (" << ret << "): " << cmd << "\n";
        exit(EXIT_FAILURE);
    }
}

template <typename Function>
pid_t clone_process(Function &&function, int flags, void *arg = 0)
{
    auto stack = StackMemory();
    pid_t pid = clone(function, stack.top(), flags, arg);
    if (pid == -1)
    {
        fprintf(stderr, "clone failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    return pid;
}

// The function that clone_process will run in child
int run_shell(void *args)
{
    // Cast the argument back to argv type
    char *const *argv = reinterpret_cast<char *const *>(args);

    if (execvp(argv[0], argv) == -1)
    {
        int err = errno;
        std::fprintf(stderr, "execvp failed for '%s': %s\n",
                     argv[0] ? argv[0] : "(null)", std::strerror(err));
        _exit(err); // exit immediately to avoid double flush
    }

    return 0; // never reached if execvp succeeds
}

struct JailArgs
{
    const char *hostname;
    const char *ip;
    const char *root_path;
    const char *run_shell_cmd;
};

int jail(void *args)
{
    sleep(2);
    JailArgs *jargs = static_cast<JailArgs *>(args);
    const char *hostname = jargs->hostname;
    const std::string ip = jargs->ip;
    const char *root_path = jargs->root_path;
    const std::string run_shell_cmd(jargs->run_shell_cmd);

    if (sethostname(hostname, strlen(hostname)) == -1)
    {
        perror("sethostname");
        return EXIT_FAILURE;
    }

    std::string hostname_s(hostname);

    std::string veth_child = "vethc-" + hostname_s;

    // Assign IP to veth in child namespace
    run_cmd("ip addr add " + ip + "/24 dev " + veth_child);
    run_cmd("ip link set " + veth_child + " up");

    printf("child process: %d, hostname '%s'\n", getpid(), hostname);

    // cgroup limitation
    limitProcessCreation();

    // setup  the root and mount
    setup_vaiables();
    setup_root(root_path);

    if (mount("proc", "/proc", "proc", 0, NULL) == -1)
    {
        perror("mount /proc");
        exit(EXIT_FAILURE);
    }

    // Allocate args in static storage or heap
    static char *const shell_args[] = {
        const_cast<char*>(jargs->run_shell_cmd), // first argument: executable
        nullptr                                 // argv must be null-terminated
    };

    // Spawn child in jail
    pid_t pid = clone_process(run_shell, SIGCHLD, (void*)shell_args);

    waitpid(pid, nullptr, 0);
    umount("/proc");

    return EXIT_SUCCESS;
}

void setup_bridge_and_veth(const std::string &hostname, const std::string &pid, const std::string &bridge_name, const std::string &ip_addr)
{
    // Create bridge if not exists
    run_cmd("ip link add " + bridge_name + " type bridge 2>/dev/null || true");
    run_cmd("ip addr add " + ip_addr + " dev " + bridge_name + " 2>/dev/null || true");
    run_cmd("ip link set " + bridge_name + " up");

    // Create veth pair
    std::string veth_host = "vethh-" + hostname;
    std::string veth_child = "vethc-" + hostname;

    run_cmd("ip link add " + veth_host + " type veth peer name " + veth_child);

    // Move child end to child namespace
    run_cmd("ip link set " + veth_child + " netns " + pid);

    // Attach host veth to bridge and bring it up
    run_cmd("ip link set " + veth_host + " master " + bridge_name);
    run_cmd("ip link set " + veth_host + " up");
}

void cleanup_bridge_and_veth(const std::string &hostname, const char *bridge_name)
{
    std::string veth_host = "vethh-" + hostname;

    // Bring down and delete the host-side veth
    run_cmd("ip link set " + veth_host + " down 2>/dev/null || true");
    run_cmd("ip link delete " + veth_host + " 2>/dev/null || true");

    // Check if bridge has any interfaces
    FILE *fp = popen(("bridge link show | grep 'master " + std::string(bridge_name) + "'").c_str(), "r");
    char buffer[256];
    bool has_if = (fp && fgets(buffer, sizeof(buffer), fp));
    if (fp)
        pclose(fp);

    // If bridge has no connected interfaces, remove it
    if (!has_if)
    {
        run_cmd("ip link set " + std::string(bridge_name) + " down");
        run_cmd("ip link delete " + std::string(bridge_name) + " type bridge");
    }
}

bool ip_in_subnet(const char *ip_str)
{
    struct in_addr addr, subnet, mask;
    if (inet_pton(AF_INET, ip_str, &addr) != 1)
        return false;
    inet_pton(AF_INET, "192.168.1.0", &subnet);
    inet_pton(AF_INET, "255.255.255.0", &mask);
    return (addr.s_addr & mask.s_addr) == (subnet.s_addr & mask.s_addr);
}

int main(int argc, char *argv[])
{
    if (argc < 5)
    {
        fprintf(stderr, "Usage: %s <root_path> <hostname> <ip> <run_shell_cmd>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *root_path = argv[1];
    const char *hostname = argv[2];
    const char *jail_ip = argv[3];
    const char *run_shell_cmd = argv[4];

    if (!ip_in_subnet(jail_ip))
    {
        fprintf(stderr, "Error: IP must be in subnet 192.168.1.0/24\n");
        exit(EXIT_FAILURE);
    }

    printf("parent process: %d\n", getpid());

    run_cmd("sudo mount --bind -o ro ./shared_folder " + std::string(root_path) + "/var/shared_folder");

    JailArgs args;
    args.hostname = hostname;
    args.ip = jail_ip;
    args.root_path = root_path;
    args.run_shell_cmd = run_shell_cmd;

    int flags = CLONE_NEWNS | CLONE_NEWNET | CLONE_NEWPID | CLONE_NEWUTS | SIGCHLD;
    pid_t pid = clone_process(jail, flags, &args);

    std::cout << "child process: " << pid << std::endl;
    setup_bridge_and_veth(hostname, std::to_string(pid), "br0", "192.168.1.1/24");
    // clean up the cgroup file

    wait(0);
    monitor_and_cleanup_cgroup();
    cleanup_bridge_and_veth(hostname, "br0");
    run_cmd("sudo umount " + std::string(root_path) + "/var/shared_folder");

    return EXIT_SUCCESS;
}