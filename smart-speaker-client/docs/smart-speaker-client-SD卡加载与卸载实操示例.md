# SD卡加载与卸载实操示例

## 编译

- `make -C player/example`
- 产物：`player/example/bin/sdcard_mount_example`

## 执行

- `./player/example/bin/sdcard_mount_example`

## 流程

1. 设备识别  
   优先 `glob("/dev/mmcblk*p*")`，失败回退 `/dev/sdb1`、`/dev/sdc1`、`/dev/sda1`。
2. 挂载  
   挂载点固定 `/mnt/sdcard/`；不存在时创建；先尝试 `exfat`，失败再自动文件系统探测挂载。
3. 扫描  
   扫描挂载目录并打印歌曲文件名，输出歌曲数量。
4. 卸载  
   执行 `umount("/mnt/sdcard/")`；`EINVAL` 与 `ENOENT` 视为可接受。

## 异常输出覆盖

- 未检测到设备
- 挂载失败
- 挂载目录不可读
- 扫描结果为空
- 卸载失败
