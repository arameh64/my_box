#ifndef FS_H
#define FS_H

/* Performs the full pivot_root sequence:
 * bind-mounts rootfs onto itself, creates old_root dir,
 * pivot_root(), chdir("/"), unmounts and removes old_root */
int fs_pivot_root(const char *new_root);

/* Mounts /proc, /sys, /dev inside the new root (call after pivot_root) */
int fs_mount_special(void);

/* Sets the container hostname via sethostname() (requires UTS namespace) */
int fs_set_hostname(const char *hostname);

#endif