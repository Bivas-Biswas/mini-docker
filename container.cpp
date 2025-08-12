#include <iostream>
#include <sys/types.h>
#include <sched.h>
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
#include <poll.h>
#include <vector>
#include <sys/utsname.h>

#include "stack_memory.h"
#include "process_limitation_cleanup.h"

// we can call it like this: run("/bin/sh", "-c", "echo hello!");
template <typename... P>
int run(P... params)
{
    // Convert to const char* first
    const char *args[] = {params..., nullptr};

    // execvp replaces current process on success — only returns on failure
    if (execvp(args[0], const_cast<char *const *>(args)) == -1)
    {
        int err = errno; // capture immediately
        std::fprintf(stderr, "execvp failed for '%s': %s\n",
                     args[0] ? args[0] : "(null)", std::strerror(err));
        return err; // or exit(err);
    }

    return 0; // never reached if execvp succeeds
}

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

    if (waitpid(pid, nullptr, 0) == -1)
    {
        perror("wait");
        exit(EXIT_FAILURE);
    }

    return pid;
}

int jail(void *args)
{
    // set the hostname
    const char *hostname = (const char *)args;

    if (sethostname(hostname, strlen(hostname)) == -1)
    {
        perror("sethostname");
        return EXIT_FAILURE;
    }

    printf("child process: %d, hostname '%s'\n", getpid(), hostname);

    // cgroup limitation
    limitProcessCreation();

    // setup  the root and mount
    setup_vaiables();
    setup_root("./root");
    mount("proc", "/proc", "proc", 0, 0);

    // create a shell inside the jail proces
    auto runThis = [](void *args)
    { return run("/bin/sh"); };

    clone_process(runThis, SIGCHLD);

    umount("/proc");

    return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <hostname>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    printf("parent process: %d\n", getpid());

    int flags = CLONE_NEWPID | CLONE_NEWUTS | SIGCHLD;
    clone_process(jail, flags, argv[1]);

    // clean up the cgroup file
    monitor_and_cleanup_cgroup();

    return EXIT_SUCCESS;
}