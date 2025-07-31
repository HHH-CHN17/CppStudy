rtsp相当于tcp，保证不会有数据丢失；rtmp相当于udp，对并发支持的比较好

rtmp包装sps和pps（0x01前面应该还有0x17,0x00,0x00,0x00,0x00）

![image-20250731201627227](./assets/image-20250731201627227.png)

rtmp包装关键帧和非关键帧

![image-20250731201722070](./assets/image-20250731201722070.png)