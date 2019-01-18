# ROS On ZeroMQ
ROS On ZeroMQ采用优秀的底层通信框架ZeroMQ作为多机通信介质，并适配ROS 原生API及消息定义。<br>
ROS在多个主机间运行时，可以通过`ROS_MASTER_URI`进行通信，但是在非可靠连接乃至局域网内由于种种原因该通信机制并不可靠。<br>
该工程类似与[ROS seial](https://github.com/ros-drivers/rosserial.git)
<br>

graph LR;  
  ROSMaster-->ROSnode(local);
  ROSMaster-->ZMQBRoker(local);  
  ZMQBRoker-->ZMQNode(remote);  
  ZMQBRoker-->ZMQNode(remote);

<br>
初步版本，当前仅支持从ROS到ZMQ，和ZMQ到ROS的消息通信

## TODO:
1. ZMQNode到ZMQNode直接通信
2. RPC机制支持
3. 本机判断并增加共享内存支持