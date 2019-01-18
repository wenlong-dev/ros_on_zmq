#ifndef PUB_SUB_H_
#define PUB_SUB_H_
#include <zmq.hpp>
#include <memory>
#include <functional>
#include <list>
#include <sstream>
#include "json.hpp"
class PubSubBase
{
protected:
    std::shared_ptr<zmq::socket_t> sock;
    std::string topic, md5, type, def;
public:
    std::string getMd5()
    {
        return this->md5;
    }
    virtual ~PubSubBase()
    {
        if(this->sock)
            this->sock->close();
    }
    std::string getType()
    {
        return this->type;
    }
    std::string getTopic()
    {
        return this->topic;
    }
    std::string getDef()
    {
        return this->def;
    }
    template<class T>
    void setType()
    {
        this->md5 = ros::message_traits::MD5Sum< T >::value();
        this->type = ros::message_traits::DataType< T >::value();
        this->def = ros::message_traits::Definition< T >::value();
    }
    template<class T>
    void setType(std::string topic)
    {
        this->topic = topic;
        setType<T>();
    }
};
class SubscriberT : public PubSubBase
{
private:
    std::function<void (zmq::message_t&)> cbk;
public:
    SubscriberT(zmq::context_t& context, std::string& topic, std::function<void (zmq::message_t&)> cbk)
    {
        this->topic = topic;
        this->cbk = cbk;
        this->sock.reset(new zmq::socket_t(context, ZMQ_SUB));
        const char *filter = "";
        this->sock->setsockopt(ZMQ_SUBSCRIBE, filter, strlen (filter));
    }
    void connect(std::string addr)
    {
        this->sock->connect(addr);
    }
    void disconnect(std::string addr)
    {
        this->sock->disconnect(addr);
    }
    void spinOnce()
    {
        zmq::message_t msg;
        if(this->sock->recv(&msg, ZMQ_DONTWAIT) && msg.size() > 0)
            this->cbk(msg);

    }
};
class PublisherT : public PubSubBase
{
private:
    std::string addr;
public:
    PublisherT(zmq::context_t& context)
    {
        this->sock.reset(new zmq::socket_t(context, ZMQ_PUB));

        this->sock->bind("tcp://*:*");
    
        char endpoint[128]; 
        size_t size = sizeof(endpoint);
        this->sock->getsockopt(ZMQ_LAST_ENDPOINT, endpoint, &size);

        this->addr = endpoint;
    }
    virtual ~PublisherT()
    {
        this->sock->close();
    }

    std::string getAddr()
    {
        return this->addr;
    }


    template<class T>
    void publish(T& t)
    {
        uint32_t serial_size = ros::serialization::serializationLength(t);
        boost::shared_array<uint8_t> buffer(new uint8_t[serial_size]);
        ros::serialization::OStream stream(buffer.get(), serial_size);
        ros::serialization::serialize(stream, t);
        zmq::message_t message(serial_size);
        memcpy(message.data(), buffer.get(), serial_size);
        // for(int i = 0; i < serial_size; ++i)
        //     printf("%c|", buffer[i]);
        this->sock->send(message);
    }
    
    void publish(zmq::message_t& message)
    {
        this->sock->send(message);
    }
};
typedef std::shared_ptr<PublisherT> Publisher;
typedef std::shared_ptr<SubscriberT> Subscriber;

class Node
{
private:
    std::shared_ptr<zmq::socket_t> service;
    std::shared_ptr<zmq::socket_t> notify;
    zmq::context_t context;
    std::list<Publisher> pubs;
    std::list<Subscriber> subs;

    void jsonRPC(nlohmann::json& j)
    {
        std::string req = j.dump(2);
        zmq::message_t msg(req.c_str(), req.size() + 1);
        try {
            this->service->send(msg);
            this->service->recv(&msg);
        }
        catch(zmq::error_t e)
        {
            fprintf(stderr, "socket error %s\n", e.what());
        }

        j = nlohmann::json::parse( (char*)msg.data() );
    }

    template <class TYPE>
    static void __callback(void (*func)(TYPE&), zmq::message_t& msg)
    {
        TYPE ros_msg;
        if(msg.size() < 1)
            return;
            
        ros::serialization::IStream stream((uint8_t*)msg.data(),msg.size());
        ros::serialization::Serializer<TYPE>::read(stream, ros_msg);
        func(ros_msg);
    }
    template <class T, class TYPE>
    static void __callback(void (T::*func)(TYPE&), T* t, zmq::message_t& msg)
    {
        TYPE ros_msg;
        if(msg.size() < 1)
            return;
            
        ros::serialization::IStream stream((uint8_t*)msg.data(),msg.size());
        ros::serialization::Serializer<TYPE>::read(stream, ros_msg);
        func(ros_msg);
    }
    template <class TYPE>
    Subscriber doSubscribe(std::string topic, int queue_size, std::function<void (zmq::message_t&)> cbk)
    {
        Subscriber sub(new SubscriberT(context, topic, cbk));
        sub->setType<TYPE>();
        this->subs.push_back(sub);
        

        nlohmann::json j;
        j["cmd"] = "subscribe";
        j["data"] = { 
                {"topic", sub->getTopic()}, 
                {"md5", sub->getMd5()},
                {"type", sub->getType()},
                {"def", sub->getDef()} };

        this->jsonRPC(j);

        sub->connect(j["addr"]);

        return sub;
    }
public:
    Node(): context(1)
    {
        this->service.reset(new zmq::socket_t(context, ZMQ_REQ));
        this->service->connect("tcp://localhost:5555");
        this->notify.reset(new zmq::socket_t(context, ZMQ_SUB));
        this->notify->connect("tcp://localhost:5556");
    }
    template <class TYPE>
    Publisher advertise(const std::string &name, int queue_size)
    {
        Publisher pub(new PublisherT(context));
        pub->setType<TYPE>(name);
        this->pubs.push_back(pub);

        nlohmann::json j;
        j["cmd"] = "publish";
        j["data"] = { 
                {"topic", pub->getTopic()}, 
                {"addr", pub->getAddr()}, 
                {"md5", pub->getMd5()},
                {"type", pub->getType()},
                {"def", pub->getDef()} };

        this->jsonRPC(j);
        std::cerr << j.dump(2) << std::endl;

        return pub;
    }

    template <class T, class TYPE>
    Subscriber subscribe(std::string topic, int queue_size, void (T::*func)(TYPE&), T* t)
    {
        //std::bind(__callback<TYPE>, func, std::placeholders::_1);
        auto cbk = std::bind(__callback<TYPE>, func, t, std::placeholders::_1);
        return doSubscribe<TYPE> (topic, queue_size, cbk);
    }
    template <class TYPE>
    Subscriber subscribe(std::string topic, int queue_size, void (*func)(TYPE&))
    {
        auto cbk = std::bind(__callback<TYPE>, func, std::placeholders::_1);
        return doSubscribe<TYPE> (topic, queue_size, cbk);
    }

    template <class TYPE>
    TYPE param(const std::string& txt)
    {
        nlohmann::json j;
        j["cmd"] = "getParam";
        j["name"] = txt;
        this->jsonRPC(j);
        std::string value = j["value"];
        TYPE t;
        if(value.length() > 0)
        {
            std::istringstream iss(value); 
            iss >> t;
        }
        return t;
    }
    template <class TYPE>
    bool param(const std::string& txt, TYPE& t, TYPE none)
    {
        t  = none;
        nlohmann::json j;
        j["cmd"] = "getParam";
        j["name"] = txt;
        this->jsonRPC(j);
        std::string value = j["value"];
        if(value.length() > 0)
        {
            std::istringstream iss(value); 
            iss >> t;
            return true;
        }
        return false;
    }
    
    void debug(std::ostream& out)
    {
        for(auto pub: this->pubs)
        {
            out << pub->getTopic() << std::endl;
            out << "  " << pub->getAddr() << std::endl;
            out << "  " << pub->getMd5() << std::endl;
            out << "  " << pub->getType() << std::endl;
        }
    }
    void spinOnce()
    {
        for(auto x: this->subs)
        {
            x->spinOnce();
        }

        zmq::message_t msg;
        if(this->notify->recv(&msg, ZMQ_DONTWAIT))
        {
            printf("fef%d\n", (int)msg.size());
        }
    }
};

#endif