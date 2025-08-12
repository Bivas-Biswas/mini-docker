#define PROCESS_LIMITATION_H
#ifdef PROCESS_LIMITATION_H
#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <filesystem>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <vector>

// Base path for cgroup v2
const std::string CGROUPV2_DIR_PATH = "/sys/fs/cgroup/";
const std::string CGROUPV2_PATH = CGROUPV2_DIR_PATH + "container";


void write_rule(const std::string &path, const std::string &value)
{
    // For cgroup files, O_WRONLY is usually sufficient. No append needed.
    int fp = open(path.c_str(), O_WRONLY);
    if (fp == -1)
    {
        fprintf(stderr, "Error opening %s: %s\n", path.c_str(), strerror(errno));
        return;
    }
    if (write(fp, value.c_str(), value.length()) == -1)
    {
        fprintf(stderr, "Error writing to %s: %s\n", path.c_str(), strerror(errno));
    }
    close(fp);
}

void limitProcessCreation()
{
    // Define the path for our new cgroup
    const std::string my_cgroup_path = CGROUPV2_PATH;

    // Create the directory for our container's cgroup
    if (mkdir(my_cgroup_path.c_str(), S_IRWXU) == -1 && errno != EEXIST)
    {
        fprintf(stderr, "Error creating cgroup directory: %s\n", strerror(errno));
        return;
    }

    // Enable the 'pids' controller for our new cgroup.
    // We do this by writing "+pids" to the PARENT's cgroup.subtree_control file.
    write_rule(CGROUPV2_DIR_PATH + "cgroup.subtree_control", "+pids +cpu +memory");


    // 10MB memory limit
    write_rule(my_cgroup_path + "/memory.max", "10M");
    write_rule(my_cgroup_path + "/memory.swap.max", "0"); // memory swap disable

    // cpu limit Format: "<quota> <period>"
    write_rule(my_cgroup_path + "/cpu.max", "25000 100000");

    // Set the process limit in our cgroup
    write_rule(my_cgroup_path + "/pids.max", "5");

    // Move the current process into the new cgroup
    const std::string pid = std::to_string(getpid());

    write_rule(my_cgroup_path + "/cgroup.procs", pid);
}

// Helper: Reads and parses cgroup.events to see if populated == 1
bool is_cgroup_populated(int fd)
{
    // Reset file position
    if (lseek(fd, 0, SEEK_SET) == -1)
    {
        perror("lseek failed");
        return true; // Safe default
    }

    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0)
    {
        perror("read failed");
        return true; // Safe default
    }
    buf[n] = '\0';

    std::istringstream iss(buf);
    std::string key;
    int value;

    while (iss >> key >> value)
    {
        if (key == "populated")
        {
            return (value != 0);
        }
    }

    // If not found, safe default
    return true;
}

void monitor_and_cleanup_cgroup()
{
    const std::string cgroup_path = CGROUPV2_PATH;
    std::string events_path = cgroup_path + "/cgroup.events";

    int event_fd = open(events_path.c_str(), O_RDONLY);
    if (event_fd == -1)
    {
        perror("Could not open cgroup.events for monitoring");
        return;
    }

    struct pollfd pfd{};
    pfd.fd = event_fd;
    pfd.events = POLLPRI;

    std::cout << "Parent: Monitoring cgroup. To trigger cleanup, exit the shell in the container.\n";

    while (true)
    {
        int res = poll(&pfd, 1, -1);
        if (res < 0)
        {
            perror("poll failed");
            break;
        }

        if (pfd.revents & POLLPRI)
        {
            if (!is_cgroup_populated(event_fd))
            {
                std::cout << "Parent: Detected cgroup is empty.\n";
                break;
            }
        }
    }

    close(event_fd);

    // Attempt cleanup
    std::cout << "Parent: Cleaning up cgroup directory...\n";
    if (rmdir(cgroup_path.c_str()) == -1)
    {
        std::cerr << "rmdir failed: " << strerror(errno) << "\n";
    }
    else
    {
        std::cout << "Parent: Cleanup successful.\n";
    }
}

#endif