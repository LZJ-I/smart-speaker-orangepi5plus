#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <glob.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <dirent.h>

#define MOUNT_POINT "/mnt/sdcard/"

static int is_music_file(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (ext == NULL) return 0;
    if (strcasecmp(ext, ".mp3") == 0) return 1;
    if (strcasecmp(ext, ".wav") == 0) return 1;
    if (strcasecmp(ext, ".flac") == 0) return 1;
    if (strcasecmp(ext, ".m4a") == 0) return 1;
    if (strcasecmp(ext, ".aac") == 0) return 1;
    if (strcasecmp(ext, ".ogg") == 0) return 1;
    return 0;
}

static int detect_device(char *out, size_t out_size)
{
    glob_t g;
    memset(&g, 0, sizeof(g));
    if (glob("/dev/mmcblk*p*", 0, NULL, &g) == 0 && g.gl_pathc > 0) {
        snprintf(out, out_size, "%s", g.gl_pathv[0]);
        globfree(&g);
        return 0;
    }
    globfree(&g);
    if (access("/dev/sdb1", F_OK) == 0) {
        snprintf(out, out_size, "%s", "/dev/sdb1");
        return 0;
    }
    if (access("/dev/sdc1", F_OK) == 0) {
        snprintf(out, out_size, "%s", "/dev/sdc1");
        return 0;
    }
    if (access("/dev/sda1", F_OK) == 0) {
        snprintf(out, out_size, "%s", "/dev/sda1");
        return 0;
    }
    return -1;
}

static int ensure_mount_point(void)
{
    struct stat st;
    if (stat(MOUNT_POINT, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "挂载点存在但不是目录: %s\n", MOUNT_POINT);
            return -1;
        }
        return 0;
    }
    if (errno != ENOENT) {
        fprintf(stderr, "检查挂载点失败: %s\n", strerror(errno));
        return -1;
    }
    if (mkdir(MOUNT_POINT, 0777) != 0) {
        fprintf(stderr, "创建挂载点失败: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int mount_device(const char *dev)
{
    if (mount(dev, MOUNT_POINT, "exfat", 0, NULL) == 0) return 0;
    if (mount(dev, MOUNT_POINT, NULL, 0, NULL) == 0) return 0;
    fprintf(stderr, "挂载失败: %s -> %s, err=%s\n", dev, MOUNT_POINT, strerror(errno));
    return -1;
}

static int scan_music_files(void)
{
    DIR *dir = opendir(MOUNT_POINT);
    if (dir == NULL) {
        fprintf(stderr, "挂载目录不可读: %s\n", strerror(errno));
        return -1;
    }
    int count = 0;
    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (is_music_file(ent->d_name)) {
            printf("歌曲: %s\n", ent->d_name);
            count++;
        }
    }
    closedir(dir);
    if (count == 0) {
        printf("扫描完成，未发现歌曲文件\n");
        return 1;
    }
    printf("扫描完成，歌曲数量: %d\n", count);
    return 0;
}

static int unmount_device(void)
{
    if (umount(MOUNT_POINT) == 0) {
        printf("卸载成功: %s\n", MOUNT_POINT);
        return 0;
    }
    if (errno == EINVAL || errno == ENOENT) {
        printf("挂载点未挂载，可接受: %s\n", MOUNT_POINT);
        return 0;
    }
    fprintf(stderr, "卸载失败: %s\n", strerror(errno));
    return -1;
}

int main(void)
{
    char dev[128] = {0};
    int rc = 0;
    int mounted = 0;

    if (detect_device(dev, sizeof(dev)) != 0) {
        fprintf(stderr, "未检测到存储设备\n");
        return 2;
    }
    printf("检测到设备: %s\n", dev);

    if (ensure_mount_point() != 0) {
        return 3;
    }

    if (mount_device(dev) != 0) {
        return 4;
    }
    mounted = 1;
    printf("挂载成功: %s -> %s\n", dev, MOUNT_POINT);

    rc = scan_music_files();
    if (rc < 0) {
        rc = 5;
    } else if (rc > 0) {
        rc = 6;
    } else {
        rc = 0;
    }

    if (mounted) {
        if (unmount_device() != 0) {
            return 7;
        }
    }
    return rc;
}
