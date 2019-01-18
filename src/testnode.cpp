#include "std_msgs/String.h"
#include "node.hpp"


void callback(std_msgs::String& msg)
{
    std::cerr << msg.data << std::endl;
}
int main(int argc, char **argv)
{
    Node node;
    Publisher pub = node.advertise<std_msgs::String>("chatter", 10);
    Subscriber sub = node.subscribe("/echo", 10, callback);
    std_msgs::String str;
    std::cerr<< node.param<int>("test") <<std::endl;
    
    for(int i = 0; 1; ++i)
    {
        str.data = std::string("helloworld") + std::to_string(i);
        pub->publish(str);
        sleep(1);
        node.spinOnce();
    }
    node.debug(std::cerr);
    //std::cout<< pub() << std::endl;
    //Core core;
    return 0;
    
}
