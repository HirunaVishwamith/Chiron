/*
 * pcie_rescan_xilinx.c
 *
 * Setuid-root helper that hot-removes every Xilinx (vendor 0x10ee) PCIe
 * device on the host, triggers a PCI bus rescan so the XDMA driver
 * re-binds against a freshly-programmed FPGA, and then relaxes the
 * permissions on the newly-created /dev/xdma* character devices so the
 * unprivileged CI user can talk to them.
 *
 * It exists so unprivileged CI users (e.g. the "github" user on dust2)
 * can perform the rescan without sudo. The helper takes no command-line
 * arguments, ignores the environment, and only ever touches sysfs nodes
 * matching /sys/bus/pci/devices/ * / {vendor,remove},
 * /sys/bus/pci/rescan, and /dev/xdma*, so the privilege surface is
 * minimal.
 *
 * Install (as root, one-time, per CI host):
 *
 *   gcc -O2 -Wall -Wextra -o /usr/local/sbin/pcie_rescan_xilinx \
 *       pcie_utils/pcie_rescan_xilinx.c
 *   chown root:github /usr/local/sbin/pcie_rescan_xilinx
 *   chmod 4750        /usr/local/sbin/pcie_rescan_xilinx
 *
 * After install, members of the "github" group can run:
 *
 *   /usr/local/sbin/pcie_rescan_xilinx
 *
 * with no sudo and no password. pcie_utils/rescan_xilinx.sh auto-detects
 * and exec()s this helper if it is present.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int write_one(const char *path)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "open(%s): %s\n", path, strerror(errno));
        return -1;
    }
    ssize_t n = write(fd, "1\n", 2);
    int saved = errno;
    close(fd);
    if (n != 2) {
        fprintf(stderr, "write(%s): %s\n", path, strerror(saved));
        return -1;
    }
    return 0;
}

static int is_xilinx(const char *bdf)
{
    char path[512];
    char buf[16];
    snprintf(path, sizeof path, "/sys/bus/pci/devices/%s/vendor", bdf);

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;

    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0)
        return 0;
    buf[n] = '\0';

    /* /sys/.../vendor is "0xXXXX\n" */
    return strncmp(buf, "0x10ee", 6) == 0;
}

/*
 * Relax permissions on every /dev/xdma* node so the unprivileged CI
 * user can open xdma0_user, xdma0_control, xdma0_h2c_*, xdma0_c2h_*,
 * xdma0_events_*, etc. The XDMA driver recreates these nodes on every
 * PCIe re-bind, so boot-time chmods in xdma.service are not enough on
 * their own.
 */
static void chmod_xdma_nodes(void)
{
    DIR *d = opendir("/dev");
    if (!d) {
        perror("opendir(/dev)");
        return;
    }

    int fixed = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "xdma", 4) != 0)
            continue;

        char path[512];
        snprintf(path, sizeof path, "/dev/%s", de->d_name);

        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        if (!S_ISCHR(st.st_mode))
            continue;

        if (chmod(path, 0666) != 0) {
            fprintf(stderr, "chmod(%s, 0666): %s\n", path, strerror(errno));
            continue;
        }
        fixed++;
    }
    closedir(d);

    printf("Relaxed permissions on %d /dev/xdma* node(s).\n", fixed);
}

int main(void)
{
    /* Be explicit: we expect the setuid bit. Refuse to run otherwise so
     * accidental non-setuid installs fail loudly instead of silently
     * inheriting the caller's permissions. */
    if (setuid(0) != 0) {
        fprintf(stderr, "setuid(0): %s "
                "(is the binary chmod 4750 root:github?)\n",
                strerror(errno));
        return 1;
    }

    DIR *d = opendir("/sys/bus/pci/devices");
    if (!d) {
        perror("opendir(/sys/bus/pci/devices)");
        return 1;
    }

    int removed = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.')
            continue;
        if (!is_xilinx(de->d_name))
            continue;

        char rpath[512];
        snprintf(rpath, sizeof rpath,
                 "/sys/bus/pci/devices/%s/remove", de->d_name);
        printf("Removing Xilinx device %s\n", de->d_name);
        if (write_one(rpath) == 0)
            removed++;
    }
    closedir(d);

    if (removed == 0) {
        fprintf(stderr,
                "warning: no Xilinx (vendor 0x10ee) devices found to remove\n");
    }

    /* Brief settle so the kernel finishes the device teardowns. */
    sleep(1);

    printf("Rescanning PCI bus...\n");
    if (write_one("/sys/bus/pci/rescan") != 0)
        return 1;

    /* Give the xdma driver a moment to re-bind and create /dev/xdma*. */
    sleep(2);

    chmod_xdma_nodes();

    printf("Done.\n");
    return 0;
}
