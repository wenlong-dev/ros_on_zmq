#include <ros/ros.h>
#include <topic_tools/shape_shifter.h>

#include <zmq.hpp>
#include <zmq.h>
#include "json.hpp"
#include "node.hpp"

#define INFO printf
class ROSPub
{
public:
    ros::Publisher pub;
    topic_tools::ShapeShifter message;
};
class ROSNode
{
private:
    std::shared_ptr<ros::NodeHandle> nh;
public:
    ROSNode()
    {
        nh.reset(new ros::NodeHandle(""));
    }
    std::shared_ptr<ROSPub> advertise(std::string name, std::string md5sum, std::string message_type, std::string def )
    {
        INFO("register ROS publish %s\n md5 = %s\n type = %s\n def = %s\n", 
                name.c_str(), 
                md5sum.c_str(), 
                message_type.c_str(), 
                def.c_str());

        std::shared_ptr<ROSPub> r(new ROSPub());
        r->message.morph(md5sum, message_type, def, "false");
        r->pub = r->message.advertise(*nh, name, 1);
        return r;
    }
    ros::Subscriber subscribe(std::string name, std::string md5sum, std::string message_type, Publisher sock)
    {
        INFO("register ROS subscribe %s\n md5 = %s\n type = %s\n", 
                name.c_str(), 
                md5sum.c_str(), 
                message_type.c_str());

        ros::SubscribeOptions opts;
        opts.init<topic_tools::ShapeShifter>(
             name, 1, boost::bind(&ROSNode::fromROS, this, sock, _1));
        opts.md5sum = md5sum;
        opts.datatype = message_type;
        return nh->subscribe(opts);
    }
    static void toROS(std::shared_ptr<ROSPub> r, zmq::message_t& buffer) 
    {
        if (buffer.size() < 1 || r == NULL)
            return;
        ros::serialization::IStream stream((uint8_t*)buffer.data(), buffer.size());
        ros::serialization::Serializer<topic_tools::ShapeShifter>::read(stream, r->message);
        r->pub.publish(r->message);
    }
    void fromROS(Publisher sock, const boost::shared_ptr<topic_tools::ShapeShifter const>& msg) 
    {
        size_t length = ros::serialization::serializationLength(*msg);
        std::vector<uint8_t> buffer(length);

        ros::serialization::OStream ostream(&buffer[0], length);
        ros::serialization::Serializer<topic_tools::ShapeShifter>::write(ostream, *msg);
        // for(int i= 0; i < length; ++i)
        //     printf("%c|", buffer[i]);
        // printf("\n");
        zmq::message_t message(length);
        memcpy(message.data(), &buffer[0], length);
        if(sock != NULL)
            sock->publish(message);
    }
    void spinOnce()
    {
        ros::spinOnce();
    }
    std::string param(std::string name)
    {
        //ros is very ugly here
        std::string r1 = "";
        double r2 = 0;   
        nh->getParam(name, r1);
        nh->getParam(name, r2);
        if(r1.length() == 0 && fabs(r2) > 1e-6)
        {
            r1 = std::to_string(r2);
        }
         
        return r1;
    }
};
ROSNode * rosNode = NULL;
zmq::context_t* context = NULL;

class PublishInfo
{
public:
    Subscriber zmqSub;
    std::shared_ptr<ROSPub> rosPub;
    std::list<std::string> addrs;

    PublishInfo(std::string topic, std::string md5, std::string type, std::string def)
    {
        this->rosPub = rosNode->advertise(topic, md5, type, def);
        std::function<void (zmq::message_t&)> cbk = std::bind(ROSNode::toROS, this->rosPub, std::placeholders::_1);
        this->zmqSub.reset(new SubscriberT(*context, topic, cbk));
    }
    void push_back(std::string addr, bool connect = true)
    {
        this->addrs.push_back(addr);
        if(connect)
            this->zmqSub->connect(addr);
    }
    void spinOnce()
    {
        if(this->zmqSub != NULL)
        {
            this->zmqSub->spinOnce();
        }
    }
};
class SubscribeInfo
{
public:
    Publisher zmqPub;
    ros::Subscriber rosSub;

    SubscribeInfo(std::string topic, std::string md5, std::string type, std::string def)
    {
        this->zmqPub.reset(new PublisherT(*context));
        this->rosSub = rosNode->subscribe(topic, md5, type, this->zmqPub);
    }
    void push_back(std::string topic)
    {
        //this->zmqPub->connect(addr);
    }
    std::string getAddr()
    {
        return this->zmqPub->getAddr();
    }
};
class Broker
{
private:
    zmq::context_t context;
    std::shared_ptr<zmq::socket_t> service;
    std::shared_ptr<zmq::socket_t> notify;

    std::map<std::string, std::shared_ptr<PublishInfo> > publishers;
    std::map<std::string, std::shared_ptr<SubscribeInfo> > subscribers;
public:
    Broker(): context(1)
    {
        ::context = &this->context;
        this->service.reset(new zmq::socket_t(context, ZMQ_REP));
        this->service->bind("tcp://*:5555");
        INFO("bind service done!\n");

        this->notify.reset(new zmq::socket_t(context, ZMQ_PUB));
        this->notify->bind("tcp://*:5556");
        INFO("bind notify proxy done!\n");
    }
    void serviceRegPub(nlohmann::json& j, zmq::message_t& msg)
    {
        /* register publish */
        std::string topic = j["data"]["topic"];
        std::string addr = j["data"]["addr"];
        std::string md5 = j["data"]["md5"];
        std::string type = j["data"]["type"];
        std::string def = j["data"]["def"];


        if(this->publishers.find(topic) == this->publishers.end())
        {
            this->publishers[topic] = 
                std::shared_ptr<PublishInfo>( new PublishInfo(topic, md5, type, def) );
        }
        this->publishers[topic]->push_back(addr);
        
        /* notify the clients */
        // std::string addrList;
        // for(auto a: this->topicAddr[topic])
        // {
        //     addrList = addrList + " " + a;
        // }
        // nlohmann::json echo;
        // echo["cmd"] = "publish_update";
        // echo["data"] = addrList;
        // std::cout<< echo.dump(2) << std::endl;
    }
    void serviceRegSub(nlohmann::json& j, zmq::message_t& msg)
    {
        /* register subscribe */
        std::string topic = j["data"]["topic"];
        std::string md5 = j["data"]["md5"];
        std::string type = j["data"]["type"];
        std::string def = j["data"]["def"];

        if(this->subscribers.find(topic) == this->subscribers.end())
        {
            this->subscribers[topic] = 
                std::shared_ptr<SubscribeInfo>( new SubscribeInfo(topic, md5, type, def) );
        }
        std::string addr = this->subscribers[topic]->getAddr();
        nlohmann::json r;
        r["topic"] = topic;
        r["addr"] = addr;

        std::string req = r.dump(2);
        msg.rebuild(req.size() + 1);
        memcpy(msg.data(), req.c_str(), req.size() + 1);
    }
    void serviceGetParam(nlohmann::json& j, zmq::message_t& msg)
    {
        std::string name = j["name"];
        nlohmann::json r;
        r["name"] = name;
        r["value"] = rosNode->param(name);

        std::string req = r.dump(2);
        msg.rebuild(req.size() + 1);
        memcpy(msg.data(), req.c_str(), req.size() + 1);
    }
    void serviceHandle()
    {
        zmq::message_t msg;
        if(!this->service->recv(&msg, ZMQ_DONTWAIT))
            return;

        INFO("Request: %s\n", (char*)msg.data());
        nlohmann::json j = nlohmann::json::parse( (char*)msg.data() );
        if(j.find("cmd") != j.end() && j["cmd"] == "publish")
        {
            this->serviceRegPub(j, msg);
        }
        else if(j.find("cmd") != j.end() && j["cmd"] == "subscribe")
        {
            this->serviceRegSub(j, msg);
        }
        else if(j.find("cmd") != j.end() && j["cmd"] == "getParam")
        {
            this->serviceGetParam(j, msg);
        }

        INFO("Respond: %s\n", (char*)msg.data());
        this->service->send(msg);
    }
    void spinOnce()
    {
        for(auto x: this->publishers)
        {
            x.second->spinOnce();
        }
        
        this->serviceHandle();
    }   
};
int main(int argc, char** argv)
{
    ros::init(argc, argv, "broken");
    rosNode = new ROSNode();
    Broker core;
    while(1)
    {
        rosNode->spinOnce();
        core.spinOnce();
    }
    return 0;
}